export module games.generalszh.gameplay.economy.systems.warehouse_crippling_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.economy.components.warehouse_crippling;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.docking.components.dock;

// SupplyWarehouseCripplingBehavior for every warehouse, after the tick's damage (one pass over the hits, then the
// warehouses by chunk):
// - onDamage (its health went down): healing suppressed for SelfHealSupression, the next heal then;
// - onBodyDamageStateChange: really damaged cripples its dock (no one is let in; SupplyWarehouseDockUpdate: whoever it
//   let in is dealt with, WarehouseCripplingEvents), leaving really damaged uncripples it;
// - update, on its heal tick: SelfHealAmount healed, the next heal SelfHealDelay on, asleep once whole.
export namespace generalszh::gameplay
{
struct WarehouseCripplingEvent
{
	ecs::Entity warehouse;
};

// The tick's crippled warehouses (a batch system's output: spent by ApplyWarehouseCrippling).
struct WarehouseCripplingEvents
{
	std::vector<WarehouseCripplingEvent> list;
};

// ActiveBody::calcDamageState with the game's UnitDamagedThreshold and UnitReallyDamagedThreshold.
inline std::uint8_t WarehouseBodyState(const engine::gameplay::Health &health, Engine::Math::Fixed damaged, Engine::Math::Fixed reallyDamaged) noexcept
{
	if (health.maximum <= Engine::Math::Fixed{})
		return health.current > Engine::Math::Fixed{} ? 0 : 3;
	const Engine::Math::Fixed ratio = health.current / health.maximum;
	return ratio > damaged ? 0 : ratio > reallyDamaged ? 1 : ratio > Engine::Math::Fixed{} ? 2 : 3;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::WarehouseCripplingEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.warehouse_crippling_events";
};
}

export namespace generalszh::gameplay
{
struct WarehouseCripplingSystem
{
	using Query = ecs::Query<ecs::Write<WarehouseCrippling>, ecs::Write<engine::gameplay::Health>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::OptionalWrite<engine::gameplay::Dock>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Hits>, ecs::Read<ObjectTemplates>, ecs::Write<WarehouseCripplingEvents>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto &gameData = templates.Content().gameData;
		auto &out = context.Write<WarehouseCripplingEvents>().list;
		const std::uint64_t now = context.Tick();
		const auto key = [](ecs::Entity entity) { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; };
		// Who lost health this tick.
		std::vector<std::uint64_t> hurt;
		context.Read<gp::Hits>().ForEach([&](const gp::Hit &hit) {
			if (!hit.handled && hit.amount > Engine::Math::Fixed{})
				hurt.push_back(key(hit.target));
		});
		std::ranges::sort(hurt);
		query.ForEachChunk([&](auto chunk) {
			auto states = chunk.template Get<WarehouseCrippling>();
			auto healths = chunk.template Get<gp::Health>();
			const auto refs = chunk.template Get<gp::DefinitionRef>();
			auto docks = chunk.template Get<gp::Dock>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < states.size(); ++row)
			{
				WarehouseCrippling &state = states[row];
				const WarehouseCripplingConfig *config = templates.WarehouseCripplingOf(refs[row].index);
				if (config == nullptr)
					continue;
				gp::Health &health = healths[row];
				if (std::ranges::binary_search(hurt, key(entities[row])))
				{
					state.suppressedUntil = now + config->suppressionTicks;
					state.nextHeal = state.suppressedUntil;
				}
				if (state.nextHeal != 0 && now >= state.nextHeal)
				{
					state.nextHeal = now + config->delayTicks;
					gp::Heal(health, config->amount, now);
					if (health.current == health.maximum)
						state.nextHeal = 0;
				}
				const std::uint8_t body = WarehouseBodyState(health, gameData.unitDamaged, gameData.unitReallyDamaged);
				if (body != state.bodyState)
				{
					const bool crippled = body == 2;
					if (!docks.empty() && (crippled || state.bodyState == 2))
					{
						docks[row].crippled = crippled;
						if (crippled)
							out.push_back({entities[row]});
					}
					state.bodyState = body;
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::WarehouseCripplingSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.warehouse_crippling";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::HealthSystem>;
};
}
