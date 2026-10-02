export module games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.tunnel_guard;
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
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.teams.resources.team_roster;
import Engine.Core.Math.FixedRandom;

// AITNGuardMachine for every unit guarding its player's tunnel network, chunk-parallel. Each tick it runs its state's
// update (its onEnter first when just entered), following the machine's transitions within the tick:
//   Return (start): inside a tunnel already: Idle; its team has a victim (teamAttackCommonTarget): that is the nemesis,
//     Inner; no tunnel: Inner; else it goes into the nearest of its player's tunnels (findBestTunnel: centre distance);
//     while going, a team victim or the network's nemesis sends it to Inner; inside, Idle; the entering given up: Idle.
//   Idle: looks every GuardEnemyScanRate (its first look within a random share of it): lookForInnerTarget (the team
//     victim; the network's nemesis; a tunnel of the network hit within the scan rate by an enemy it may attack, who
//     becomes the team's victim and the network's nemesis); found, it leaves the network at once through the tunnel
//     nearest the nemesis, or (outside) goes to Inner; nothing and outside while a tunnel stands: Return.
//   Inner: attacks the nemesis until GuardChaseUnitsDuration passes; the nemesis gone, the team victim or the network's
//     nemesis takes over, else one look (TunnelNetworkScan: the enemy nearest its centre it may attack within its inner
//     guard range) and, that failing, Outer. A team victim other than the nemesis becomes the nemesis (the attack keeps
//     its goal) and the old one the network's.
//   Outer: without pursuit, or with no nemesis: Return; else attacks it for another chase duration (its goal gone: the
//     nemesis, or with a common target the team's victim), then Return (a crate to pick up is not ported).
//   Return and Inner strike back (hasAttackedMeAndICanReturnFire: its last damage not yet looked at, by a living enemy it
//     may attack): Aggressor attacks that attacker (the network's nemesis), keeping it the nemesis while it fires, and
//     on leaving clears the team's victim; then Return.
// A target is there while it is in the spatial index, not hidden and alive. What reaches beyond the unit (boarding,
// leaving, the nemesis, the team's victim) goes out as TunnelGuardEvents, applied after the step.
export namespace generalszh::gameplay
{
namespace tunnel_guard_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

struct Env
{
	const gp::SpatialIndex &spatial;
	const gp::Relationships &relationships;
	const gp::WeaponCatalog &weapons;
	const gp::TeamRoster &roster;
	const gp::CargoManifest &manifest;
	const gp::MoodRanges &moods;
	const TunnelGuardRules &rules;
	std::uint64_t tick;
	std::uint64_t seed;
};
}

