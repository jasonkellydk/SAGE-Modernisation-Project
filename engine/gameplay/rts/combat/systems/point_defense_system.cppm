export module engine.gameplay.rts.combat.systems.point_defense_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.identity.components.team_member;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.combat.components.point_defense;
export import engine.gameplay.rts.combat.systems.missile_flight_system;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.resources.armor_catalog;
import Engine.Core.Math.FixedRandom;

// Point defense lasers, chunk-parallel, a tick each as the original's
// PointDefenseLaserUpdate: between scans a laser counts down and fires at
// what it tracks; every ScanRate it looks within its ScanRange for the
// closest enemy of its primary kinds (else of its secondary kinds), in firing
// range if any is (a laser that cannot hit the ground only takes airborne
// ones), and fires at once. It fires when its reload is done and the target
// is in range; a target that leaves its range makes it look again within 3
// ticks, as does one its shot kills. Its shots land this tick.
export namespace engine::gameplay
{
struct PointDefenseShots : ecs::ChunkOutputs<Shot>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PointDefenseShots>
{
	static constexpr std::string_view StableName = "engine.gameplay.point_defense_shots";
};
}

export namespace engine::gameplay
{

struct PointDefenseSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Write<PointDefense>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Exclude<OffMap>, ecs::Optional<Disabled>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Read<WeaponCatalog>, ecs::Read<ArmorCatalog>,
		ecs::Read<RandomSeed>, ecs::Write<PointDefenseShots>, ecs::Write<ShotQueue>>;
	// Whether its shot will kill what it hits (the original's isEffectivelyDead right after firing).
	using Lookup = ecs::Lookup<ecs::Read<Health>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<PointDefenseShots>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const ArmorCatalog &armors = context.Read<ArmorCatalog>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x9D1Au;
		const auto lookup = context.Lookup<Lookup>();
		auto &shots = context.Write<PointDefenseShots>().Slot(context);
		auto defenses = chunk.Get<PointDefense>();
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto teamRows = chunk.Get<TeamMember>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const auto disabledRows = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < defenses.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::None))
				continue;
			PointDefense &laser = defenses[row];
			const PointDefenseDefinition &d = laser.definition;
			if (d.weapon == WeaponCatalog::None)
				continue;
			const WeaponDefinition &weapon = weapons.At(d.weapon);
			const Engine::Math::FixedVector2 self = transforms[row].position.XY();
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			const auto distance = [&](const SpatialEntry &entry) { return Engine::Math::Distance(self, entry.position.XY()); };
			// scanClosestTarget
			const auto scan = [&] {
				const SpatialEntry *inRange[2] = {nullptr, nullptr}, *outOfRange[2] = {nullptr, nullptr};
				Fixed closest[2], closestOutside[2];
				spatial.ForEachWithin(self, d.scanRange, [&](const SpatialEntry &entry) {
					const Fixed away = distance(entry);
					if (away > d.scanRange)
						return; // from centre to centre
					const int index = (entry.classes & d.primaryClasses) != 0 ? 0 : (entry.classes & d.secondaryClasses) != 0 ? 1 : -1;
					if (index < 0)
						return;
					const bool airborne = (entry.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) != 0;
					if (!airborne && (weapon.anti & weapon_anti::Ground) == 0)
						return;
					if (!relationships.Enemies(team, player, entry.team, entry.player) || (entry.classes & target_class::Hidden) != 0)
						return;
					if (away <= weapon.attackRange)
					{
						if (inRange[index] == nullptr || away < closest[index])
						{
							closest[index] = away;
							inRange[index] = &entry;
						}
					}
					else if (inRange[index] == nullptr && (outOfRange[index] == nullptr || away < closestOutside[index]))
					{
						closestOutside[index] = away;
						outOfRange[index] = &entry;
					}
				});
				const SpatialEntry *best = inRange[0] != nullptr ? inRange[0] : inRange[1] != nullptr ? inRange[1] : outOfRange[0] != nullptr ? outOfRange[0] : outOfRange[1];
				laser.target = best != nullptr ? best->entity : ecs::Entity{};
				laser.inRange = best != nullptr && (best == inRange[0] || best == inRange[1]);
				return best != nullptr;
			};
			const auto rescanSoon = [&] {
				laser.scanLeft = static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 3));
				laser.target = {};
				if (laser.scanLeft == 0)
				{
					scan();
					laser.scanLeft = d.scanTicks;
					return true; // (it does not shoot at what that found this tick)
				}
				return false;
			};
			// fireWhenReady
			const auto fireWhenReady = [&] {
				const SpatialEntry *target = laser.target == ecs::Entity{} ? nullptr : spatial.Find(laser.target);
				if (target != nullptr)
				{
					if (distance(*target) < weapon.attackRange)
						laser.inRange = true;
					else if (laser.inRange)
					{
						// Out of range since last tick: look again soon (still shooting this tick unless it looked now).
						if (rescanSoon())
							target = nullptr;
					}
					else
						laser.inRange = false;
				}
				if (laser.shotLeft > 0)
				{
					--laser.shotLeft;
					return;
				}
				if (target == nullptr || !laser.inRange)
					return;
				shots.push_back({entities[row], target->entity, d.weapon, player, transforms[row].position, target->position, tick, tick});
				laser.shotLeft = static_cast<std::uint32_t>(weapon.delayMin);
				// It killed it: look again soon.
				const Health *health = lookup.Get<Health>(target->entity);
				if (health != nullptr && AdjustDamage(armors.At(health->armor), weapon.damageType, weapon.primaryDamage) >= health->current)
					rescanSoon();
			};
			if (laser.scanLeft > 0)
			{
				--laser.scanLeft;
				fireWhenReady();
				continue;
			}
			laser.scanLeft = d.scanTicks;
			if (scan())
				fireWhenReady();
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		ShotQueue &queue = context.Write<ShotQueue>();
		context.Write<PointDefenseShots>().ForEach([&](const Shot &shot) { queue.Add(shot); });
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PointDefenseSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.point_defense";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Its shots land this tick; after the tick's projectiles moved (it aims where they are).
	using Before = SystemTypeList<engine::gameplay::ImpactSystem>;
	using After = SystemTypeList<engine::gameplay::WeaponSystem, engine::gameplay::AutoFireSystem, engine::gameplay::ProjectileFlightSystem,
		engine::gameplay::MissileFlightSystem>;
};
}
