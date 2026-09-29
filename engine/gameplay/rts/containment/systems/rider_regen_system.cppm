export module engine.gameplay.rts.containment.systems.rider_regen_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.common.health.components.health;

// TransportContain::update with HealthRegen%PerSec, each tick, chunk-parallel over those inside: a rider short of its
// maximum regains its carrier's share of it (attemptHealing: up to its maximum, never the dead).
export namespace engine::gameplay
{
struct RiderRegenSystem
{
	using Query = ecs::Query<ecs::Write<Health>, ecs::Read<Passenger>>;
	using Lookup = ecs::Lookup<ecs::Read<Transport>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto healths = chunk.Get<Health>();
		const auto seats = chunk.Get<Passenger>();
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			const Transport *carrier = lookup.Get<Transport>(seats[row].transport);
			if (carrier == nullptr || carrier->definition.riderRegen <= Engine::Math::Fixed{})
				continue;
			Health &health = healths[row];
			if (IsDead(health) || health.current >= health.maximum)
				continue;
			const Engine::Math::Fixed regen = health.maximum * carrier->definition.riderRegen;
			Heal(health, regen, context.Tick());
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::RiderRegenSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.rider_regen";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it among the tick's other healing.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
