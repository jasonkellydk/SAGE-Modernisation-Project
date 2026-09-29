export module engine.gameplay.common.healing.systems.self_healing_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.common.healing.components.healing;
export import engine.gameplay.common.health.systems.health_system;

// Heals entities that heal themselves, in parallel per chunk: while hurt and
// alive, `amount` every `delay` ticks, waiting `startDelay` after each hit
// (nor while it waits: a structure's base regeneration while it is not standing whole).
export namespace engine::gameplay
{
struct SelfHealingSystem
{
	using Query = ecs::Query<ecs::Write<Health>, ecs::Write<SelfHealing>, ecs::Optional<Disabled>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		auto healths = chunk.Get<Health>();
		auto healing = chunk.Get<SelfHealing>();
		const auto disabledRows = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			SelfHealing &self = healing[row];
			if (self.waiting != 0)
				continue;
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], self.runsWhileDisabled != 0 ? self.runsWhileDisabled : disabled_type::Held))
				continue;
			Health &health = healths[row];
			if (IsDead(health) || health.current >= health.maximum || tick < self.nextTick)
				continue;
			if (self.startDelay > 0 && health.lastDamageTick != 0 && tick < health.lastDamageTick + self.startDelay)
				continue;
			Heal(health, self.amount, tick);
			self.nextTick = tick + self.delay;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SelfHealingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.self_healing";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's damage: the dead do not heal.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::HealthSystem>;
};
}
