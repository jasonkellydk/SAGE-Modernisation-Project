export module games.renegade.gameplay.missions.systems.speech_animation_system;
import std;
export import games.renegade.gameplay.missions.resources.speech_animations;
import games.renegade.gameplay.missions.systems.conversation_system;
export namespace renegade {
struct SpeechAnimationSystem {
    using Query=ecs::Query<ecs::Write<SpeechAnimation>>;
    using Resources=ecs::Resources<ecs::Read<SpeechAnimations>,ecs::Read<ConversationLineEvents>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto states=chunk.Get<SpeechAnimation>();const auto entities=chunk.Entities();const auto& banks=context.Read<SpeechAnimations>().banks;
        for(std::size_t row=0;row<states.size();++row) {
            auto& state=states[row];std::optional<ConversationLineEvent> selected;
            context.Read<ConversationLineEvents>().ForEach([&](const auto& event) {
                if(event.speaker!=entities[row]) return;
                if(event.finished) {if(event.conversation==state.conversation) state.playback.clip=0;return;}
                if(!selected || std::tie(event.conversation.index,event.conversation.generation)>std::tie(selected->conversation.index,selected->conversation.generation)) selected=event;
            });
            if(selected) {
                state.playback.clip=0;const auto& bank=banks.at(state.bank-1);
                if(const auto clip=std::ranges::find(bank,selected->face,&SpeechClip::speech);clip!=bank.end()) {
                    state.playback=clip->playback;state.started_tick=context.Tick();state.conversation=selected->conversation;
                }
            }
            if(state.playback.clip) {
                state.playback.frame=Engine::Math::Fixed::FromInt(context.Tick()-state.started_tick)*state.playback.frames_per_second/Engine::Math::Fixed::FromInt(context.Time().Step().TicksPerSecond());
                state.playback.frame=std::min(state.playback.frame,Engine::Math::Fixed::FromInt(state.playback.frame_count-1));
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::SpeechAnimationSystem> {
    static constexpr std::string_view StableName="renegade.speech_animation";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using After=SystemTypeList<renegade::ConversationSystem>;using Before=SystemTypeList<>;
};
}
