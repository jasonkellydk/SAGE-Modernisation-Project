export module engine.gameplay.common.spatial.components.object_shroud;
import std;

export import engine.ecs.core.component_registry;

// How each player sees an object through the shroud (the original's Object::getShroudedStatus, a bit per player):
// clear (every partition cell it touches is clear), partly clear (some are), fogged (none clear, some fogged). A player
// in none of the three sees it shrouded. Refreshed before each step's spatial index from the shroud; everyone sees
// it clearly until then. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct ObjectShroud
{
	std::uint64_t clear{~std::uint64_t{0}};
	std::uint64_t partial{0};
	std::uint64_t fogged{0};

	bool ClearTo(std::uint32_t player) const noexcept { return player < 64 && (clear & (std::uint64_t{1} << player)) != 0; }
	// OBJECTSHROUD_PARTIAL_CLEAR or better: shown (on the radar, as a target).
	bool SeenBy(std::uint32_t player) const noexcept { return player < 64 && ((clear | partial) & (std::uint64_t{1} << player)) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ObjectShroud>
{
	static constexpr std::string_view StableName = "engine.gameplay.object_shroud";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
