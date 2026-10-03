export module engine.gameplay.common.appearance.resources.clip_requests;
import std;
export import engine.gameplay.common.appearance.components.clip_playback;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.core.resource_store;
export namespace engine::gameplay {
struct ClipRequest {ecs::Entity subject;ClipPlayback playback;std::uint64_t order{};};
struct ClipRequests : ecs::ChunkOutputs<ClipRequest> {};
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::ClipRequests> {
    static constexpr std::string_view StableName="engine.gameplay.clip_requests";
};
}
