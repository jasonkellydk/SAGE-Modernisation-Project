export module games.renegade.gameplay.missions.resources.speech_animations;
import std;
export import games.renegade.gameplay.missions.components.speech_animation;
export import engine.ecs.system.system;
export namespace renegade {
struct SpeechClip {std::uint32_t speech{};engine::gameplay::ClipPlayback playback;Engine::Math::Fixed duration;};
struct SpeechAnimations {std::vector<std::vector<SpeechClip>> banks;};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::SpeechAnimations> {static constexpr std::string_view StableName="renegade.speech_animations";};
}
