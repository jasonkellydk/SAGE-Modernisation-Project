export module engine.gameplay.common.timing.systems.timeline_system;
import std;
export import engine.gameplay.common.timing.components.timeline_playback;
export import engine.gameplay.common.timing.resources.timelines;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.level.model.timeline;
export namespace engine::gameplay {
// Every running authored sequence owns one clock in an archetype column.
// Immutable cue definitions are shared; games provide the action vocabulary.




}
export namespace engine::gameplay {
struct TimelineSystem {
    using Query=ecs::Query<ecs::Write<TimelinePlayback>>;
    using Resources=ecs::Resources<ecs::Read<TimelineLibrary>,ecs::Write<FiredTimelineCues>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<FiredTimelineCues>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto clocks=chunk.Get<TimelinePlayback>();const auto entities=chunk.Entities();const auto& library=context.Read<TimelineLibrary>();auto& fired=context.Write<FiredTimelineCues>().Slot(context);
        for(std::size_t row=0;row<clocks.size();++row) {
            auto& clock=clocks[row];if(!clock.enabled || clock.complete || !clock.timeline || clock.timeline>library.timelines.size()) continue;
            // Store tick+1 so a sequence starting at tick zero is unambiguous.
            if(!clock.start_tick) clock.start_tick=context.Time().Tick()+1;
            // Compute from integer ticks instead of accumulating a truncated
            // fixed delta. A 30-frame cue occurs exactly on its 60Hz tick.
            clock.elapsed=Engine::Math::Fixed::FromRatio(context.Time().Tick()+1-clock.start_tick,context.Time().Step().TicksPerSecond());
            const auto& cues=library.timelines[clock.timeline-1].cues;
            while(clock.cursor<cues.size() && cues[clock.cursor].time<=clock.elapsed) {
                fired.push_back({entities[row],clock.timeline,cues[clock.cursor].action,clock.elapsed});++clock.cursor;
            }
            clock.complete=clock.cursor==cues.size();
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::TimelineSystem> {
    static constexpr std::string_view StableName="engine.gameplay.timeline";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
