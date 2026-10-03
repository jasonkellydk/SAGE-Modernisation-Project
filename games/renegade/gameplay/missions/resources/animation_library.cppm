export module games.renegade.gameplay.missions.resources.animation_library;
import std;
export import engine.gameplay.common.appearance.resources.clip_requests;
import engine.ecs.core.resource_store;
export namespace renegade {
struct MissionAnimationBinding {
    ecs::Entity subject;std::string name;engine::gameplay::ClipPlayback playback;
};
// Content names and command schemas belong to game composition; the engine
// receives typed requests and immutable model-space collision only.
struct MissionAnimationLibrary {std::vector<MissionAnimationBinding> bindings;};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::MissionAnimationLibrary> {
    static constexpr std::string_view StableName="renegade.mission_animation_library";
};
}
