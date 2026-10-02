export module engine.gameplay.common.lifetime.resources.expirations;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// Entities whose lives ran out this tick, per chunk (they are to be killed).
export namespace engine::gameplay
{
// Its own type (not an alias): a resource is found by its type.
struct Expiration
{
	ecs::Entity entity;
	std::uint32_t deathType{0};
};

struct Expirations : ecs::ChunkOutputs<Expiration>
{
};

// Entities whose time is up and that simply go (no death).
struct Deletions : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Expirations>
{
	static constexpr std::string_view StableName = "engine.gameplay.expirations";
};
template<>
struct ResourceTraits<engine::gameplay::Deletions>
{
	static constexpr std::string_view StableName = "engine.gameplay.deletions";
};
}
