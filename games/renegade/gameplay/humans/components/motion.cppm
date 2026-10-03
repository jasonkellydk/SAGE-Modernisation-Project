export module games.renegade.gameplay.humans.components.motion;
import std;
export import engine.ecs.core.component_registry;
export import games.renegade.content.characters.movement_definition;

export namespace renegade {
struct HumanMovement {content::HumanMovementDefinition definition;};
struct HumanState {
    Engine::Math::FixedVector3 velocity,ground_normal;
    std::uint32_t grounded{},just_jumped{};
    std::uint32_t ladder{},ladder_up_mask{},ladder_down_mask{},state_locked{};
    // Zero means ordinary movement; otherwise original transition style+1.
    // Completion is driven by the shared once playhead, never by a wall timer.
    std::uint32_t transition{},transition_sequence{};
};
struct HumanControl {
    Engine::Math::Fixed forward{},left{};
    Engine::Math::TurnAngle facing;
    std::uint32_t jump{},enabled{1},action{};
};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::HumanMovement> {
    static constexpr std::string_view StableName="renegade.human_movement";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<renegade::HumanState> {
    static constexpr std::string_view StableName="renegade.human_state";
    static constexpr std::uint32_t Version=3;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<renegade::HumanControl> {
    static constexpr std::string_view StableName="renegade.human_control";
    static constexpr std::uint32_t Version=2;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
