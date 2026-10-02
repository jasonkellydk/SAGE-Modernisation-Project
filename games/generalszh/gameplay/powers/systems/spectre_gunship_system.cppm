export module games.generalszh.gameplay.powers.systems.spectre_gunship_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.powers.components.spectre_gunship;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.death.components.dying;
import Engine.Core.Math.FixedRandom;

// SpectreGunshipUpdate::update for each gunship on an attack, every tick (a batch: one or two in the air; its orders go
// in one list, carried out after the tick):
//   dead: nothing more;
//   coming in or circling: it heads for the orbit point (OrbitInsertionSlope of the way from straight at the target to
//   a quarter turn round, GunshipOrbitRadius out); the reticle is kept within AttackAreaRadius less
//   TargetingReticleRadius of the centre; coming in, once within GunshipOrbitRadius of the centre (2D) it circles: its
//   OrbitTime starts, its gattling wakes (DISABLED_PARALYZED off), it takes its normal set, afterburners off;
//   circling: OrbitTime up, its gattling goes and it leaves along its heading on its panic set, afterburners on; else
//   every HowitzerFiringRate ticks (the frame a multiple of it) its gattling attacks the nearest live, seen,
//   attackable enemy in the reticle not under it (beyond 3/4 GunshipOrbitRadius from it, 2D) - a computer's gunship
//   tries the whole area next - else the spot its fire has walked to; the gattling steady longer than
//   HowitzerFollowLag, the howitzer fires there too, RandomOffsetForHowitzer off each way; while the gattling fires,
//   its fire walks toward the spot it shoots at by StrafingIncrement a tick (arrived: steady one more tick);
//   leaving: off the map (its extent), it is gone.
// The decals and the gattling's strafe smoke are the presentation's.
export namespace generalszh::gameplay
{
struct SpectreGunshipSystem
{
	using Query = ecs::Query<ecs::Write<SpectreGunship>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Owner>, ecs::Exclude<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Armament>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::GroundHeight>, ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::TeamRoster>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<GunshipEvents>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		using Engine::Math::FixedVector3;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const gp::WeaponCatalog &weapons = context.Read<gp::WeaponCatalog>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const auto lookup = context.Lookup<Lookup>();
		auto &events = context.Write<GunshipEvents>().list;
		events.clear();
		const std::uint64_t now = context.Tick();
		const auto [low, high] = ground.Extent();
		query.ForEachChunk([&](auto chunk) {
			auto ships = chunk.template Get<SpectreGunship>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < ships.size(); ++row)
			{
				SpectreGunship &ship = ships[row];
				const SpectreGunshipConfig *config = templates.SpectreGunshipOf(definitions[row].index);
				if (config == nullptr || ship.status == GunshipStatus::Idle)
					continue;
				const ecs::Entity me = entities[row];
				const FixedVector3 &at = transforms[row].position;
				const auto push = [&](GunshipOrder order, FixedVector3 where = {}, ecs::Entity target = {}, std::uint8_t set = 0) {
					events.push_back({me, order, set, target, where});
				};
				if (ship.status == GunshipStatus::Inserting || ship.status == GunshipStatus::Orbiting)
				{
					FixedVector2 perigee = at.XY() - ship.initialTarget.XY();
					const Fixed toTarget = Engine::Math::Length(perigee);
					perigee = toTarget > Fixed{} ? perigee / toTarget : FixedVector2{};
					const FixedVector2 apogee{-perigee.y, perigee.x};
					const Fixed n1 = std::clamp(config->orbitInsertionSlope, Fixed::FromRatio(1, 2), Fixed::FromRatio(4, 5));
					const FixedVector2 declination = (perigee * n1 + apogee * (Fixed::One() - n1)) * config->orbitRadius;
					const FixedVector2 satellite = ship.initialTarget.XY() + declination;
					push(GunshipOrder::ShipMove, {satellite.x, satellite.y, Fixed{}});
					const Fixed constraint = config->attackAreaRadius - config->reticleRadius;
					FixedVector3 offset = ship.initialTarget - ship.reticle;
					const Fixed offsetLength = Engine::Math::Length(offset);
					if (offsetLength > constraint && offsetLength > Fixed{})
					{
						offset = offset / offsetLength * constraint;
						ship.reticle.x = ship.initialTarget.x - offset.x;
						ship.reticle.y = ship.initialTarget.y - offset.y;
					}
					if (ship.status == GunshipStatus::Inserting && toTarget < config->orbitRadius)
					{
						ship.status = GunshipStatus::Orbiting;
						ship.orbitEscapeTick = now + config->orbitTicks;
						push(GunshipOrder::GattlingParalyzed, {}, {}, 0);
						push(GunshipOrder::ShipSet, {}, {}, 0);
					}
				}
				if (ship.status == GunshipStatus::Orbiting)
				{
					if (now >= ship.orbitEscapeTick)
					{
						// cleanUp; disengageAndDepartAO: off along its heading, far.
						push(GunshipOrder::DestroyGattling);
						ship.status = GunshipStatus::Departing;
						const auto heading = Engine::Math::Direction(transforms[row].facing);
						const FixedVector2 away = at.XY() + FixedVector2{heading.x, heading.y} * Fixed::FromInt(99999);
						push(GunshipOrder::ShipMove, {away.x, away.y, Fixed{}});
						push(GunshipOrder::ShipSet, {}, {}, 2);
					}
					else
					{
						if (config->howitzerFiringTicks > 0 && now % config->howitzerFiringTicks == 0)
						{
							ship.shootAt = ship.reticle;
							const std::uint32_t player = owners[row].player;
							const gp::Armament *gun = lookup.IsAlive(ship.gattling) ? lookup.Get<gp::Armament>(ship.gattling) : nullptr;
							const gp::WeaponDefinition *weapon = gun != nullptr && gun->weapon != gp::WeaponCatalog::None ? &weapons.At(gun->weapon) : nullptr;
							// PartitionFilterLiveMapEnemies, StealthedAndUndetected, PossibleToAttack, FreeOfFog; near to far.
							const auto find = [&](FixedVector2 center, Fixed radius) -> const gp::SpatialEntry * {
								const gp::SpatialEntry *best = nullptr;
								Fixed bestDistance;
								spatial.ForEachWithin(center, radius, [&](const gp::SpatialEntry &entry) {
									if (entry.entity == me || !relationships.Enemies(player, entry.player) || (entry.classes & gp::target_class::Hidden) != 0 ||
										(entry.clearTo >> player & 1u) == 0 || (weapon != nullptr && !gp::CanTarget(*weapon, entry.classes)))
										return;
									// isFairDistanceFromShip: not under it.
									if (Engine::Math::Distance(at.XY(), entry.position.XY()) <= config->orbitRadius * Fixed::FromRatio(3, 4))
										return;
									const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), center);
									if (best == nullptr || distance < bestDistance)
									{
										best = &entry;
										bestDistance = distance;
									}
								});
								return best;
							};
							const gp::SpatialEntry *target = find(ship.reticle.XY(), config->reticleRadius);
							const bool human = player < roster.PlayerCount() && roster.PlayerAt(player).human;
							if (target == nullptr && !human)
							{
								target = find(ship.initialTarget.XY(), config->attackAreaRadius);
								if (target != nullptr)
									ship.shootAt = target->position;
							}
							if (target != nullptr)
								push(GunshipOrder::GattlingAttack, {}, target->entity);
							else
								push(GunshipOrder::GattlingAttackAt, ship.gattlingTarget);
							if (ship.steadyTicks > config->howitzerFollowLag && config->howitzer != gp::WeaponCatalog::None)
							{
								auto random = Engine::Math::Stream(seed, {now, me.index, me.generation, 0x5EC7u});
								const std::int64_t offset = config->randomOffset.Floor();
								const FixedVector3 spot{ship.gattlingTarget.x + Fixed::FromInt(Engine::Math::UniformInt(random, -offset, offset)),
									ship.gattlingTarget.y + Fixed::FromInt(Engine::Math::UniformInt(random, -offset, offset)), ship.gattlingTarget.z};
								push(GunshipOrder::Howitzer, spot);
							}
						}
						// Its gattling firing (OBJECT_STATUS_IS_FIRING_WEAPON: a shot within the last few ticks): its fire walks.
						const gp::Armament *gun = lookup.IsAlive(ship.gattling) ? lookup.Get<gp::Armament>(ship.gattling) : nullptr;
						if (gun != nullptr && gun->firedTick != 0 && now - gun->firedTick <= 4)
						{
							ship.strafedTick = now;
							const FixedVector3 delta = ship.shootAt - ship.gattlingTarget;
							const Fixed distance = Engine::Math::Length(delta);
							if (distance < config->strafingIncrement)
							{
								ship.gattlingTarget = ship.shootAt;
								++ship.steadyTicks;
							}
							else
							{
								ship.steadyTicks = 0;
								ship.gattlingTarget = ship.gattlingTarget + delta / distance * config->strafingIncrement;
							}
						}
					}
				}
				else if (ship.status == GunshipStatus::Departing)
				{
					if (at.x < low.x || at.y < low.y || at.x > high.x || at.y > high.y)
					{
						push(GunshipOrder::DestroyShip);
						ship.status = GunshipStatus::Idle;
					}
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::SpectreGunshipSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spectre_gunship";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
