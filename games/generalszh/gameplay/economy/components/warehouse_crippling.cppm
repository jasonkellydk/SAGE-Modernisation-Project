export module games.generalszh.gameplay.economy.components.warehouse_crippling;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A supply warehouse that is crippled while really damaged and mends itself (SupplyWarehouseCripplingBehavior): the tick
// its self-healing may start again after damage (m_healingSupressedUntilFrame), the tick of its next heal (m_nextHealingFrame;
// 0: asleep, as it starts and once whole), and its body damage state as last seen (0 pristine, 1 damaged, 2 really
// damaged, 3 rubble). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct WarehouseCrippling
{
	std::uint64_t suppressedUntil{0};
	std::uint64_t nextHeal{0};
	std::uint8_t bodyState{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

// Each definition's SupplyWarehouseCripplingBehavior (present or not): SelfHealSupression and SelfHealDelay (ticks,
// rounded up) and SelfHealAmount.
struct WarehouseCripplingConfig
{
	bool present{false};
	std::uint32_t suppressionTicks{0};
	std::uint32_t delayTicks{0};
	Engine::Math::Fixed amount;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::WarehouseCrippling>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.warehouse_crippling";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