struct TunnelGuardSystem
{
	using Query = ecs::Query<ecs::Write<TunnelGuard>, ecs::Write<engine::gameplay::AttackTarget>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::Aggression>,
		ecs::Read<engine::gameplay::TeamMember>, ecs::Optional<engine::gameplay::Health>, ecs::Optional<engine::gameplay::OffMap>,
		ecs::Optional<engine::gameplay::Boarding>, ecs::Optional<engine::gameplay::WeaponSlots>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::AttackTarget>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::Relationships>,
		ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<engine::gameplay::CargoManifest>,
		ecs::Read<engine::gameplay::MoodRanges>, ecs::Read<TunnelGuardRules>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<TunnelGuardEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<TunnelGuardEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using State = TunnelGuardState;
		using Kind = TunnelGuardEvent::Kind;
		using Engine::Math::Fixed;
		const tunnel_guard_detail::Env env{context.Read<gp::SpatialIndex>(), context.Read<gp::Relationships>(), context.Read<gp::WeaponCatalog>(),
			context.Read<gp::TeamRoster>(), context.Read<gp::CargoManifest>(), context.Read<gp::MoodRanges>(), context.Read<TunnelGuardRules>(), context.Tick(),
			context.Read<gp::RandomSeed>().value};
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<TunnelGuardEvents>().Slot(context);
		auto guards = chunk.Get<TunnelGuard>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto aggressions = chunk.Get<gp::Aggression>();
		const auto members = chunk.Get<gp::TeamMember>();
		const auto healths = chunk.Get<gp::Health>();
		const auto offMap = chunk.Get<gp::OffMap>();
		const auto boardings = chunk.Get<gp::Boarding>();
		const auto sets = chunk.Get<gp::WeaponSlots>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = env.tick;
		for (std::size_t row = 0; row < guards.size(); ++row)
		{
			TunnelGuard &guard = guards[row];
			gp::AttackTarget &attack = attacks[row];
			const ecs::Entity self = entities[row];
			const std::uint32_t player = owners[row].player;
			const Engine::Math::FixedVector2 at = transforms[row].position.XY();
			const bool inside = !offMap.empty();
			const gp::Aggression &aggression = aggressions[row];
			const gp::WeaponDefinition weapon =
				gp::TargetingSystem::Reach(env.weapons, armaments[row], sets.empty() ? nullptr : &sets[row], 0);
			const auto emit = [&](Kind kind, ecs::Entity target) { out.push_back({self, target, kind}); };
			// There: indexed, not hidden, alive.
			const auto there = [&](ecs::Entity target) -> const gp::SpatialEntry * {
				if (target == ecs::Entity{})
					return nullptr;
				const gp::SpatialEntry *entry = env.spatial.Find(target);
				if (entry == nullptr || (entry->classes & gp::target_class::Hidden) != 0)
					return nullptr;
				if (const gp::Health *health = lookup.Get<gp::Health>(target); health != nullptr && gp::IsDead(*health))
					return nullptr;
				return entry;
			};
			// getAbleToAttackSpecificObject for a new target (possible, or after moving).
			const auto mayAttack = [&](const gp::SpatialEntry &entry) {
				return gp::TargetingSystem::Acceptable(env.relationships, entry, self, members[row].team, player, weapon, aggression);
			};
			const gp::Team *team = members[row].team < env.roster.TeamCount() ? &env.roster.TeamAt(members[row].team) : nullptr;
			// Team::getTeamTargetObject for a team that attacks together (not an aircraft).
			const auto teamVictim = [&]() -> ecs::Entity {
				if (team == nullptr || !team->attackCommonTarget)
					return {};
				const gp::SpatialEntry *entry = there(team->commonTarget);
				return entry != nullptr && (entry->classes & gp::target_class::Aircraft) == 0 ? team->commonTarget : ecs::Entity{};
			};
			// TunnelTracker::getCurNemesis: seen within 4 seconds, still there.
			const auto networkNemesis = [&]() -> ecs::Entity {
				if (player >= env.roster.PlayerCount())
					return {};
				const gp::Player &owner = env.roster.PlayerAt(player);
				if (owner.tunnelNemesis == ecs::Entity{} || owner.nemesisTick + 4 * 30 < tick || there(owner.tunnelNemesis) == nullptr)
					return {};
				return owner.tunnelNemesis;
			};
			// findBestTunnel: the player's tunnel nearest `from` (centre distance; the first of equals).
			const auto bestTunnel = [&](Engine::Math::FixedVector2 from) -> ecs::Entity {
				ecs::Entity best;
				Fixed bestDistance;
				for (const ecs::Entity tunnel : env.manifest.Network(player))
					if (const gp::Transform *where = lookup.Get<gp::Transform>(tunnel))
					{
						const Fixed distance = Engine::Math::DistanceSquared(where->position.XY(), from);
						if (best == ecs::Entity{} || distance < bestDistance)
						{
							best = tunnel;
							bestDistance = distance;
						}
					}
				return best;
			};
			// hasAttackedMeAndICanReturnFire: its last damage not yet looked at (now it is), by an enemy there it may attack.
			const auto struckBack = [&]() -> bool {
				const gp::Health *health = healths.empty() ? nullptr : &healths[row];
				if (health == nullptr || health->lastDamageTick <= guard.damageSeen)
					return false;
				guard.damageSeen = health->lastDamageTick;
				const gp::SpatialEntry *attacker = there(health->lastAttacker);
				return attacker != nullptr && env.relationships.Enemies(members[row].team, player, attacker->team, attacker->player) && mayAttack(*attacker);
			};
			const auto attackNemesis = [&] {
				attack = gp::AttackTarget{guard.nemesis, true};
				guard.giveUp = tick + env.rules.chaseTicks;
			};
			// lookForInnerTarget.
			const auto lookForInner = [&]() -> bool {
				if (const ecs::Entity victim = teamVictim(); victim != ecs::Entity{})
				{
					guard.nemesis = victim;
					return true;
				}
				if (const ecs::Entity nemesis = networkNemesis(); nemesis != ecs::Entity{})
				{
					guard.nemesis = nemesis;
					return true;
				}
				for (const ecs::Entity tunnel : env.manifest.Network(player))
				{
					if (const gp::AttackTarget *goal = lookup.Get<gp::AttackTarget>(tunnel))
						if (const gp::SpatialEntry *victim = there(goal->target); victim != nullptr && env.relationships.Enemies(members[row].team, player, victim->team, victim->player))
						{
							guard.nemesis = victim->entity;
							return true;
						}
					const gp::Health *body = lookup.Get<gp::Health>(tunnel);
					if (body == nullptr || body->lastDamageTick == 0 || body->lastDamageTick + env.rules.scanTicks <= tick)
						continue;
					const gp::SpatialEntry *attacker = there(body->lastAttacker);
					if (attacker == nullptr || !env.relationships.Enemies(members[row].team, player, attacker->team, attacker->player) || !mayAttack(*attacker))
						continue;
					guard.nemesis = attacker->entity;
					emit(Kind::TeamTarget, attacker->entity);
					emit(Kind::Nemesis, attacker->entity);
					return true;
				}
				return false;
			};
			// TunnelNetworkScan: the nearest enemy (centre distance) it may attack within its inner guard range
			// (getAdjustedVisionRangeForObject with the inner guard factor and its mood).
			const auto scan = [&]() -> ecs::Entity {
				const bool human = player < env.roster.PlayerCount() && env.roster.PlayerAt(player).human;
				Fixed range = aggression.vision * (human ? env.moods.guardInnerHuman : env.moods.guardInnerAi);
				if (!human)
					range = aggression.attitude == gp::attitude::Sleep ? Fixed{}
						: aggression.attitude == gp::attitude::Alert ? range * env.moods.alert
						: aggression.attitude == gp::attitude::Aggressive ? range * env.moods.aggressive
						: range;
				if (range <= Fixed{})
					return {};
				const gp::SpatialEntry *best = nullptr;
				Fixed bestDistance;
				env.spatial.ForEachWithin(at, range, [&](const gp::SpatialEntry &entry) {
					const Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), at);
					if (distance > range * range || !mayAttack(entry))
						return;
					if (best == nullptr || distance < bestDistance)
					{
						best = &entry;
						bestDistance = distance;
					}
				});
				return best != nullptr ? best->entity : ecs::Entity{};
			};
			const auto go = [&](State next) {
				if (guard.state == State::Inner || guard.state == State::Outer || guard.state == State::Aggressor)
					attack = {};
				if (guard.state == State::Aggressor)
					emit(Kind::ClearTeamTarget, {});
				guard.state = next;
				guard.entered = 0;
			};

			// The machine's transitions within the tick (a few at most: each state waits or attacks once entered).
			for (int step = 0; step < 8; ++step)
			{
				if (guard.entered != 0 && (guard.state == State::Return || guard.state == State::Inner) && struckBack())
				{
					go(State::Aggressor);
					continue;
				}
				if (guard.state == State::Return)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						if (inside)
						{
							go(State::Idle);
							continue;
						}
						if (const ecs::Entity victim = teamVictim(); victim != ecs::Entity{})
						{
							guard.nemesis = victim;
							go(State::Inner);
							continue;
						}
						const ecs::Entity tunnel = bestTunnel(at);
						if (tunnel == ecs::Entity{})
						{
							go(State::Inner);
							continue;
						}
						emit(Kind::Board, tunnel);
						guard.boardAsked = tick;
						break;
					}
					if (const ecs::Entity victim = teamVictim(); victim != ecs::Entity{})
					{
						guard.nemesis = victim;
						go(State::Inner);
						continue;
					}
					if (const ecs::Entity nemesis = networkNemesis(); nemesis != ecs::Entity{})
					{
						guard.nemesis = nemesis;
						go(State::Inner);
						continue;
					}
					if (inside || (boardings.empty() && guard.boardAsked < tick))
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
						auto random = Engine::Math::Stream(env.seed, {tick, self.index, self.generation, 0x7E61u});
						guard.nextScan = tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(env.rules.scanTicks)));
						break;
					}
					if (tick < guard.nextScan)
						break;
					guard.nextScan = tick + env.rules.scanTicks;
					if (lookForInner())
					{
						if (inside)
						{
							const gp::SpatialEntry *nemesis = there(guard.nemesis);
							const ecs::Entity exit = bestTunnel(nemesis != nullptr ? nemesis->position.XY() : at);
							if (exit != ecs::Entity{})
								emit(Kind::Exit, exit);
							break;
						}
						go(State::Inner);
						continue;
					}
					if (!inside && bestTunnel(at) != ecs::Entity{})
					{
						go(State::Return);
						continue;
					}
					break;
				}
				if (guard.state == State::Inner)
				{
					if (guard.entered == 0)
					{
						guard.entered = 1;
						guard.scanForEnemy = 1;
						if (there(guard.nemesis) == nullptr)
						{
							go(State::Outer);
							continue;
						}
						attackNemesis();
						break;
					}
					if (tick >= guard.giveUp)
					{
						go(State::Outer);
						continue;
					}
					const ecs::Entity victim = teamVictim();
					if (there(guard.nemesis) == nullptr)
					{
						if (victim != ecs::Entity{})
						{
							guard.nemesis = victim;
							attackNemesis();
							break;
						}
						if (const ecs::Entity nemesis = networkNemesis(); nemesis != ecs::Entity{})
						{
							guard.nemesis = nemesis;
							attackNemesis();
							break;
						}
						if (guard.scanForEnemy != 0)
						{
							guard.scanForEnemy = 0;
							if (const ecs::Entity found = scan(); found != ecs::Entity{})
							{
								guard.nemesis = found;
								attack = gp::AttackTarget{found, true};
								emit(Kind::Nemesis, found);
								break;
							}
						}
						if (there(attack.target) == nullptr)
						{
							go(State::Outer);
							continue;
						}
						break;
					}
					if (victim != ecs::Entity{} && victim != guard.nemesis)
					{
						emit(Kind::Nemesis, guard.nemesis);
						guard.nemesis = victim;
					}
					if (there(attack.target) == nullptr)
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
						if (guard.withoutPursuit != 0 || there(guard.nemesis) == nullptr)
						{
							go(State::Return);
							continue;
						}
						attackNemesis();
						break;
					}
					if (tick >= guard.giveUp)
					{
						go(State::Return);
						continue;
					}
					if (there(attack.target) == nullptr)
					{
						ecs::Entity goal = there(guard.nemesis) != nullptr ? guard.nemesis : ecs::Entity{};
						if (goal == ecs::Entity{} && team != nullptr && team->attackCommonTarget)
							goal = teamVictim();
						if (goal == ecs::Entity{})
						{
							go(State::Return);
							continue;
						}
						attack = gp::AttackTarget{goal, true};
					}
					break;
				}
				// Aggressor.
				if (guard.entered == 0)
				{
					guard.entered = 1;
					if (const gp::Health *health = healths.empty() ? nullptr : &healths[row]; health != nullptr && health->lastAttacker != ecs::Entity{})
						guard.nemesis = health->lastAttacker;
					if (there(guard.nemesis) == nullptr)
					{
						go(State::Return);
						continue;
					}
					emit(Kind::Nemesis, guard.nemesis);
					attackNemesis();
					break;
				}
				if (tick >= guard.giveUp || there(attack.target) == nullptr)
				{
					go(State::Return);
					continue;
				}
				if (armaments[row].firedTick == tick || armaments[row].firedTick + 1 == tick)
					emit(Kind::Nemesis, guard.nemesis);
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::TunnelGuardSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tunnel_guards";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::TargetingSystem>;
	using After = SystemTypeList<>;
};
}
