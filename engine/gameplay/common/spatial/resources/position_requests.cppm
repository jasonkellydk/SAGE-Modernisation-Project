export module engine.gameplay.common.spatial.resources.position_requests;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.components.transform;

export namespace engine::gameplay {
struct PositionRequest {ecs::Entity subject;Engine::Math::FixedVector3 position;std::uint64_t order{};};
using PositionRequests=ecs::ChunkOutputs<PositionRequest>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::PositionRequests> {
    static constexpr std::string_view StableName="engine.gameplay.position_requests";
};
}
