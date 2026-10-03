export module engine.gameplay.common.inventory.components.inventory;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

export namespace engine::gameplay {
// Each item is an archetype entity. A bag can grow without embedding a vector
// or a game-dependent maximum item count in an owner's component column.
struct InventoryItem {
    ecs::Entity container;
    std::uint32_t definition{},group{},order{},available{1};
};
struct InventorySelection {ecs::Entity selected;};
enum class InventoryOperation : std::uint32_t {None,Clear,Next,Previous,Group,Definition};
struct InventoryControl {
    InventoryOperation operation{InventoryOperation::None};
    std::uint32_t value{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::InventoryItem> {
    static constexpr std::string_view StableName="engine.gameplay.inventory_item";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::InventorySelection> {
    static constexpr std::string_view StableName="engine.gameplay.inventory_selection";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::InventoryControl> {
    static constexpr std::string_view StableName="engine.gameplay.inventory_control";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
