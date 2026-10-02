export module engine.gameplay.rts.movement.components.path_completed;
import std;

export import engine.ecs.core.component_registry;

// The last waypoint of the path it last finished and the tick it got there (AIUpdateInterface::m_completedWaypoint,
// which the original clears at the start of its next AI update: the scripts of the next tick see it). Simulation state:
// checkpointed.
export namespace engine::gameplay
{
struct PathCompleted
{
	std::uint32_t waypoint{0xFFFFFFFFu};
	std::uint32_t reserved{0};
	std::uint64_t tick{0};

	// Seen by the scripts of tick `now` (they run before its systems): finished the tick before.
	bool SeenAt(std::uint64_t now) const noexcept { return tick != 0 && tick + 1 == now; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PathCompleted>
{
	static constexpr std::string_view StableName = "engine.gameplay.path_completed";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
