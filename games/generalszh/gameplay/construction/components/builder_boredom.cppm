export module games.generalszh.gameplay.construction.components.builder_boredom;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// DozerPrimaryIdleState's boredom (DozerAIUpdate, WorkerAIUpdate): `since` the tick its idle stretch began (its AI last
// doing something, or its task ending); after more than BoredTime of it, and every BoredTime after, it looks within
// BoredRange for something of its player's to repair, else for a mine to clear. And whether it is on its player's idle
// worker list (DozerPrimaryIdleState's m_isMarkedAsIdle, InGameUI::addIdleWorker): `idleMarked` the tick it went on plus
// one (0: not on it); the list runs in that order.
export namespace generalszh::gameplay
{
struct BuilderBoredom
{
	std::uint64_t since{0};
	std::uint64_t boredTicks{0};
	Engine::Math::Fixed boredRange;
	std::uint64_t idleMarked{0};
};

// The builders bored this tick, for the game to find them work.
struct BoredBuilders : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::BuilderBoredom>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.builder_boredom";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::BoredBuilders>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bored_builders";
};
}
