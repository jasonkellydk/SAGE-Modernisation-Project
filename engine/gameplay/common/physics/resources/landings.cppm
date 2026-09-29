export module engine.gameplay.common.physics.resources.landings;
import std;

export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Bodies with a bounce sound that landed this tick (PhysicsBehavior::update:
// airborne the step before, not now, not immune to falling), where they
// landed, for the presentation's sound (per chunk of the physics step).
export namespace engine::gameplay
{
struct Landing
{
	ecs::Entity entity;
	std::uint32_t sound{0};
	Engine::Math::FixedVector3 position;
};

struct Landings : ecs::ChunkOutputs<Landing>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Landings>
{
	static constexpr std::string_view StableName = "engine.gameplay.landings";
};
}
