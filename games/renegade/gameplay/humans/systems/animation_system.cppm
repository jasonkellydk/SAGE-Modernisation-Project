export module games.renegade.gameplay.humans.systems.animation_system;
import std;
export import games.renegade.gameplay.humans.components.animation;
export import games.renegade.gameplay.humans.resources.animations;
export import games.renegade.gameplay.humans.systems.motion_system;
export import engine.gameplay.common.appearance.systems.clip_playback_system;
export import engine.gameplay.common.appearance.components.clip_transition;
export namespace renegade {
// The source's leg-state table is game policy. The playheads, transition
// interpolation and W3D evaluation are shared mechanisms.



}
export namespace renegade {
struct HumanAnimationSystem {
    using Query=ecs::Query<ecs::Write<HumanState>,ecs::Read<engine::gameplay::Transform>,ecs::Write<HumanAnimation>,ecs::Write<engine::gameplay::ClipPlayback>,ecs::Write<engine::gameplay::ClipTransition>>;
    using Resources=ecs::Resources<ecs::Read<HumanAnimationCatalog>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;using namespace engine::gameplay;
        const auto states=chunk.Get<HumanState>();const auto poses=chunk.Get<Transform>();const auto animations=chunk.Get<HumanAnimation>();
        const auto clips=chunk.Get<ClipPlayback>();const auto transitions=chunk.Get<ClipTransition>();const auto& catalog=context.Read<HumanAnimationCatalog>();
        for(std::size_t row=0;row<states.size();++row) {
            auto& animation=animations[row];if(!animation.bank || animation.bank>catalog.banks.size()) continue;
            auto& state=states[row];
            if(state.transition) {
                if(!OnceClipComplete(clips[row])) continue;
                state.ladder=state.transition>=3;state.transition=0;state.state_locked=0;
                state.velocity={};state.grounded=0;animation.landing=0;
                if(!state.ladder) {state.ladder_up_mask=0;state.ladder_down_mask=0;}
            }
            const auto& pose=poses[row];const auto cosine=Cos(pose.facing),sine=Sin(pose.facing);
            const auto forward=state.velocity.x*cosine+state.velocity.y*sine,left=-state.velocity.x*sine+state.velocity.y*cosine;
            unsigned direction=0;const auto threshold=Fixed::FromRatio(1,5);
            if(Abs(forward)>Fixed::FromRatio(3,4)*Abs(left)) {if(forward>threshold) direction=1;else if(forward<-threshold) direction=2;}
            else {if(left>threshold) direction=3;else if(left<-threshold) direction=4;}
            unsigned selected=direction;auto scale=Fixed::One();
            if(state.ladder) {
                // humanstate.cpp Handle_Legs uses the same strict .2
                // movement threshold for vertical ladder substate flags.
                selected=state.velocity.z>threshold ? 22 : state.velocity.z<-threshold ? 23 : 21;animation.landing=0;
                if(selected!=21) scale=std::clamp(Length(state.velocity)/Fixed::FromRatio(15,100),Fixed::FromRatio(33,100),Fixed::FromInt(3));
            }
            else if(!state.grounded) {selected=9+direction;animation.landing=0;}
            else if(!animation.grounded || animation.landing && clips[row].frame<Fixed::FromInt(clips[row].frame_count ? clips[row].frame_count-1 : 0)) {selected=14+direction;animation.landing=1;}
            else {
                animation.landing=0;const auto speed=Length(state.velocity);
                const bool slow=speed<Fixed::FromRatio(321,100);
                if(direction) {
                    selected=slow ? 4+direction : direction;
                    const auto ideal=slow ? (direction==1 || direction==4 ? Fixed::FromRatio(16,10) : Fixed::FromRatio(15,10)) :
                        (direction==1 || direction==4 ? Fixed::FromRatio(55,10) : Fixed::FromRatio(45,10));
                    scale=speed/ideal;
                } else {
                    const auto turn=std::bit_cast<std::int32_t>(pose.facing.units-animation.previous_facing);
                    if(turn) selected=turn>0 ? 19 : 20;
                }
            }
            auto& playback=clips[row];auto& transition=transitions[row];const auto& target=catalog.banks[animation.bank-1][selected];
            if(playback.clip!=target.clip) {transition.previous=playback;transition.elapsed={};transition.duration=Fixed::FromRatio(1,5);playback=target;}
            playback.frames_per_second=target.frames_per_second*scale;
            transition.elapsed=std::min(transition.duration,transition.elapsed+context.Time().SecondsPerTick());
            if(transition.elapsed<transition.duration && transition.previous.frame_count) {
                const auto period=transition.previous.loop_period.Raw();transition.previous.frame+=context.Time().PerTick(transition.previous.frames_per_second);
                if(transition.previous.mode==ClipMode::Loop && period>0) transition.previous.frame=Fixed::FromRaw(transition.previous.frame.Raw()%period);
                else transition.previous.frame=std::min(transition.previous.frame,Fixed::FromInt(transition.previous.frame_count-1));
            }
            animation.grounded=state.grounded;animation.previous_facing=pose.facing.units;
        }
    }
};
}
export namespace ecs {

template<> struct SystemTraits<renegade::HumanAnimationSystem> {
    static constexpr std::string_view StableName="renegade.human_animation_selection";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<engine::gameplay::ClipPlaybackSystem>;using After=SystemTypeList<renegade::HumanMotionSystem>;
};
}
