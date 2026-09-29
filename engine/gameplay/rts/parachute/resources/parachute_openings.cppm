export module engine.gameplay.rts.parachute.resources.parachute_openings;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Parachutes that opened this tick (the original plays its ParachuteOpenSound on the rider as it opens), for the
// presentation's sound.
export namespace engine::gameplay
{
struct ParachuteOpening
{
	ecs::Entity chute;
	ecs::Entity rider;
	Engine::Math::FixedVector3 position;
};

struct ParachuteOpenings
{
	std::vector<ParachuteOpening> opened;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ParachuteOpenings>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_openings";
};
}
