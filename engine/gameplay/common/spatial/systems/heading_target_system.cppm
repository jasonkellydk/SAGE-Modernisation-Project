export module engine.gameplay.common.spatial.systems.heading_target_system;
import std;
export import engine.gameplay.common.spatial.resources.heading_requests;

export namespace engine::gameplay {
struct HeadingTargetSystem {
    using Query=ecs::Query<ecs::Read<Transform>>;
    using Lookup=ecs::Lookup<ecs::Read<Transform>>;
    using Resources=ecs::Resources<ecs::Read<HeadingTargets>,ecs::Write<HeadingRequests>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<HeadingRequests>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;const auto poses=chunk.Get<Transform>();const auto entities=chunk.Entities();const auto lookup=context.Lookup<Lookup>();
        auto& out=context.Write<HeadingRequests>().Slot(context);
        for(std::size_t row=0;row<poses.size();++row) {
            std::optional<HeadingRequest> latest;
            context.Read<HeadingTargets>().ForEach([&](const auto& request) {
                if(request.subject!=entities[row] || (latest && request.order<latest->order)) return;
                const auto* target_pose=request.entity_target ? lookup.Get<Transform>(request.target) : nullptr;
                if(request.entity_target && !target_pose) return;
                const auto delta=((target_pose ? target_pose->position : request.point)-poses[row].position).XY();
                const bool coincident=delta.x==Fixed{} && delta.y==Fixed{};
                if(coincident && !request.apply_coincident) return;
                latest=HeadingRequest{entities[row],coincident ? request.coincident_heading : Atan2(delta.y,delta.x),request.order};
            });
            if(latest) out.push_back(*latest);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::HeadingTargetSystem> {
    static constexpr std::string_view StableName="engine.gameplay.heading_targets";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
