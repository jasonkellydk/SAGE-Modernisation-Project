export module engine.gameplay.common.timing.components.timeline_playback;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

export namespace engine::gameplay {
struct TimelinePlayback {
    std::uint64_t start_tick{};
    Engine::Math::Fixed elapsed;
    std::uint32_t timeline{},cursor{},enabled{1},complete{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::TimelinePlayback> {
    static constexpr std::string_view StableName="engine.gameplay.timeline_playback";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
