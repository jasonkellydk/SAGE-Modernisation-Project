export module games.renegade.gameplay.weapons.systems.weapon_control_system;
import std;
export import games.renegade.gameplay.weapons.systems.weapon_state_system;
import engine.gameplay.common.inventory.systems.inventory_selection_system;
import engine.gameplay.common.input.components.input_permission;
import engine.gameplay.common.health.components.health;
export namespace renegade {
struct WeaponControlSystem {
    using Query=ecs::Query<ecs::Read<engine::gameplay::InventoryItem>,ecs::Write<WeaponControl>>;
    using Lookup=ecs::Lookup<ecs::Read<WeaponInput>,ecs::Read<engine::gameplay::InventorySelection>,ecs::Read<engine::gameplay::InputPermission>,ecs::Read<engine::gameplay::Health>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto items=chunk.Get<engine::gameplay::InventoryItem>();const auto controls=chunk.Get<WeaponControl>();const auto entities=chunk.Entities();const auto owners=context.Lookup<Lookup>();
        for(std::size_t row=0;row<items.size();++row) {
            auto& control=controls[row];const auto owner=items[row].container;const auto* selection=owners.Get<engine::gameplay::InventorySelection>(owner);const auto* input=owners.Get<WeaponInput>(owner);
            control.active=selection && selection->selected==entities[row];control.primary=input && input->primary && control.active;control.secondary=input && input->secondary && control.active;control.reload=input && input->reload && control.active;
            const auto* permission=owners.Get<engine::gameplay::InputPermission>(owner);const auto* health=owners.Get<engine::gameplay::Health>(owner);
            control.permitted=input && input->permitted && (!permission || permission->enabled) && (!health || health->current>Engine::Math::Fixed{});
            if(!control.permitted) control.primary=control.secondary=control.reload=0;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::WeaponControlSystem> {
    static constexpr std::string_view StableName="renegade.weapon_control";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<renegade::WeaponStateSystem>;using After=SystemTypeList<>;
};
}
