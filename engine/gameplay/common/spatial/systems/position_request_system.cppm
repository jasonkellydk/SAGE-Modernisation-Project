export module engine.gameplay.common.spatial.systems.position_request_system;
import std;
export import engine.gameplay.common.spatial.resources.position_requests;
export import engine.gameplay.common.spatial.components.affine_pose;

export namespace engine::gameplay {
// Relocation changes translation only. Velocity, navigation, collision and
// gameplay consequences belong to the composing systems' declared policies.
struct PositionRequestSystem {
    using Query=ecs::Query<ecs::Write<Transform>,ecs::OptionalWrite<AffinePose>>;
    using Resources=ecs::Resources<ecs::Read<PositionRequests>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto poses=chunk.Get<Transform>();const auto matrices=chunk.Get<AffinePose>();const auto entities=chunk.Entities();
        for(std::size_t row=0;row<poses.size();++row) {
            const PositionRequest* latest{};
            context.Read<PositionRequests>().ForEach([&](const auto& request) {
                if(request.subject==entities[row] && (!latest || request.order>=latest->order)) latest=&request;
            });
            if(!latest) continue;poses[row].position=latest->position;
            if(!matrices.empty()) {
                auto& m=matrices[row].transform.elements;m[3]=latest->position.x;m[7]=latest->position.y;m[11]=latest->position.z;
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::PositionRequestSystem> {
    static constexpr std::string_view StableName="engine.gameplay.position_request";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
