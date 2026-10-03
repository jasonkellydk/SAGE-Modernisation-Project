export module engine.gameplay.common.appearance.systems.clip_request_system;
import std;
export import engine.gameplay.common.appearance.resources.clip_requests;
export import engine.gameplay.common.appearance.systems.clip_playback_system;
export namespace engine::gameplay {
// Commands identify entities, never pointers into archetype storage. Multiple
// requests to a subject resolve by authored order, independent of job order.
struct ClipRequestSystem {
    using Query=ecs::Query<ecs::Write<ClipPlayback>>;
    using Resources=ecs::Resources<ecs::Read<ClipRequests>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto clips=chunk.Get<ClipPlayback>();const auto entities=chunk.Entities();
        for(std::size_t row=0;row<clips.size();++row) {
            const ClipRequest* selected{};
            context.Read<ClipRequests>().ForEach([&](const auto& request) {
                if(request.subject==entities[row] && (!selected || request.order>selected->order)) selected=&request;
            });
            if(selected) clips[row]=selected->playback;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::ClipRequestSystem> {
    static constexpr std::string_view StableName="engine.gameplay.clip_requests";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::ClipPlaybackSystem>;
};
}
