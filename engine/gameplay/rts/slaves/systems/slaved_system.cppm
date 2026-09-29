export module engine.gameplay.rts.slaves.systems.slaved_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.rts.slaves.components.slaved;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.slaves.algorithms.enslave;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import Engine.Core.Math.FixedRandom;

// SlavedUpdate::update without repairing (drones): every SlavedUpdateTicks a
// slave looks at its master. A master gone, dead or unmanned leaves it
// DISABLED_UNMANNED and idle: its AI stops, so its locomotor no longer holds it
// and physics brings it down, going on at the speed it flew (the original's
// physics already had it), to be killed at rest on the ground. Else, by priority:
// with an AttackRange it heads for its master's victim, or as near as that
// range from the master allows, wandering by AttackWanderRange; with a
// ScoutRange it heads for its master's move destination while the master is
// more than half its GuardMaxRange from it (ScoutRange, ScoutWanderRange
// alike); else it guards: idle and more than 15 from its guard point (the
// master plus its offset), or anywhere more than twice GuardMaxRange from
// its master, it picks a new offset (GuardMaxRange in a random direction,
// with a GuardWanderRange) and heads for the point. Moves go through the
// tick's commands. Distances as the original's partition manager: to the
// victim and destinations from the slave's bounding circle, the master's
// distance to its destination from the master's (FROM_BOUNDINGSPHERE_2D: the
// circles' radii off the centre distance, never below zero); to the guard
// point (at the master's height) and to the master centre to centre in 3D.
// Spotting (drones): each look first clears the master's spotting bonus; heading
// for the victim from within DistToTargetToGrantRangeBonus of it (measured
// before it moves) sets it again. Every change re-times the master's weapons
// (WeaponBonusRetimeSystem), as the original's did.
export namespace engine::gameplay
{
namespace slaved_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

// distCalcProc_BoundaryAndBoundary_2D against a point: the centre distance less `radius`, never below zero.
inline Fixed BoundaryDistance(FixedVector2 from, Fixed radius, FixedVector2 to)
{
	const Fixed distance = Engine::Math::Length(to - from) - radius;
	return distance > Fixed{} ? distance : Fixed{};
}

// doAttackLogic / doScoutLogic: the goal, or as near as `range` from the master allows, then wandering.
inline FixedVector2 Approach(Slaved &slave, FixedVector2 me, FixedVector2 master, FixedVector2 goal, Fixed range, Fixed wander, Engine::Math::RandomStream &random)
{
	FixedVector2 position = goal;
	if (BoundaryDistance(me, slave.radius, goal) > range)
	{
		const FixedVector2 toward = goal - master;
		const Fixed length = Engine::Math::Length(toward);
		position = length > Fixed{} ? master + toward * (range / length) : master;
	}
	if (wander > Fixed{})
	{
		slave.guardOffset = RandomReach(random, wander);
		position = position + slave.guardOffset;
	}
	return position;
}
}

