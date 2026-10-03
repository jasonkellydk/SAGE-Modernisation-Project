export module games.renegade.gameplay.cinematics.resources.cinematic_library;
import std;
import engine.ecs.core.resource_store;
export import games.renegade.gameplay.cinematics.components.cinematic_state;
export import games.renegade.content.cinematics.cinematic;
export import games.renegade.content.cinematics.attack;
export import engine.gameplay.common.appearance.components.clip_playback;

export namespace renegade {
struct CinematicLibrary {
    content::CinematicDefinition definition;
    std::map<std::uint32_t,engine::gameplay::ClipPlayback> animations; // action -> bound clip metadata
    std::map<std::uint32_t,content::CinematicAttack> attacks;
};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::CinematicLibrary> {static constexpr std::string_view StableName="renegade.cinematic_library";};
}
