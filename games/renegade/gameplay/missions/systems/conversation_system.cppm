export module games.renegade.gameplay.missions.systems.conversation_system;
import std;
export import games.renegade.gameplay.missions.components.conversation_state;
export import games.renegade.gameplay.missions.resources.conversation_library;
import games.renegade.gameplay.missions.systems.mission_command_system;
import engine.gameplay.common.timing.systems.timeline_system;
import engine.gameplay.common.spatial.components.affine_pose;
import engine.gameplay.common.spatial.systems.affine_snapshot_system;
import engine.gameplay.common.audio.systems.emitter_snapshot_system;
export namespace renegade {
struct ConversationSystem {
    using Query=ecs::Query<ecs::Read<ConversationState>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::AffinePose>>;
    using Resources=ecs::Resources<ecs::Read<ConversationLibrary>,ecs::Read<engine::gameplay::FiredTimelineCues>,ecs::Read<MissionRequests>,ecs::Write<ConversationLineEvents>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<ConversationLineEvents>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace engine::gameplay;const auto states=chunk.Get<ConversationState>();const auto entities=chunk.Entities();const auto poses=context.Lookup<Lookup>();
        const auto& library=context.Read<ConversationLibrary>();auto& lines=context.Write<ConversationLineEvents>().Slot(context);
        std::optional<std::pair<std::uint64_t,std::uint64_t>> stop_all;
        context.Read<MissionRequests>().ForEach([&](const auto& request) {
            if(request.command.name!="Stop_All_Conversations") return;
            const std::pair key{request.tick,request.order};if(!stop_all || key>*stop_all) stop_all=key;
        });
        for(std::size_t row=0;row<states.size();++row) {
            const auto& state=states[row];const auto& definition=library.definitions.at(state.definition);
            const auto finish=[&](std::int64_t reason) {
                lines.push_back({entities[row],state.speaker,0,0,1,std::uint32_t(reason)});
                BehaviorMessage message;message.recipient=state.speaker;message.observer=state.observer;message.event=std::uint32_t(content::MissionEvent::ActionComplete);
                message.due_tick=context.Tick();message.arguments={std::int64_t(state.authored_speaker),state.action,reason,0};message.argument_count=3;
                const auto timer=context.Commands().Create();context.Commands().Add<ScheduledBehaviorMessage>(timer,ScheduledBehaviorMessage{message});context.Commands().Destroy(entities[row]);
            };
            if((stop_all && std::pair{state.created_tick,state.created_order}<*stop_all) || !poses.IsAlive(state.speaker) || !poses.IsAlive(state.listener)) {finish(7);continue;}
            context.Read<FiredTimelineCues>().ForEach([&](const auto& cue) {
                if(cue.playback!=entities[row]) return;
                if(cue.action==definition.lines.size()) {finish(6);return;}
                const auto& line=definition.lines.at(cue.action);const auto speaker=line.speaker==0 ? state.speaker : state.listener;
                const auto* pose=poses.Get<AffinePose>(speaker);if(!pose) return;
                lines.push_back({entities[row],speaker,line.text,line.sound,0,0,line.face});if(!line.sound) return;
                const auto voice=context.Commands().Create();context.Commands().Add<ConversationVoice>(voice,ConversationVoice{entities[row]});
                context.Commands().Add<SoundEmitter>(voice,SoundEmitter{line.sound,1});context.Commands().Add<Transform>(voice,Transform{pose->transform.Point({}),{}});
            });
        }
    }
};
struct ConversationVoiceSystem {
    using Query=ecs::Query<ecs::Read<ConversationVoice>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::TimelinePlayback>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto voices=chunk.Get<ConversationVoice>();const auto entities=chunk.Entities();const auto clocks=context.Lookup<Lookup>();
        for(std::size_t row=0;row<voices.size();++row) {
            const auto* clock=clocks.Get<engine::gameplay::TimelinePlayback>(voices[row].conversation);
            if(!clock || clock->complete) context.Commands().Destroy(entities[row]);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::ConversationSystem> {
    static constexpr std::string_view StableName="renegade.conversation";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<engine::gameplay::EmitterSnapshotSystem>;using After=SystemTypeList<renegade::MissionCommandSystem,engine::gameplay::TimelineSystem,engine::gameplay::AffineSnapshotSystem>;
};
template<> struct SystemTraits<renegade::ConversationVoiceSystem> {
    static constexpr std::string_view StableName="renegade.conversation_voice";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<engine::gameplay::EmitterSnapshotSystem>;using After=SystemTypeList<renegade::ConversationSystem>;
};
}
