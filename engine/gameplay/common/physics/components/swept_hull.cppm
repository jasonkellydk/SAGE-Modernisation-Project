export module engine.gameplay.common.physics.components.swept_hull;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

export namespace engine::gameplay {
// Axis-aligned collision proxy relative to an entity's Transform. Games
// compose authored dimensions, categories and contact response parameters.
struct SweptHull {
    Engine::Math::FixedVector3 offset,extent;
    Engine::Math::Fixed clip_bias{Engine::Math::Fixed::One()};
    std::uint64_t ignored_subject{};
    std::uint32_t categories{0xffffffffu},maximum_contacts{8};
    Engine::Math::Fixed support_normal_z{},support_clearance{},blocking_clearance{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SweptHull> {
    static constexpr std::string_view StableName="engine.gameplay.swept_hull";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
