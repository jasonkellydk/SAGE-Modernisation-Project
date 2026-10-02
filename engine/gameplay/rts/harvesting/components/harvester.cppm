export module engine.gameplay.rts.harvesting.components.harvester;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// A mover that ferries a resource in boxes from stores to its player's
// depots on its own (the original's SupplyTruckAIUpdate and its state
// machine): how many boxes it carries and may carry, how long each action
// at a store (per box) and a depot (all at once) takes, how far it looks
// for a store, the dock its player sent it to, and where it is in its
// round:
//   Busy: its player (or a script) has it doing something else;
//   Idle: nothing to do: it waits until told to harvest;
//   Wanting: looking for a depot (carrying) or a store (empty) to dock with;
//   Regrouping: none to be had: it waits at home (its player's preferred
//   kind of building) and looks again when there;
//   Docking: at a dock.
export namespace engine::gameplay
{
enum class HarvesterState : std::uint8_t
{
	Busy,
	Idle,
	Wanting,
	Regrouping,
	Docking,
};

struct Harvester
{
	std::uint32_t boxes{0};
	std::uint32_t maxBoxes{0};          // MaxBoxes
	std::uint64_t storeDelay{0};        // SupplyWarehouseActionDelay, ticks per box
	std::uint64_t depotDelay{0};        // SupplyCenterActionDelay, ticks for the delivery
	Engine::Math::Fixed scanDistance;   // SupplyWarehouseScanDistance (doubled for a computer player)
	ecs::Entity preferredDock;          // the dock its player last sent it to
	HarvesterState state{HarvesterState::Busy};
	bool entering{true};                // the state's start is still to run (a new harvester: its first state)
	bool forceWanting{false};           // told to go harvesting
	bool forceBusy{false};              // its player told it to stop
	bool directed{false};               // its last order came from its player (not its own round)
	std::uint8_t reserved[3]{};
	// UpgradedSupplyBoost: more money a full load brings once its player has `boostUpgrade` (a part load its share).
	static constexpr std::uint32_t NoUpgrade = 0xFFFFFFFFu;
	std::int64_t boost{0};
	std::uint32_t boostUpgrade{NoUpgrade};
	std::uint32_t reserved2{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Harvester>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvester";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Harvester &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.boxes} << 32) | value.maxBoxes);
		hasher.AppendU64(value.storeDelay);
		hasher.AppendU64(value.depotDelay);
		hasher.AppendU64(static_cast<std::uint64_t>(value.scanDistance.Raw()));
		hasher.AppendU64((std::uint64_t{value.preferredDock.index} << 32) | value.preferredDock.generation);
		hasher.AppendU64((static_cast<std::uint64_t>(value.state) << 8) | (value.entering ? 1u : 0u) | (value.forceWanting ? 2u : 0u) |
			(value.forceBusy ? 4u : 0u) | (value.directed ? 8u : 0u));
		hasher.AppendU64(static_cast<std::uint64_t>(value.boost));
		hasher.AppendU64(value.boostUpgrade);
	}
};
}
