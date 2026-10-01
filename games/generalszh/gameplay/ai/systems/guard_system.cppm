export module games.generalszh.gameplay.ai.systems.guard_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.guard;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.combat.resources.mood_ranges;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.common.areas.resources.trigger_areas;
export import engine.gameplay.common.areas.resources.area_activity;
import engine.gameplay.common.areas.systems.area_presence_system;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.common.status.components.disabled;
import Engine.Core.Math.FixedRandom;

// AIGuardMachine (retail: the struck-back condition on its inner, return and idle states) for every unit guarding a
// position or an object, chunk-parallel. Each tick it runs its state's update (its onEnter first when just entered),
// following the machine's transitions within the tick. What it guards is where the guarded object stands now, else the
// position.
//   Inner (start): attacks the nemesis (none: Outer) until it is gone or out of the inner guard range of what it guards
//     (getStdGuardRange: vision x GuardInnerModifier, the mood's range for a computer player's; inside a container, its
//     weapon's range); then Outer.
//   Outer: without pursuit, or with no nemesis: Return (the crate pick-up between is not ported); else attacks it as long
//     as it stays within the outer range (vision x GuardOuterModifier, the mood's) for GuardChaseUnitsDuration, the time
//     starting again each tick it is within the inner range; then Return.
//   Return: moves back (its first look within a random share of GuardEnemyReturnScanRate, then every rate); a look that
//     finds a target (lookForInnerTarget) goes to Inner; there (the move over): Idle.
//   Idle: looks every GuardEnemyScanRate (its first look within a random share of it); a target: Inner; else a guarded
//     object that moved more than two pathfinding cells along x or y since the last look (m_guardeePos, zero at first):
//     Return.
//   Aggressor: attacks its last attacker within the inner range for the chase duration; then Inner.
//   Inner, Return and Idle strike back (hasAttackedMeAndICanReturnFire: its last damage not yet looked at, by a living
//   enemy it may attack): Aggressor.
// lookForInnerTarget: its team's common victim; else the enemy nearest what it guards (centre distance) it may attack
// within the inner range, only airborne ones when guarding against flyers. Leaving Inner or Aggressor clears the team's
// victim (GuardEvents, applied after the step).
// Guarding a trigger area: what it guards is the area's middle; it looks into the area only within GuardEnemyScanRate of
// the last change of what the areas hold, as far as the area's radius, for what is inside it; it chases as far as the
// greater of its outer range and that radius. (The original left an area guard's own position at the origin, which its
// inner and aggressor attacks measured from: a quirk fixed, they measure from the area's middle.)
// Retaliating (AIGuardRetaliateMachine, retail: the struck-back condition on its return and idle states only): it starts
// striking back at the aggressor (Aggressor: the aggressor, else its last attacker if an enemy; out beyond its outer
// range plus its inner one, or the chase duration, it gives up: Return); Inner attacks within 1.5 times its inner
// range, Outer within 0.67 of its outer plus inner range; an attack also ends once it is itself beyond its inner range
// of where it stood; Idle's look that finds nothing ends the machine (GuardEvent::End: its AI idles). Its look passes
// over structures but for base defences and containers that may attack (PartitionFilterRejectBuildings; a computer
// player's looks take them all).
// Not yet: enter/hijack guards (isEnterGuard), crates.
export namespace generalszh::gameplay
{
struct GuardSystem
{
	using Query = ecs::Query<ecs::Write<Guard>, ecs::Write<engine::gameplay::AttackTarget>, ecs::Write<engine::gameplay::MoveOrder>,
		ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Aggression>, ecs::Read<engine::gameplay::TeamMember>,
		ecs::Optional<engine::gameplay::Armament>, ecs::Optional<engine::gameplay::Health>, ecs::Optional<engine::gameplay::OffMap>,
		ecs::Optional<engine::gameplay::WeaponSlots>, ecs::OptionalWrite<engine::gameplay::Route>, ecs::Exclude<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Transport>, ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::Sale>,
		ecs::Read<engine::gameplay::Disabled>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<engine::gameplay::MoodRanges>, ecs::Read<GuardRules>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Read<engine::gameplay::TriggerAreas>, ecs::Read<engine::gameplay::AreaActivity>, ecs::Read<ObjectTemplates>,
		ecs::Write<GuardEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<GuardEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using State = GuardState;
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector2;
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::WeaponCatalog &weapons = context.Read<gp::WeaponCatalog>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const gp::MoodRanges &moods = context.Read<gp::MoodRanges>();
		const GuardRules &rules = context.Read<GuardRules>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const gp::TriggerAreas &areas = context.Read<gp::TriggerAreas>();
		const std::uint64_t areasChanged = context.Read<gp::AreaActivity>().lastChange;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<GuardEvents>().Slot(context);
		auto guards = chunk.Get<Guard>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		auto orders = chunk.Get<gp::MoveOrder>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto aggressions = chunk.Get<gp::Aggression>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto healths = chunk.Get<gp::Health>();
		const auto offMap = chunk.Get<gp::OffMap>();
		const auto sets = chunk.Get<gp::WeaponSlots>();
		auto routes = chunk.Get<gp::Route>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
		for (std::size_t row = 0; row < guards.size(); ++row)
		{
			Guard &guard = guards[row];
			const bool retaliate = guard.mode == GuardMode::Retaliate;
			gp::AttackTarget &attack = attacks[row];
			gp::MoveOrder &order = orders[row];
			const ecs::Entity self = entities[row];
			const std::uint32_t player = owners[row].player;
			const bool inside = !offMap.empty();
			const gp::Aggression &aggression = aggressions[row];
			const bool armed = !armaments.empty();
			const gp::WeaponDefinition weapon = armed ? gp::TargetingSystem::Reach(weapons, armaments[row], sets.empty() ? nullptr : &sets[row], 0) : gp::WeaponDefinition{};
			// There: indexed, not hidden, alive.
			const auto there = [&](ecs::Entity target) -> const gp::SpatialEntry * {
				if (target == ecs::Entity{})
					return nullptr;
				const gp::SpatialEntry *entry = spatial.Find(target);
				if (entry == nullptr || (entry->classes & gp::target_class::Hidden) != 0)
					return nullptr;
				if (const gp::Health *health = lookup.Get<gp::Health>(target); health != nullptr && gp::IsDead(*health))
					return nullptr;
				return entry;
			};
			const auto mayAttack = [&](const gp::SpatialEntry &entry) {
				return armed && gp::TargetingSystem::Acceptable(relationships, entry, self, members[row].team, player, weapon, aggression);
			};
			// A guarded area: its bounds' middle and half their diagonal (getCenterPoint, getRadius).
			const gp::TriggerArea *polygon = guard.area < areas.areas.size() ? &areas.areas[guard.area] : nullptr;
			FixedVector2 areaCentre;
			Fixed areaRadius;
			if (polygon != nullptr)
			{
				const auto twice = polygon->CenterTimesTwo();
				areaCentre = {Fixed::FromRaw(twice[0] << 15), Fixed::FromRaw(twice[1] << 15)};
				areaRadius = Engine::Math::Sqrt(Fixed::FromInt(polygon->RadiusSquaredTimesFour())) / Fixed::FromInt(2);
			}
			// What it guards: the object where it stands now, the area's middle, else the position.
			const auto guarded = [&]() -> FixedVector2 {
				if (guard.target != ecs::Entity{})
					if (const gp::Transform *at = lookup.Get<gp::Transform>(guard.target))
						return at->position.XY();
				return polygon != nullptr ? areaCentre : guard.position;
			};
			// getAdjustedVisionRangeForObject (owner type, mood; the inner or outer guard factor).
			const bool human = player < roster.PlayerCount() && roster.PlayerAt(player).human;
			const auto range = [&](bool innerRange) -> Fixed {
				if (inside)
					return weapon.attackRange;
				return gp::GuardVision(moods, aggression.vision, human, innerRange, aggression.attitude);
			};
			const gp::Team *team = members[row].team < roster.TeamCount() ? &roster.TeamAt(members[row].team) : nullptr;
			const auto teamVictim = [&]() -> ecs::Entity {
				if (team == nullptr || !team->attackCommonTarget)
					return {};
				const gp::SpatialEntry *entry = there(team->commonTarget);
				return entry != nullptr && (entry->classes & gp::target_class::Aircraft) == 0 ? team->commonTarget : ecs::Entity{};
			};
			// hasAttackedMeAndICanReturnFire.
			const auto struckBack = [&]() -> bool {
				const gp::Health *health = healths.empty() ? nullptr : &healths[row];
				if (health == nullptr || health->lastDamageTick <= guard.damageSeen)
					return false;
				guard.damageSeen = health->lastDamageTick;
				const gp::SpatialEntry *attacker = there(health->lastAttacker);
				return attacker != nullptr && relationships.Enemies(members[row].team, player, attacker->team, attacker->player) && mayAttack(*attacker);
			};
			// PartitionFilterRejectBuildings for a player's look (not a computer's): a base defence, or a container
			// able to attack (not under construction, sold or subdued).
			const auto takesBuilding = [&](ecs::Entity building) -> bool {
				if (const auto *ref = lookup.Get<gp::DefinitionRef>(building); ref != nullptr && templates.DefinitionAt(ref->index).Is("FS_BASE_DEFENSE"))
					return true;
				if (lookup.Get<gp::Transport>(building) == nullptr || lookup.Get<gp::UnderConstruction>(building) != nullptr || lookup.Get<gp::Sale>(building) != nullptr)
					return false;
				const gp::Disabled *off = lookup.Get<gp::Disabled>(building);
				return off == nullptr || (off->mask & gp::disabled_type::Subdued) == 0;
			};
			// lookForInnerTarget.
			const auto lookForInner = [&]() -> bool {
				if (!armed)
					return false;
				if (const ecs::Entity victim = teamVictim(); victim != ecs::Entity{})
				{
					guard.nemesis = victim;
					return true;
				}
				const FixedVector2 from = guarded();
				Fixed reach = range(true);
				// An area is looked into only just after what it holds changed (getFrameObjectsChangedTriggerAreas plus
				// GuardEnemyScanRate), as far out as its radius, for what is inside it.
				if (polygon != nullptr)
				{
					if (tick > areasChanged + rules.scanTicks)
						return false;
					reach = areaRadius;
				}
				if (reach <= Fixed{})
					return false;
				const gp::SpatialEntry *best = nullptr;
				Fixed bestDistance;
				spatial.ForEachWithin(from, reach, [&](const gp::SpatialEntry &entry) {
					const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), from);
					if (distance > reach * reach || !relationships.Enemies(members[row].team, player, entry.team, entry.player) || !mayAttack(entry))
						return;
					if (guard.mode == GuardMode::FlyingOnly && (entry.classes & (gp::target_class::AirborneVehicle | gp::target_class::AirborneInfantry)) == 0)
						return;
					if (retaliate && (entry.classes & gp::target_class::Structure) != 0 && human && !takesBuilding(entry.entity))
						return;
					if (polygon != nullptr && !polygon->Contains(gp::area_presence_detail::Whole(entry.position.x), gp::area_presence_detail::Whole(entry.position.y)))
						return;
					if (best == nullptr || distance < bestDistance || (distance == bestDistance && entry.entity.index < best->entity.index))
					{
						best = &entry;
						bestDistance = distance;
					}
				});
				if (best == nullptr)
					return false;
				guard.nemesis = best->entity;
				return true;
			};
			// AIAttackState with ExitConditions on the nemesis.
			const auto startAttack = [&](FixedVector2 center, Fixed radius, std::uint8_t flags, std::uint64_t giveUp) {
				attack = gp::AttackTarget{guard.nemesis, true};
				guard.exitCenter = center;
				guard.exitRadius = radius;
				guard.exitFlags = flags;
				guard.giveUp = giveUp;
			};
			const auto shouldExit = [&]() -> bool {
				const gp::SpatialEntry *goal = there(attack.target);
				if (goal == nullptr)
					return true; // no unit (and the attack itself is over)
				if ((guard.exitFlags & guard_exit::Expired) != 0 && tick >= guard.giveUp)
					return true;
				if ((guard.exitFlags & guard_exit::Outside) == 0)
					return false;
				if (Engine::Math::DistanceSquared(goal->position.XY(), guard.exitCenter) > guard.exitRadius * guard.exitRadius)
					return true;
				// GuardRetaliateExitConditions: never beyond its inner range of where it stood.
				const Fixed inner = range(true);
				return retaliate && Engine::Math::DistanceSquared(transforms[row].position.XY(), guard.exitCenter) > inner * inner;
			};
			// The attack's centre follows a guarded object.
			const auto follow = [&] {
				if (guard.target != ecs::Entity{})
					if (const gp::Transform *at = lookup.Get<gp::Transform>(guard.target))
						guard.exitCenter = at->position.XY();
			};
			const auto roll = [&](std::uint64_t most, std::uint32_t salt) {
				auto random = Engine::Math::Stream(seed, {tick, self.index, self.generation, salt});
				return static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(most)));
			};
			const auto go = [&](State next) {
				if (guard.state == State::Inner || guard.state == State::Outer || guard.state == State::Aggressor)
					attack = {};
				if (guard.state == State::Inner || guard.state == State::Aggressor)
					out.push_back({self, GuardEvent::Kind::ClearTeamTarget});
				if (guard.state == State::Return && order.mode == gp::MoveMode::Point)
					order.mode = gp::MoveMode::Idle;
				guard.state = next;
				guard.entered = 0;
			};

			for (int step = 0; step < 8; ++step)
			{
				if (guard.entered != 0 && ((!retaliate && guard.state == State::Inner) || guard.state == State::Return || guard.state == State::Idle) && struckBack())
				{
					go(State::Aggressor);
					continue;
				}
				if (guard.state == State::Inner)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						if (there(guard.nemesis) == nullptr)
						{
							go(State::Outer);
							continue;
						}
						startAttack(guarded(), retaliate ? range(true) * Fixed::FromRatio(3, 2) : range(true), guard_exit::Outside | guard_exit::NoUnit, 0);
						break;
					}
					follow();
					if (shouldExit())
					{
						go(State::Outer);
						continue;
					}
					break;
				}
				if (guard.state == State::Outer)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						if (guard.mode == GuardMode::WithoutPursuit || there(guard.nemesis) == nullptr)
						{
							go(State::Return);
							continue;
						}
						const Fixed outer = retaliate ? (range(false) + range(true)) * Fixed::FromRatio(67, 100)
							: polygon != nullptr ? std::max(range(false), areaRadius) : range(false);
						startAttack(guarded(), std::max(outer, Fixed{}), guard_exit::Expired | guard_exit::Outside | guard_exit::NoUnit, tick + rules.chaseTicks);
						break;
					}
					follow();
					// Within the inner range the chase time starts again.
					if (const gp::SpatialEntry *goal = there(attack.target))
					{
						const Fixed inner = range(true);
						if (Engine::Math::DistanceSquared(goal->position.XY(), guard.exitCenter) <= inner * inner)
							guard.giveUp = tick + rules.chaseTicks;
					}
					if (shouldExit())
					{
						go(State::Return);
						continue;
					}
					break;
				}
				if (guard.state == State::Return)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						guard.nextReturnScan = tick + roll(rules.returnScanTicks, 0x6A71u);
						// A new move: its route planned afresh (as aiMoveToPosition), even to where it went before.
						order = gp::MoveToPoint(guarded());
						if (!routes.empty())
							routes[row].planned = false;
						break;
					}
					if (tick >= guard.nextReturnScan)
					{
						guard.nextReturnScan = tick + rules.returnScanTicks;
						if (lookForInner())
						{
							go(State::Inner);
							continue;
						}
					}
					if (order.mode == gp::MoveMode::Idle)
					{
						go(State::Idle);
						continue;
					}
					break;
				}
				if (guard.state == State::Idle)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						guard.nextScan = tick + roll(rules.scanTicks, 0x6A72u);
						break;
					}
					if (tick < guard.nextScan)
						break;
					guard.nextScan = tick + rules.scanTicks;
					if (lookForInner())
					{
						go(State::Inner);
						continue;
					}
					if (retaliate)
					{
						// EXIT_MACHINE_WITH_SUCCESS: AI_IDLE.
						out.push_back({self, GuardEvent::Kind::End});
						break;
					}
					if (guard.target != ecs::Entity{})
						if (const gp::Transform *at = lookup.Get<gp::Transform>(guard.target))
						{
							const FixedVector2 now = at->position.XY();
							const Fixed dx = guard.guardeeAt.x - now.x, dy = guard.guardeeAt.y - now.y;
							if (dx * dx > cell * cell * 4 || dy * dy > cell * cell * 4)
							{
								guard.guardeeAt = now;
								go(State::Return);
								continue;
							}
						}
					break;
				}
				// Aggressor.
				const State afterAggressor = retaliate ? State::Return : State::Inner;
				if (guard.entered == 0)
				{
					guard.entered = 1;
					const gp::Health *health = healths.empty() ? nullptr : &healths[row];
					if (!retaliate)
					{
						if (health != nullptr && health->lastAttacker != ecs::Entity{})
							guard.nemesis = health->lastAttacker;
					}
					else if (there(guard.nemesis) == nullptr && health != nullptr && health->lastAttacker != ecs::Entity{})
					{
						// The given nemesis gone: the last damage's source, if an enemy.
						if (const gp::SpatialEntry *last = there(health->lastAttacker);
							last != nullptr && relationships.Enemies(members[row].team, player, last->team, last->player))
							guard.nemesis = health->lastAttacker;
					}
					if (there(guard.nemesis) == nullptr)
					{
						go(afterAggressor);
						continue;
					}
					startAttack(guarded(), retaliate ? range(false) + range(true) : range(true), guard_exit::Expired | guard_exit::Outside | guard_exit::NoUnit,
						tick + rules.chaseTicks);
					break;
				}
				follow();
				if (shouldExit())
				{
					go(afterAggressor);
					continue;
				}
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::GuardSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.guards";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::TargetingSystem>;
	using After = SystemTypeList<>;
};
}
