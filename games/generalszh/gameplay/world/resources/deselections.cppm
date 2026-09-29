export module games.generalszh.gameplay.world.resources.deselections;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Objects the logic took out of every player's selection this tick (the original's GameLogic::deselectObject with
// PLAYERMASK_ALL: a vehicle made unmanned by a pilot kill or a script), for the presentation to drop from the local
// selection. Per tick: not saved.
export namespace generalszh::gameplay
{
struct Deselections
{
	std::vector<ecs::Entity> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::Deselections>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.deselections";
};
}
