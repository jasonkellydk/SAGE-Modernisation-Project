export module engine.gameplay.common.fire.systems.flammability_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.fire.components.flammable;
export import engine.gameplay.common.fire.resources.fire_settings;
export import engine.gameplay.common.fire.resources.ignitions;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.health.definitions.armor;

// Catching fire and burning, in parallel per chunk, between this tick's
// impacts and the health pass: igniting damage (after armor) wears the
// flame limit down; things aflame burn on their timer, turn burned and go
// out; things spreading fire set alight what they offered to (at once,
// whatever its limit). Burning damage joins this tick's incoming damage once all chunks are
// done (on the caller, in chunk order).
export namespace engine::gameplay
{
struct BurnDamage : ecs::ChunkOutputs<DamageRecord>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::BurnDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.burn_damage";
};

}

export namespace engine::gameplay
{
struct FlammabilitySystem
{
	using Query = ecs::Query<ecs::Write<Flammable>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<FireSettings>, ecs::Read<ArmorCatalog>, ecs::Read<Ignitions>, ecs::Write<IncomingDamage>, ecs::Write<BurnDamage>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<BurnDamage>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const FireSettings &settings = context.Read<FireSettings>();
		const ArmorCatalog &armors = context.Read<ArmorCatalog>();
		const Ignitions &ignitions = context.Read<Ignitions>();
		// Only read here: records are added after the chunks.
		const IncomingDamage &incoming = context.Write<IncomingDamage>();
		auto &burns = context.Write<BurnDamage>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto flammables = chunk.Get<Flammable>();
		const auto healths = chunk.Get<Health>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < flammables.size(); ++row)
		{
			Flammable &fire = flammables[row];
			const Health &health = healths[row];
			if (IsDead(health))
				continue;
			const auto ignite = [&] {
				fire.state = FlameState::Aflame;
				fire.aflameEnd = tick + fire.aflameDuration;
				fire.burnedAt = fire.burnedDelay != 0 ? tick + fire.burnedDelay : 0;
				fire.nextBurn = fire.burnDelay != 0 ? tick + fire.burnDelay : 0;
			};
			// Set alight by a neighbour's spreading fire.
			if (ignitions.Count() != 0 && fire.state == FlameState::Normal && fire.burned == 0)
				if (const auto source = ignitions.For(entities[row]))
				{
					fire.source = *source;
					ignite();
				}
			// Igniting damage this tick.
			for (const DamageRecord &record : incoming.For(entities[row]))
			{
				if (!settings.Ignites(record.damageType))
					continue;
				if (tick > fire.lastFlameTick + fire.expiration)
					fire.remaining = fire.limit; // long since the last flame: the limit is back
				fire.lastFlameTick = tick;
				fire.source = record.source;
				if (fire.state != FlameState::Normal || fire.burned != 0)
					continue;
				fire.remaining -= AdjustDamage(armors.At(health.armor), record.damageType, record.amount);
				if (fire.remaining <= Engine::Math::Fixed{})
					ignite();
			}
			if (fire.state != FlameState::Aflame)
				continue;
			if (fire.nextBurn != 0 && tick >= fire.nextBurn)
			{
				fire.nextBurn = tick + fire.burnDelay;
				burns.push_back({entities[row], fire.source, fire.burnAmount, settings.burnDamageType, settings.burnDeathType});
			}
			if (fire.burnedAt != 0 && tick >= fire.burnedAt)
				fire.burned = 1;
			if (tick >= fire.aflameEnd)
				fire.state = fire.burned != 0 ? FlameState::Burned : FlameState::Normal;
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		const BurnDamage &burns = context.Write<BurnDamage>();
		if (burns.Size() == 0)
			return;
		burns.ForEach([&](const DamageRecord &record) { incoming.Add(record); });
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::FlammabilitySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.flammability";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the tick's impacts fill the incoming damage, before it is applied.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<>;
};
}
