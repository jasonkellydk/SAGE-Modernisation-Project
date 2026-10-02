export module engine.gameplay.rts.navigation.components.ignored_obstacle;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;

// The obstacle a mover's routes pass through (AIUpdateInterface::ignoreObstacle: a unit leaving the factory that made
// it walks out through its footprint): its cells count as open for this mover's route searches. Gone once it has
// reached where it was going (AIFollowWaypointPathState: "we have exited whatever object we are leaving") or takes
// another order. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct IgnoredObstacle
{
	ecs::Entity obstacle;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::IgnoredObstacle>
{
	static constexpr std::string_view StableName = "engine.gameplay.ignored_obstacle";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
