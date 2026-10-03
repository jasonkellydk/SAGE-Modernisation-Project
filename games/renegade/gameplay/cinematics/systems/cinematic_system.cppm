export module games.renegade.gameplay.cinematics.systems.cinematic_system;
import std;
export import games.renegade.gameplay.cinematics.components.cinematic_state;
export import games.renegade.gameplay.cinematics.resources.cinematic_library;
import games.renegade.gameplay.humans.systems.animation_system;
export import games.renegade.content.cinematics.cinematic;
export import engine.gameplay.common.timing.systems.pose_track_system;
export import engine.gameplay.common.appearance.systems.clip_playback_system;
export import engine.gameplay.common.appearance.components.draw_hidden;
export import engine.gameplay.common.appearance.components.clip_transition;
export import engine.gameplay.fps.weapons.systems.fire_sequence_system;
export import games.renegade.content.cinematics.attack;
export import engine.time.value_transition;
export namespace renegade {
struct CinematicActorSystem {
    using Query=ecs::Query<ecs::Write<CinematicActor>,ecs::Write<engine::gameplay::ClipPlayback>,ecs::Write<engine::gameplay::ClipTransition>,ecs::Write<engine::gameplay::FireTrigger>,ecs::Read<engine::gameplay::AffinePose>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::TimelinePlayback>>;
    using Resources=ecs::Resources<ecs::Read<CinematicLibrary>,ecs::Read<engine::gameplay::FiredTimelineCues>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto actors=chunk.Get<CinematicActor>();const auto clips=chunk.Get<engine::gameplay::ClipPlayback>();const auto entities=chunk.Entities();const auto& library=context.Read<CinematicLibrary>();
        const auto transitions=chunk.Get<engine::gameplay::ClipTransition>();const auto triggers=chunk.Get<engine::gameplay::FireTrigger>();const auto poses=chunk.Get<engine::gameplay::AffinePose>();const auto clocks=context.Lookup<Lookup>();
        for(std::size_t row=0;row<actors.size();++row) {
          auto& transition=transitions[row];transition.elapsed=std::min(transition.duration,transition.elapsed+context.Time().SecondsPerTick());
          if(transition.elapsed<transition.duration && transition.previous.clip) transition.previous.frame+=context.Time().PerTick(transition.previous.frames_per_second);
          if(const auto* clock=clocks.Get<engine::gameplay::TimelinePlayback>(actors[row].clock);clock && clock->elapsed>=actors[row].attack_until) triggers[row].pressed=0;
          context.Read<engine::gameplay::FiredTimelineCues>().ForEach([&](const auto& cue) {
            auto& actor=actors[row];if(cue.playback!=actor.clock || cue.action>=library.definition.actions.size()) return;
            const auto& action=library.definition.actions[cue.action];if(action.slot!=actor.slot) return;
            using O=content::CinematicOpcode;
            if(action.opcode==O::CreateModel || action.opcode==O::CreatePreset) {
                if(!actor.active) context.Commands().Remove<engine::gameplay::DrawHidden>(entities[row]);actor.active=1;
            } else if(action.opcode==O::Destroy) {
                if(actor.active) context.Commands().Destroy(entities[row]);actor.active=0;
            } else if(action.opcode==O::Animation) {
                const auto found=library.animations.find(cue.action);if(found==library.animations.end()) return;
                transition.previous=clips[row];transition.elapsed={};transition.duration=action.blended ? Engine::Math::Fixed::FromRatio(1,5) : Engine::Math::Fixed{};
                clips[row]=found->second;
                // ClipPlaybackSystem advances later in this tick. Source
                // Test_Cinematic starts at elapsed time since the command.
                const auto authored=std::ranges::find(library.definition.timeline.cues,cue.action,&engine::level::TimelineCue::action);
                clips[row].frame=(cue.elapsed-authored->time)*clips[row].frames_per_second-context.Time().PerTick(clips[row].frames_per_second);
            } else if(action.opcode==O::Script) {
                const auto attack=library.attacks.find(cue.action);if(attack==library.attacks.end() || !actor.active) return;
                const auto& command=attack->second;const auto& pose=poses[row].transform;
                using namespace Engine::Math;auto forward=FixedVector3{pose.elements[0],pose.elements[4],{}};const auto length=Length(forward);if(length>Fixed{}) forward=forward/length;
                const FixedVector3 side{-forward.y,forward.x,{}};
                triggers[row].target=pose.Point({})+forward*command.offset.x+side*command.offset.y+FixedVector3{{},{},command.offset.z};triggers[row].pressed=1;triggers[row].force=command.force;actor.attack_until=cue.elapsed+command.duration;
            }
          });
        }
    }
};
struct CinematicDirectorSystem {
    using Query=ecs::Query<ecs::Read<engine::gameplay::TimelinePlayback>,ecs::Write<CinematicDirector>>;
    using Resources=ecs::Resources<ecs::Read<CinematicLibrary>,ecs::Read<engine::gameplay::FiredTimelineCues>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto states=chunk.Get<CinematicDirector>();const auto entities=chunk.Entities();const auto& library=context.Read<CinematicLibrary>();
        for(std::size_t row=0;row<states.size();++row) context.Read<engine::gameplay::FiredTimelineCues>().ForEach([&](const auto& cue) {
            if(cue.playback!=entities[row] || cue.action>=library.definition.actions.size()) return;
            auto& state=states[row];const auto& action=library.definition.actions[cue.action];++state.commands_seen;
            using O=content::CinematicOpcode;
            switch(action.opcode) {
            case O::Camera:state.camera=action.slot;break;
            case O::Letterbox:engine::time::Retarget(state.letterbox,cue.elapsed,action.values[0],action.values[1]);break;
            case O::FadeOpacity:engine::time::Retarget(state.fade,cue.elapsed,action.values[0],action.values[1]);break;
            case O::FadeColor:std::copy_n(action.values.begin(),3,state.fade_color.begin());break;
            case O::Script:++state.scripts_seen;break;
            case O::Custom:++state.customs_seen;break;
            default:break;
            }
        });
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::CinematicActorSystem> {
    static constexpr std::string_view StableName="renegade.cinematic_actor";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<engine::gameplay::ClipPlaybackSystem>;using After=SystemTypeList<renegade::HumanAnimationSystem>;
};
template<> struct SystemTraits<renegade::CinematicDirectorSystem> {
    static constexpr std::string_view StableName="renegade.cinematic_director";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
