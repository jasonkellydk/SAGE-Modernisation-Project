export module engine.gameplay.common.physics.components.kinematic_collider;
import std;
export import engine.ecs.core.component_registry;
export namespace engine::gameplay {
// Definition indexes immutable geometry; subject is the content identity
// returned by shared collision queries. Each instance is an archetype row.
struct KinematicCollider {std::uint64_t subject{};std::uint32_t definition{},categories{0xffffffffu},enabled{1},reserved{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::KinematicCollider> {
    static constexpr std::string_view StableName="engine.gameplay.kinematic_collider";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
