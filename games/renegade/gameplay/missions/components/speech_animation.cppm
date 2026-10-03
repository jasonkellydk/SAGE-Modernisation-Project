export module games.renegade.gameplay.missions.components.speech_animation;
import std;
export import engine.gameplay.common.appearance.components.clip_playback;
export import engine.ecs.core.entity;
export namespace renegade {
struct SpeechAnimation {
    ecs::Entity conversation;
    std::uint64_t started_tick{};
    std::uint32_t bank{},first_bone{},bone_count{},reserved{};
    engine::gameplay::ClipPlayback playback;
};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::SpeechAnimation> {
    static constexpr std::string_view StableName="renegade.speech_animation";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
