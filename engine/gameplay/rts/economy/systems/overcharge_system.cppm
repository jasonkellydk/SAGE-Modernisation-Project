export module engine.gameplay.rts.economy.systems.overcharge_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.economy.algorithms.overcharge_switch;
export import engine.gameplay.rts.economy.resources.overcharge_events;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.resources.incoming_damage;

// OverchargeBehavior::update (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/OverchargeBehavior.cpp):
// each tick an overcharged producer hurts itself by its most health times its drain a second over the second's ticks
// ((getMaxHealth() * m_healthPercentToDrainPerSecond) / LOGICFRAMES_PER_SECOND: DAMAGE_PENALTY from itself,
// joining the tick's damage after the impacts), then (OverchargeLimitSystem, once the damage is taken) switches itself
// off if its health is below its floor.
export namespace engine::gameplay
{
struct OverchargeDrainSystem
{
	using Query = ecs::Query<ecs::Read<Overcharge>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<OverchargeSettings>, ecs::Write<IncomingDamage>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const OverchargeSettings &settings = context.Read<OverchargeSettings>();
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		const std::uint64_t tick = context.Tick();
		bool any = false;
		query.ForEachChunk([&](auto chunk) {
			const auto overcharges = chunk.template Get<Overcharge>();
			const auto healths = chunk.template Get<Health>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < overcharges.size(); ++row)
				if (overcharges[row].active != 0 && tick > overcharges[row].since)
				{
					incoming.Add({entities[row], entities[row], healths[row].maximum * overcharges[row].drain / Engine::Math::Fixed::FromInt(100 * static_cast<std::int32_t>(settings.ticksPerSecond)), settings.damageType, settings.deathType});
					any = true;
				}
		});
		if (any)
			incoming.Seal();
	}
};

struct OverchargeLimitSystem
{
	using Query = ecs::Query<ecs::Write<Overcharge>, ecs::Read<Health>, ecs::OptionalWrite<EnergySource>>;
	using Resources = ecs::Resources<ecs::Write<OverchargeEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<OverchargeEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &exhausted = context.Write<OverchargeEvents>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto overcharges = chunk.Get<Overcharge>();
		const auto healths = chunk.Get<Health>();
		auto energies = chunk.Get<EnergySource>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < overcharges.size(); ++row)
		{
			Overcharge &overcharge = overcharges[row];
			if (overcharge.active == 0 || tick <= overcharge.since)
				continue;
			const Health &health = healths[row];
			if (health.current < health.maximum * overcharge.floor)
			{
				SetOvercharge(overcharge, energies.empty() ? nullptr : &energies[row], false, tick);
				exhausted.push_back({entities[row]});
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::OverchargeDrainSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.overcharge_drain";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the tick's impacts, before the damage is taken.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::OverchargeLimitSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.overcharge_limit";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the damage is taken (HealthSystem).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::OverchargeDrainSystem>;
};
}
