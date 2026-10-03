export module engine.gameplay.common.timing.resources.timelines;
import std;
import engine.ecs.core.resource_store;
export import engine.gameplay.common.timing.components.timeline_playback;
export import engine.ecs.system.chunk_outputs;
export import engine.level.model.timeline;

export namespace engine::gameplay {
struct TimelineLibrary {std::vector<engine::level::Timeline> timelines;};
struct FiredTimelineCue {ecs::Entity playback;std::uint32_t timeline{},action{};Engine::Math::Fixed elapsed;};
using FiredTimelineCues=ecs::ChunkOutputs<FiredTimelineCue>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::TimelineLibrary> {static constexpr std::string_view StableName="engine.gameplay.timeline_library";};
template<> struct ResourceTraits<engine::gameplay::FiredTimelineCues> {static constexpr std::string_view StableName="engine.gameplay.fired_timeline_cues";};
}
