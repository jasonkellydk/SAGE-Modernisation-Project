export module engine.gameplay.common.appearance.components.clip_playback;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export namespace engine::gameplay {
enum class ClipMode:std::uint32_t {Hold,Once,Loop,Target};
struct ClipPlayback {
    Engine::Math::Fixed frame{},frames_per_second{},target{},loop_period{};
    std::uint32_t clip{},frame_count{};
    ClipMode mode{ClipMode::Hold};std::uint32_t reserved{};
};
inline bool OnceClipComplete(const ClipPlayback& clip) noexcept {
    return clip.mode==ClipMode::Once && clip.frame_count &&
        clip.frame>=Engine::Math::Fixed::FromInt(clip.frame_count-1);
}
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::ClipPlayback> {
    static constexpr std::string_view StableName="engine.gameplay.clip_playback";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
