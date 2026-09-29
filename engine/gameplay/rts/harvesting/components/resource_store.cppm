export module engine.gameplay.rts.harvesting.components.resource_store;
import std;

export import engine.ecs.core.component_registry;

// The two ends of a harvest round (both docks):
//   ResourceStore: boxes to be picked up one at a time (the original's
//   SupplyWarehouseDockUpdate: StartingBoxes, and whether it is deleted once
//   emptied); harvesters of any player not its enemy take from it; `listed`
//   when harvesters looking for one find it (SupplyWarehouseCreate: every
//   player's ResourceGatheringManager::addSupplyWarehouse), else only when
//   sent to it;
//   ResourceDepot: where its own player's harvesters deliver (the original's
//   SupplyCenterDockUpdate): every box is worth the harvest's box value to
//   the depot's player.
export namespace engine::gameplay
{
struct ResourceStore
{
	std::uint32_t boxes{0};
	std::uint32_t startingBoxes{0}; // what full is (for its looks)
	bool deleteWhenEmpty{false};
	bool listed{false};
	std::uint8_t reserved[6]{};
};

struct ResourceDepot
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ResourceStore>
{
	static constexpr std::string_view StableName = "engine.gameplay.resource_store";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ResourceStore &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.boxes} << 32) | value.startingBoxes);
		hasher.AppendU64((value.deleteWhenEmpty ? 1u : 0u) | (value.listed ? 2u : 0u));
	}
};

template<>
struct ComponentTraits<engine::gameplay::ResourceDepot>
{
	static constexpr std::string_view StableName = "engine.gameplay.resource_depot";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ResourceDepot &, StateHasher &hasher) noexcept { hasher.AppendU64(1); }
};
}
