export module engine.gameplay.common.input.components.input_permission;
import std;
export import engine.ecs.core.component_registry;

export namespace engine::gameplay {
// Permission persists across input samples. Disabling input does not suspend
// physics, clocks, animation or the entity's lifetime.
struct InputPermission {std::uint32_t enabled{1};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::InputPermission> {
    static constexpr std::string_view StableName="engine.gameplay.input_permission";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
