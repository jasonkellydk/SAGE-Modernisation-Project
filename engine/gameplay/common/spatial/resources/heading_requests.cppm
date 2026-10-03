export module engine.gameplay.common.spatial.resources.heading_requests;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.spatial.components.transform;

export namespace engine::gameplay {
// Targets resolve against one completed set of positions before any heading
// is written. This permits cross-entity requests without chunk read/write races.
struct HeadingTargetRequest {
    ecs::Entity subject,target;
    Engine::Math::FixedVector3 point;
    std::uint64_t order{};
    Engine::Math::TurnAngle coincident_heading;
    bool entity_target{true},apply_coincident{false};
};
struct HeadingRequest {ecs::Entity subject;Engine::Math::TurnAngle heading;std::uint64_t order{};};
using HeadingTargets=ecs::ChunkOutputs<HeadingTargetRequest>;
using HeadingRequests=ecs::ChunkOutputs<HeadingRequest>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::HeadingTargets> {static constexpr std::string_view StableName="engine.gameplay.heading_targets";};
template<> struct ResourceTraits<engine::gameplay::HeadingRequests> {static constexpr std::string_view StableName="engine.gameplay.heading_requests";};
}
