export module engine.gameplay.rts.docking.resources.dock_repairs;
import std;

import engine.ecs.system.system;
export import engine.ecs.core.entity;

// This tick's repairs at repair docks, in docker order (RepairDockUpdate::action): the docker and the dock, for
// what else the action touches (the docker's drone). Per tick: not saved.
export namespace engine::gameplay
{
struct DockRepair
{
	ecs::Entity docker;
	ecs::Entity dock;
};

struct DockRepairs
{
	std::vector<DockRepair> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DockRepairs>
{
	static constexpr std::string_view StableName = "engine.gameplay.dock_repairs";
};
}
