export module games.generalszh.gameplay.production.resources.production_notices;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// What production brought out this tick, for the presentation (ProductionUpdate: the voice of the first of each
// production made, VoiceCreate): the first unit of each, in the order they came. Per tick: not saved.
export namespace generalszh::gameplay
{
struct ProductionNotices
{
	std::vector<ecs::Entity> created;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::ProductionNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.production_notices";
};
}
