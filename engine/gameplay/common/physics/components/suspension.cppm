export module engine.gameplay.common.physics.components.suspension;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedAffineTransform3;

export namespace engine::gameplay {
struct Suspension {
    ecs::Entity parent;Engine::Math::FixedAffineTransform3 socket;
    Engine::Math::Fixed travel{},radius{},translation_scale{Engine::Math::Fixed::One()};
    std::uint32_t position_bone{},rotation_bone{~0u},categories{},reserved{};
};
struct SuspensionState {
    Engine::Math::FixedVector3 last_contact;
    Engine::Math::Fixed displacement{},rotation{};
    std::uint32_t initialized{},contact{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::Suspension> {
    static constexpr std::string_view StableName="engine.gameplay.suspension";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::SuspensionState> {
    static constexpr std::string_view StableName="engine.gameplay.suspension_state";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