struct SlavedSystem
{
	using Query = ecs::Query<ecs::Write<Slaved>, ecs::Read<Transform>, ecs::Optional<MoveOrder>, ecs::Optional<Locomotion>, ecs::Optional<PhysicsBody>, ecs::Optional<Disabled>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<Health>, ecs::Read<AttackTarget>, ecs::Read<MoveOrder>, ecs::Read<Disabled>,
		ecs::Read<WeaponBonusConditions>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace slaved_detail;
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x51A7u;
		// Masters' conditions as this tick's looks leave them, in order (committed after the pass).
		std::vector<std::pair<ecs::Entity, WeaponBonusConditions>> masters;
		const auto spotting = [&](ecs::Entity master, std::uint32_t bits, bool on) {
			if (bits == 0)
				return;
			auto found = std::find_if(masters.begin(), masters.end(), [&](const auto &entry) { return entry.first == master; });
			if (found == masters.end())
			{
				const WeaponBonusConditions *now = lookup.Get<WeaponBonusConditions>(master);
				if (now == nullptr)
					return;
				masters.emplace_back(master, *now);
				found = masters.end() - 1;
			}
			SetWeaponBonus(found->second, bits, on, tick);
		};
		query.ForEachChunk([&](auto chunk) {
			auto slaves = chunk.template Get<Slaved>();
			const auto transforms = chunk.template Get<Transform>();
			const auto orders = chunk.template Get<MoveOrder>();
			const auto locomotions = chunk.template Get<Locomotion>();
			const auto bodies = chunk.template Get<PhysicsBody>();
			const auto entities = chunk.Entities();
			const auto disabledRows = chunk.template Get<Disabled>();
			for (std::size_t row = 0; row < slaves.size(); ++row)
			{
				if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::None))
					continue;
				Slaved &slave = slaves[row];
				if (slave.waitTicks > 0 && --slave.waitTicks > 0)
					continue;
				slave.waitTicks = SlavedUpdateTicks;
				if (slave.enslaved == 0 || orders.empty())
					continue;
				const Transform *master = lookup.IsAlive(slave.master) ? lookup.Get<Transform>(slave.master) : nullptr;
				const Health *masterHealth = master != nullptr ? lookup.Get<Health>(slave.master) : nullptr;
				const Disabled *masterOff = master != nullptr ? lookup.Get<Disabled>(slave.master) : nullptr;
				if (master == nullptr || (masterHealth != nullptr && IsDead(*masterHealth)) ||
					(masterOff != nullptr && (masterOff->mask & disabled_type::Unmanned) != 0))
				{
					// stopSlavedEffects; setDisabled(DISABLED_UNMANNED); aiIdle.
					slave.master = {};
					slave.enslaved = 0;
					const Disabled *mine = lookup.Get<Disabled>(entities[row]);
					if (mine != nullptr)
						commands.Set<Disabled>(entities[row], Disabled{mine->mask | disabled_type::Unmanned});
					else
						commands.Add<Disabled>(entities[row], Disabled{disabled_type::Unmanned});
					commands.Set<MoveOrder>(entities[row], MoveOrder{});
					if (!bodies.empty() && !locomotions.empty())
					{
						PhysicsBody body = bodies[row];
						const auto heading = Engine::Math::Direction(transforms[row].facing);
						const Fixed speed = locomotions[row].speed;
						body.velocity = {heading.x * speed, heading.y * speed, body.velocity.z};
						commands.Set<PhysicsBody>(entities[row], body);
					}
					continue;
				}
				// Clear the drone spotting bonus; up to the drone to earn it again this look.
				spotting(slave.master, slave.definition.spottingBonus, false);
				auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
				const FixedVector2 me = transforms[row].position.XY();
				const FixedVector2 home = master->position.XY();
				const SlavedDefinition &d = slave.definition;
				// 2ND PRIORITY (after repairing): the master's victim.
				if (d.attackRange > Fixed{})
					if (const AttackTarget *attack = lookup.Get<AttackTarget>(slave.master); attack != nullptr && lookup.IsAlive(attack->target))
						if (const Transform *victim = lookup.Get<Transform>(attack->target))
						{
							const Fixed away = BoundaryDistance(me, slave.radius, victim->position.XY());
							commands.Set<MoveOrder>(entities[row], MoveToPoint(Approach(slave, me, home, victim->position.XY(), d.attackRange, d.attackWanderRange, random)));
							if (away < d.spottingRange)
								spotting(slave.master, slave.definition.spottingBonus, true);
							continue;
						}
				// 3RD: scout ahead of a master on the move.
				if (d.scoutRange > Fixed{})
					if (const MoveOrder *going = lookup.Get<MoveOrder>(slave.master); going != nullptr && going->mode == MoveMode::Point)
					{
						const Fixed half = d.guardMaxRange / Fixed::FromInt(2);
						if (BoundaryDistance(home, slave.masterRadius, going->destination) > half)
						{
							commands.Set<MoveOrder>(entities[row], MoveToPoint(Approach(slave, me, home, going->destination, d.scoutRange, d.scoutWanderRange, random)));
							continue;
						}
					}
				// Guard the master's area.
				if (d.guardMaxRange <= Fixed{})
					continue;
				FixedVector2 pinned = home + slave.guardOffset;
				const FixedVector3 at = transforms[row].position;
				const FixedVector3 pinned3{pinned.x, pinned.y, master->position.z}; // the master's height (its position plus the offset)
				const bool idle = orders[row].mode == MoveMode::Idle;
				const Fixed stray = d.guardMaxRange * Fixed::FromInt(2);
				if ((idle && Engine::Math::LengthSquared(at - pinned3) > Fixed::FromInt(15 * 15)) ||
					Engine::Math::LengthSquared(at - master->position) > stray * stray)
				{
					if (d.guardWanderRange > Fixed{})
					{
						// doGuardLogic: a new offset of GuardMaxRange (as the original), on top of the old pinned point.
						slave.guardOffset = RandomReach(random, d.guardMaxRange);
						pinned = pinned + slave.guardOffset;
					}
					commands.Set<MoveOrder>(entities[row], MoveToPoint(pinned));
				}
			}
		});
		for (const auto &[master, conditions] : masters)
			commands.Set<WeaponBonusConditions>(master, conditions);
	}
};

}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SlavedSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.slaved";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
