export module engine.gameplay.rts.containment.resources.evacuations;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Containers emptied before this tick's deaths (Team::evacuateTeam: removeAllContained): whatever becomes of them,
// their riders get out where they are, unhurt by how they go. Filled and consumed within a tick.
export namespace engine::gameplay
{
struct Evacuations
{
	std::vector<ecs::Entity> containers;

	bool Contains(ecs::Entity container) const noexcept { return std::find(containers.begin(), containers.end(), container) != containers.end(); }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Evacuations>
{
	static constexpr std::string_view StableName = "engine.gameplay.evacuations";
};
}
