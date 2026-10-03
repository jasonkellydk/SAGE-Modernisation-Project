export module games.renegade.gameplay.humans.components.animation;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

export namespace renegade {
struct HumanAnimation {std::uint32_t bank{},grounded{1},landing{},previous_facing{};};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::HumanAnimation> {
    static constexpr std::string_view StableName="renegade.human_animation";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
