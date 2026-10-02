export module games.generalszh.gameplay.combat.systems.cleanup_hazard_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat.components.cleanup_hazard;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.rts.death.components.dying;
import games.generalszh.content.objects.kind_of;
import Engine.Core.Math.FixedRandom;

// CleanupHazardUpdate::update for every hazard cleaner, each tick (its AI's idle: no move, no attack, not busy; busy:
// the AI_BUSY state):
// - sent to an area (moveRange): while its AI idles it goes busy (aiBusy); an order from outside its AI (a player's or a
//   script's) ends the area job, and nothing more this tick;
// - between looks it counts down and keeps at what it picked (fireWhenReady); on a look (every ScanRate) it picks the
//   nearest CLEANUP_HAZARD (centres, 2D; on the map as it is) within ScanRange of it (sent to an area: within ScanRange
//   plus the area's reach of the area's spot), else, sent to an area with its AI idle or busy, it goes there, or once
//   within 25 of it the area job is over;
// - fireWhenReady, outside an area job: within its WeaponSlot's weapon's reach (getAttackRange with no bonus; centres,
//   2D) of what it picked it is in range; out of reach after being in range, it drops it and looks again in 0 to 3 ticks
//   (at once when 0: the new pick not fired at); then, its AI idle or busy, it locks that slot for the attack
//   (setWeaponLock LOCKED_TEMPORARILY, deferred to the weapon system through AttackTarget::lockSlot) and attacks what it
//   picked (aiAttackObject from its AI).
//   (The original takes the slot's weapon once, as it is made with its VETERAN set; here the slot's weapon as it is:
//   the same for every shipped cleaner, whose sets never change.)
export namespace generalszh::gameplay
{
struct CleanupHazardSystem
{
	using Query = ecs::Query<ecs::Write<CleanupHazard>, ecs::Write<engine::gameplay::MoveOrder>, ecs::Write<engine::gameplay::AttackTarget>,
		ecs::Write<engine::gameplay::AiActivity>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Armament>,
		ecs::Optional<engine::gameplay::WeaponSlots>, ecs::OptionalWrite<engine::gameplay::Route>, ecs::Optional<engine::gameplay::OffMap>,
		ecs::Exclude<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::OffMap>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::WeaponCatalog>,
		ecs::Read<engine::gameplay::RandomSeed>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		static constexpr std::size_t HazardBit = content::KindOfBit("CLEANUP_HAZARD");
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::WeaponCatalog &weapons = context.Read<gp::WeaponCatalog>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const auto lookup = context.Lookup<Lookup>();
		auto cleaners = chunk.Get<CleanupHazard>();
		auto moves = chunk.Get<gp::MoveOrder>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		auto activities = chunk.Get<gp::AiActivity>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto sets = chunk.Get<gp::WeaponSlots>();
		auto routes = chunk.Get<gp::Route>();
		const auto offMap = chunk.Get<gp::OffMap>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < cleaners.size(); ++row)
		{
			CleanupHazard &cleaner = cleaners[row];
			gp::MoveOrder &move = moves[row];
			gp::AttackTarget &attack = attacks[row];
			gp::AiActivity &activity = activities[row];
			const FixedVector2 at = transforms[row].position.XY();
			const bool inside = !offMap.empty();
			const auto idleOrBusy = [&] { return move.mode == gp::MoveMode::Idle && !gp::Attacking(attack); };
			const auto idle = [&] { return idleOrBusy() && activity.busy == 0; };
			// scanClosestTarget.
			const auto scan = [&]() -> bool {
				cleaner.best = {};
				const bool area = cleaner.moveRange > Fixed{};
				const FixedVector2 from = area ? cleaner.position : at;
				const Fixed range = area ? cleaner.scanRange + cleaner.moveRange : cleaner.scanRange;
				std::optional<std::pair<Fixed, ecs::Entity>> best;
				spatial.ForEachWithin(from, range, [&](const gp::SpatialEntry &entry) {
					const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), from);
					if (entry.entity == entities[row] || distance > range * range || (lookup.Get<gp::OffMap>(entry.entity) != nullptr) != inside)
						return;
					const auto *definition = lookup.Get<gp::DefinitionRef>(entry.entity);
					if (definition == nullptr || !content::HasKindOf(templates.DefinitionAt(definition->index).kinds, HazardBit))
						return;
					if (!best || distance < best->first || (distance == best->first && entry.entity.index < best->second.index))
						best.emplace(distance, entry.entity);
				});
				if (best)
					cleaner.best = best->second;
				return best.has_value();
			};
			const auto moveTo = [&](FixedVector2 to) {
				if (move.mode != gp::MoveMode::Point || move.destination != to)
				{
					move = gp::MoveToPoint(to);
					if (!routes.empty())
						routes[row].planned = false;
				}
				activity.commanded = 0;
			};
			const auto fireWhenReady = [&] {
				const gp::Transform *target = cleaner.best != ecs::Entity{} ? lookup.Get<gp::Transform>(cleaner.best) : nullptr;
				if (target != nullptr && cleaner.moveRange == Fixed{})
				{
					// m_weaponTemplate->getAttackRange(cleared bonus): its WeaponSlot's weapon (without a weapon set, its one
					// weapon is its PRIMARY).
					const std::uint32_t weapon = sets.empty() ? (cleaner.slot == 0 ? armaments[row].weapon : gp::WeaponCatalog::None)
															  : sets[row].slots[cleaner.slot < gp::WeaponSlotCount ? cleaner.slot : 0].weapon;
					const Fixed reach = weapon == gp::WeaponCatalog::None ? Fixed{} : gp::BonusAttackRange(weapons.At(weapon).attackRange, gp::WeaponBonus{});
					if (Engine::Math::DistanceSquared(target->position.XY(), at) < reach * reach)
						cleaner.inRange = 1;
					else if (cleaner.inRange != 0)
					{
						auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0xC1EAu});
						cleaner.nextScan = static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, 3));
						cleaner.best = {};
						if (cleaner.nextScan == 0)
						{
							scan();
							cleaner.nextScan = cleaner.scanTicks;
							target = nullptr; // not shot at: it may be out of reach
						}
					}
					else
						cleaner.inRange = 0;
				}
				if (target != nullptr && idleOrBusy())
				{
					// setWeaponLock(WeaponSlot, LOCKED_TEMPORARILY) (none without a weapon there), then aiAttackObject(no
					// limit, CMD_FROM_AI): its attack, no longer busy, asking for the lock as it starts (AttackTarget::lockSlot:
					// the weapon system takes it before choosing its weapon this tick).
					attack = gp::AttackTarget{.target = cleaner.best, .ordered = true};
					attack.lockSlot = static_cast<std::uint8_t>(cleaner.slot + 1);
					activity.busy = 0;
					activity.commanded = 0;
				}
			};

			if (cleaner.moveRange > Fixed{})
			{
				if (idle())
				{
					activity.busy = 1; // aiBusy(CMD_FROM_AI)
					activity.commanded = 0;
				}
				else if (activity.commanded != 0)
				{
					cleaner.moveRange = {};
					continue;
				}
			}
			if (cleaner.nextScan > 0)
			{
				--cleaner.nextScan;
				fireWhenReady();
				continue;
			}
			cleaner.nextScan = cleaner.scanTicks;
			if (scan())
				fireWhenReady();
			else if (cleaner.moveRange > Fixed{} && idleOrBusy())
			{
				if (Engine::Math::DistanceSquared(at, cleaner.position) < Fixed::FromInt(25 * 25))
					cleaner.moveRange = {};
				else
					moveTo(cleaner.position);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::CleanupHazardSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cleanup_hazard";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::TargetingSystem>;
	using After = SystemTypeList<>;
};
}
