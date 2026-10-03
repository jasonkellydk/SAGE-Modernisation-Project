export module engine.gameplay.common.appearance.components.pose_binding;
import std;
export import engine.ecs.core.component_registry;
export namespace engine::gameplay {
// Index into an immutable presentation pose/animation definition library.
// Mutable playheads and transitions remain separate archetype columns.
struct PoseBinding {std::uint32_t definition{},reserved{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::PoseBinding> {
    static constexpr std::string_view StableName="engine.gameplay.pose_binding";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
