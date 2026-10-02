export module engine.gameplay.rts.combat.resources.assists;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// This tick's assists (who asked, who joined in, at whom), for the presentation's data streams.
export namespace engine::gameplay
{
struct Assist
{
	ecs::Entity requester;
	ecs::Entity assister;
	ecs::Entity victim;
};

struct Assists
{
	std::vector<Assist> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Assists>
{
	static constexpr std::string_view StableName = "engine.gameplay.assists";
};
}
