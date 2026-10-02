export module games.renegade.gameplay.defense.systems.defense_system;
import std;
export import engine.ecs.system.system;
export import games.renegade.gameplay.defense.algorithms.apply_damage;

export namespace renegade
{
struct DefenseSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::Health>, ecs::Write<engine::gameplay::Shield>, ecs::Read<Defense>>;
	using Resources = ecs::Resources<ecs::Read<DamageRules>, ecs::Read<DamageRequests>, ecs::Write<DefenseHits>>;
	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<DefenseHits>().Reset(query.PreparedChunkCount());
	}
	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto health = chunk.Get<engine::gameplay::Health>();
		auto shield = chunk.Get<engine::gameplay::Shield>();
		const auto defenses = chunk.Get<Defense>();
		const auto entities = chunk.Entities();
		auto &output = context.Write<DefenseHits>().Slot(context);
		for (std::size_t row = 0; row < health.size(); ++row)
			for (const auto &hit : context.Read<DamageRequests>().For(entities[row]))
			{
				auto result = ApplyDamage(health[row], shield[row], defenses[row], hit, context.Read<DamageRules>());
				if (result.healthDamage > Fixed{})
				{
					health[row].lastAttacker = hit.source;
					health[row].lastDamageTick = context.Time().Tick();
					health[row].lastDamageType = hit.warhead;
				}
				output.push_back(result);
			}
	}
};
}
export namespace ecs
{
template<> struct SystemTraits<renegade::DefenseSystem>
{
	static constexpr std::string_view StableName = "renegade.defense";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
