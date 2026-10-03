export module engine.gameplay.common.audio.components.sound_emitter;
import std;
export import engine.ecs.core.component_registry;
export namespace engine::gameplay {
// Clip IDs reference an injected presentation catalog. Playback state remains
// in SoA, while device handles stay outside the authoritative world.
struct SoundEmitter {std::uint32_t clip{},enabled{1};};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::SoundEmitter> {
    static constexpr std::string_view StableName="engine.gameplay.sound_emitter";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
