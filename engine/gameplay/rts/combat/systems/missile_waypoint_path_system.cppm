export module engine.gameplay.rts.combat.systems.missile_waypoint_path_system;
import std;

export import engine.gameplay.rts.combat.components.missile_waypoint_path;
export import engine.gameplay.rts.combat.systems.missile_flight_system;
export import engine.gameplay.rts.navigation.algorithms.waypoint_steps;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// Guided missiles flying a waypoint path, chunk-parallel, a tick each before they fly: their AI's
// AIFollowWaypointPathState::update as AIUpdateInterface::update runs it after MissileAIUpdate's own state logic.
//   A missile whose state logic locks on this tick (ATTACK within half its DistanceToTargetForLock of its AI's goal
//   position, 2D) has its AI moved to its original target instead: the path state never updates again (the flight
//   system takes the path away).
//   Else its AI's goal position becomes its waypoint's location (seen by the next tick's lock check), and once it is
//   within its locomotor's CloseEnoughDist of its leg's end (3D, a projectile's AIInternalMoveToState) it takes the
//   next waypoint (at random among the links, never straight back): its goal that waypoint on the ground, and on the
//   last leg (no next one) it flies to its precise height. No next one: the path is over.
export namespace engine::gameplay
{
struct MissileWaypointPathSystem
{
	using Query = ecs::Query<ecs::Write<MissileWaypointPath>, ecs::Write<MissileFlight>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<WaypointGraph>, ecs::Read<GroundHeight>, ecs::Read<WeaponCatalog>, ecs::Read<RandomSeed>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const WaypointGraph &graph = context.Read<WaypointGraph>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x3A7Bu;
		auto paths = chunk.Get<MissileWaypointPath>();
		auto missiles = chunk.Get<MissileFlight>();
		const auto transforms = chunk.Get<Transform>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < paths.size(); ++row)
		{
			MissileWaypointPath &path = paths[row];
			MissileFlight &m = missiles[row];
			if (m.state == MissileState::Kill || m.state == MissileState::KillSelf || path.waypoint == MissileWaypointPath::None)
				continue;
			const MissileFlightDefinition &d = weapons.At(m.shot.weapon).missile;
			const Engine::Math::FixedVector3 position = transforms[row].position;
			path.lockGoal = path.nextLockGoal;
			// MissileAIUpdate::doAttackState's lock this tick (not when it detonates for want of fuel first): no path
			// update.
			if ((m.state == MissileState::AttackNoTurn || m.state == MissileState::Attack) && !(tick >= m.fuelTick && d.detonateOnNoFuel) &&
				d.lockDistance > Fixed{})
			{
				const Fixed lock = m.tracking ? d.lockDistance : d.lockDistance / Fixed::FromInt(2);
				if (Engine::Math::DistanceSquared(position.XY(), path.lockGoal.XY()) < lock * lock)
					continue;
			}
			path.nextLockGoal = graph.Position(path.waypoint);
			if (Engine::Math::DistanceSquared(position, m.goal) >= d.closeEnough * d.closeEnough)
				continue;
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			const std::uint32_t next = NextWaypoint(graph, path.waypoint, path.prior, random);
			path.prior = path.waypoint;
			path.waypoint = next;
			if (next == MissileWaypointPath::None)
			{
				context.Commands().Remove<MissileWaypointPath>(entities[row]);
				continue;
			}
			m.goal = MissileWaypointGoal(graph, ground, next);
			if (!HasNextWaypoint(graph, next, path.prior))
				m.preciseZ = true;
		}
	}

	// AIFollowWaypointPathState::computeGoal for a missile: the waypoint, on the ground (waypoints are always on the
	// ground).
	static Engine::Math::FixedVector3 MissileWaypointGoal(const WaypointGraph &graph, const GroundHeight &ground, std::uint32_t waypoint)
	{
		const Engine::Math::FixedVector3 at = graph.Position(waypoint);
		return {at.x, at.y, ground.At(at.XY())};
	}
};

// ScriptActions::doNamedFireWeaponFollowingWaypointPath once its projectile is out: the missile's AI follows the path
// from `waypoint` (aiFollowWaypointPath, AIFollowWaypointPathState::onEnter): its AI's goal position that waypoint's
// location, its goal the waypoint on the ground, on its precise height if that is the last leg already.
inline MissileWaypointPath FollowWaypointPath(MissileFlight &flight, const WaypointGraph &graph, const GroundHeight &ground, std::uint32_t waypoint)
{
	MissileWaypointPath path;
	path.waypoint = waypoint;
	path.lockGoal = graph.Position(waypoint);
	path.nextLockGoal = path.lockGoal;
	flight.goal = MissileWaypointPathSystem::MissileWaypointGoal(graph, ground, waypoint);
	if (!HasNextWaypoint(graph, waypoint, MissileWaypointPath::None))
		flight.preciseZ = true;
	return path;
}
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MissileWaypointPathSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_waypoint_path";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Its AI's update follows the missile's state logic within the same object update; the flight reads what it set.
	using Before = SystemTypeList<engine::gameplay::MissileFlightSystem>;
	// After the tick's shots and the lobbed projectiles' flight, as the missiles' own flight.
	using After = SystemTypeList<engine::gameplay::WeaponSystem, engine::gameplay::ProjectileFlightSystem>;
};
}
