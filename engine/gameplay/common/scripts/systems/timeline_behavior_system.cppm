export module engine.gameplay.common.scripts.systems.timeline_behavior_system;
import std;
export import engine.gameplay.common.scripts.components.timeline_behavior;
import engine.gameplay.common.timing.systems.timeline_system;
export namespace engine::gameplay {
struct TimelineBehaviorSystem {
    using Query=ecs::Query<ecs::Write<LuaBehavior>,ecs::Read<TimelineBehavior>>;
    using Resources=ecs::Resources<ecs::Read<FiredTimelineCues>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto behaviors=chunk.Get<LuaBehavior>();const auto bindings=chunk.Get<TimelineBehavior>();
        for(std::size_t row=0;row<behaviors.size();++row) {
            if(behaviors[row].enabled || behaviors[row].initialized) continue;
            context.Read<FiredTimelineCues>().ForEach([&](const auto& cue) {
                if(cue.playback==bindings[row].clock && cue.action==bindings[row].action) behaviors[row].enabled=1;
            });
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::TimelineBehaviorSystem> {
    static constexpr std::string_view StableName="engine.gameplay.timeline_behavior";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using After=SystemTypeList<engine::gameplay::TimelineSystem>;using Before=SystemTypeList<>;
};
}
