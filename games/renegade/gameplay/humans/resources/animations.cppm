export module games.renegade.gameplay.humans.resources.animations;
import std;
import engine.ecs.core.resource_store;
export import games.renegade.gameplay.humans.components.animation;
export import engine.gameplay.common.appearance.components.clip_playback;

export namespace renegade {
inline constexpr std::size_t HumanLocomotionClips=28;
struct HumanAnimationCatalog {std::vector<std::array<engine::gameplay::ClipPlayback,HumanLocomotionClips>> banks;};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::HumanAnimationCatalog> {static constexpr std::string_view StableName="renegade.human_animation_catalog";};
}
