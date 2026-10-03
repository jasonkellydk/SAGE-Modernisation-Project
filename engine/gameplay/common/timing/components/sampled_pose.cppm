export module engine.gameplay.common.timing.components.sampled_pose;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

export namespace engine::gameplay {
struct SampledPose {ecs::Entity clock;std::uint32_t track{},reserved{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SampledPose> {
    static constexpr std::string_view StableName="engine.gameplay.sampled_pose";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
