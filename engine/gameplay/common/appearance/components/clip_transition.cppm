export module engine.gameplay.common.appearance.components.clip_transition;
import std;
export import engine.gameplay.common.appearance.components.clip_playback;
export namespace engine::gameplay {
struct ClipTransition {ClipPlayback previous;Engine::Math::Fixed elapsed{},duration{};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::ClipTransition> {
    static constexpr std::string_view StableName="engine.gameplay.clip_transition";static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
