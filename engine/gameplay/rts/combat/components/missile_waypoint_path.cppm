export module engine.gameplay.rts.combat.components.missile_waypoint_path;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// A guided missile flying a waypoint path (a script's NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH: its AI in
// AIFollowWaypointPathState): the waypoint it heads for and the one it came from (WaypointGraph indices), and its AI's
// goal position as MissileAIUpdate's lock check reads it (getGoalPosition: the state machine's, which the path state
// sets to its waypoint's location as each of its updates begins, so the check sees the waypoint the last update began
// with), with the one the next tick's check sees. It goes once the missile locks on (its AI moves to its original
// target instead) or the path ends. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct MissileWaypointPath
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	std::uint32_t waypoint{None};
	std::uint32_t prior{None};
	Engine::Math::FixedVector3 lockGoal;     // this tick's getGoalPosition
	Engine::Math::FixedVector3 nextLockGoal; // the next tick's
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::MissileWaypointPath>
{
	static constexpr std::string_view StableName = "engine.gameplay.missile_waypoint_path";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
