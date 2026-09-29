export module engine.gameplay.common.poison.systems.poison_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.poison.components.poison;
export import engine.gameplay.common.health.systems.health_system;

// PoisonedBehavior, once a tick before this tick's damage is applied, in
// entity order: last tick's hits of a poison's type (the damage it actually
// took) start or renew it (onDamage -> startPoisonedEffects: its end
// `duration` from the hit, its next hurt `interval` from the hit or sooner if
// one was already due); a rise in health since last tick is healing and ends
// it (onHealing); a hurt that is due is dealt, and the next set an interval on
// (update); past its end, it stops.
export namespace engine::gameplay
{
struct PoisonSystem
{
	using Query = ecs::Query<ecs::Write<Poison>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<Hits>, ecs::Write<IncomingDamage>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const std::uint64_t now = context.Tick();
		const std::uint64_t hitTick = now > 0 ? now - 1 : 0;
		std::vector<Hit> hits;
		context.Read<Hits>().ForEach([&](const Hit &hit) { hits.push_back(hit); });
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		bool added = false;
		query.ForEachChunk([&](auto chunk) {
			auto poisons = chunk.template Get<Poison>();
			const auto healths = chunk.template Get<Health>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < poisons.size(); ++row)
			{
				Poison &poison = poisons[row];
				const Health &health = healths[row];
				const auto stop = [&] {
					poison.nextDamageTick = 0;
					poison.stopTick = 0;
					poison.amount = {};
					poison.source = {};
				};
				if (poison.Active() && health.current > poison.seenHealth)
					stop();
				for (const Hit &hit : hits)
					if (hit.target == entities[row] && hit.damageType == poison.catchType)
					{
						poison.amount = hit.amount;
						poison.source = hit.source;
						poison.deathType = hit.deathType;
						poison.stopTick = hitTick + poison.duration;
						poison.nextDamageTick = poison.nextDamageTick != 0 ? std::min(poison.nextDamageTick, hitTick + poison.interval)
							: hitTick + poison.interval;
					}
				poison.seenHealth = health.current;
				if (!poison.Active() || IsDead(health))
					continue;
				if (poison.nextDamageTick != 0 && now >= poison.nextDamageTick)
				{
					DamageRecord record{entities[row], poison.source, poison.amount, poison.dealType, poison.deathType};
					record.fxType = poison.fxType;
					incoming.Add(record);
					added = true;
					poison.nextDamageTick = now + poison.interval;
				}
				if (now >= poison.stopTick)
					stop();
			}
		});
		if (added)
			incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PoisonSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.poison";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Its hurts join this tick's damage before it is applied.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<>;
};
}
