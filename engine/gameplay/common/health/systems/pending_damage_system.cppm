export module engine.gameplay.common.health.systems.pending_damage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.health.components.pending_damage;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.health.systems.health_system;

// Queued damage joins the tick's incoming damage (after the tick's impacts fill it, before it is applied), in entity
// order, and is spent.
export namespace engine::gameplay
{
struct PendingDamageSystem
{
	using Query = ecs::Query<ecs::Read<PendingDamage>>;
	using Resources = ecs::Resources<ecs::Write<IncomingDamage>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		auto &commands = context.Commands();
		std::vector<std::pair<ecs::Entity, PendingDamage>> pending;
		query.ForEachChunk([&](auto chunk) {
			const auto damage = chunk.template Get<PendingDamage>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < damage.size(); ++row)
				pending.emplace_back(entities[row], damage[row]);
		});
		if (pending.empty())
			return;
		std::sort(pending.begin(), pending.end(), [](const auto &a, const auto &b) { return a.first.index < b.first.index; });
		for (const auto &[entity, damage] : pending)
		{
			incoming.Add({entity, damage.source, damage.amount, damage.damageType, damage.deathType});
			commands.Remove<PendingDamage>(entity);
		}
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PendingDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.pending_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's impacts.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<>;
};
}
