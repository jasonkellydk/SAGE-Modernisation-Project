export module engine.gameplay.common.healing.systems.healing_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.healing.systems.area_healing_system;
export import engine.gameplay.common.healing.systems.self_healing_system;

// Applies this tick's heal pulses, in parallel per chunk: each hurt, living
// target takes them in order up to its maximum; a pulse that locks is only
// accepted from the healer holding the target's lock until it runs out
// (heals from several healers do not stack, as the original's sole
// benefactor).
export namespace engine::gameplay
{
struct HealingSystem
{
	using Query = ecs::Query<ecs::Write<Health>, ecs::OptionalWrite<HealLock>>;
	using Resources = ecs::Resources<ecs::Read<HealPulses>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const HealPulses &pulses = context.Read<HealPulses>();
		if (pulses.All().empty())
			return;
		const std::uint64_t tick = context.Tick();
		auto healths = chunk.Get<Health>();
		auto locks = chunk.Get<HealLock>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			Health &health = healths[row];
			if (IsDead(health))
				continue;
			for (const HealPulse &pulse : pulses.For(entities[row]))
			{
				if (health.current >= health.maximum)
					break;
				if (pulse.lockTicks > 0 && !locks.empty())
				{
					HealLock &lock = locks[row];
					if (tick <= lock.until && lock.healer != pulse.healer)
						continue;
					lock = {pulse.healer, tick + pulse.lockTicks};
				}
				Heal(health, pulse.amount, tick);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HealingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.healing";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::AreaHealingSystem, engine::gameplay::SelfHealingSystem>;
};
}
