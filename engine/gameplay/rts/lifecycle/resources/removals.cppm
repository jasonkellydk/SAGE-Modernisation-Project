export module engine.gameplay.rts.lifecycle.resources.removals;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// Entities whose time is up and whose books are already settled (their
// team, name and cargo were let go when they died): the removal system only
// destroys them. Written per chunk (e.g. by slow deaths finishing).
export namespace engine::gameplay
{
// Its own type (not an alias): a resource is found by its type.
struct Removals : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Removals>
{
	static constexpr std::string_view StableName = "engine.gameplay.removals";
};
}
