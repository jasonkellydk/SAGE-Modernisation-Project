export module engine.gameplay.common.lifetime.systems.lifetime_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.common.lifetime.resources.expirations;

// Finds lives that ran out this tick, in parallel per chunk: to be killed
// or deleted; each is reported once (its Lifetime goes with the tick's
// commands).
export namespace engine::gameplay
{
struct LifetimeSystem
{
	using Query = ecs::Query<ecs::Read<Lifetime>>;
	using Resources = ecs::Resources<ecs::Write<Expirations>, ecs::Write<Deletions>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<Expirations>().Reset(query.PreparedChunkCount());
		context.Write<Deletions>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		Expirations &expired = context.Write<Expirations>();
		const std::uint64_t tick = context.Tick();
		const auto lifetimes = chunk.Get<Lifetime>();
		const auto entities = chunk.Entities();
		auto &out = expired.Slot(context);
		auto &gone = context.Write<Deletions>().Slot(context);
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < lifetimes.size(); ++row)
			if (tick >= lifetimes[row].expiresTick)
			{
				if (lifetimes[row].deletes != 0)
					gone.push_back(entities[row]);
				else
					out.push_back({entities[row], lifetimes[row].deathType});
				commands.Remove<Lifetime>(entities[row]);
			}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::LifetimeSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.lifetime";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
