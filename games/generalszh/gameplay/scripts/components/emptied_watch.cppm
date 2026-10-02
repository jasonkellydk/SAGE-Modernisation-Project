export module games.generalszh.gameplay.scripts.components.emptied_watch;
import std;

export import engine.ecs.core.component_registry;

// What UNIT_EMPTIED last saw of a container (the original's ScriptConditions TransportStatus, one per object asked
// about): the tick it looked and how many were inside then. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct EmptiedWatch
{
	std::uint64_t tick{0};
	std::uint64_t count{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::EmptiedWatch>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.emptied_watch";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
