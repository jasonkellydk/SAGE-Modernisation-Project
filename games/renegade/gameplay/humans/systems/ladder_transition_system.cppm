export module games.renegade.gameplay.humans.systems.ladder_transition_system;
import std;
export import games.renegade.gameplay.humans.resources.ladders;
export import games.renegade.gameplay.humans.systems.motion_system;
export import games.renegade.gameplay.humans.resources.animations;
export import engine.gameplay.common.appearance.components.clip_transition;
export import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.timing.systems.pose_track_system;

export namespace renegade {
// Combat/transition.cpp Check/Start/End: first eligible authored portal,
// full-pose transfer and authored once animation before completing ladder state.
// Restore the source's Start_Transition_Animation path: the published source
// disables that call, but retail exit poses require its 1.6m animated movement.
struct LadderTransitionSystem {
    using Query=ecs::Query<ecs::Write<engine::gameplay::Transform>,ecs::Write<engine::gameplay::AffinePose>,
        ecs::Write<HumanState>,ecs::Write<HumanControl>,ecs::Optional<engine::gameplay::Owner>,
        ecs::Read<HumanAnimation>,ecs::Write<engine::gameplay::ClipPlayback>,ecs::Write<engine::gameplay::ClipTransition>>;
    using Resources=ecs::Resources<ecs::Read<LadderPortals>,ecs::Read<HumanAnimationCatalog>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::InputPermission>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;using namespace content;
        const auto entities=chunk.Entities();const auto poses=chunk.Get<engine::gameplay::Transform>();const auto matrices=chunk.Get<engine::gameplay::AffinePose>();
        const auto states=chunk.Get<HumanState>();const auto controls=chunk.Get<HumanControl>();const auto owners=chunk.Get<engine::gameplay::Owner>();
        const auto animations=chunk.Get<HumanAnimation>();const auto clips=chunk.Get<engine::gameplay::ClipPlayback>();const auto blends=chunk.Get<engine::gameplay::ClipTransition>();
        for(std::size_t row=0;row<states.size();++row) {
            auto& state=states[row];auto& control=controls[row];const auto* permission=context.Lookup<Lookup>().Get<engine::gameplay::InputPermission>(entities[row]);
            const bool manual=std::exchange(control.action,0u) && control.enabled && (!permission || permission->enabled);
            if(state.state_locked) continue;
            for(const auto& portal:context.Read<LadderPortals>().portals) {
                if(!portal.geometry.bounds.Contains(poses[row].position)) continue;
                const bool enter=portal.style==TransitionStyle::LadderEnterTop || portal.style==TransitionStyle::LadderEnterBottom;
                if(enter ? state.ladder || !state.grounded || state.just_jumped : !state.ladder) continue;
                if(!manual) {
                    if(enter) continue;
                    const auto speed=Length(state.velocity);if(speed<=Fixed::FromRatio(1,10)) continue;
                    const auto direction=portal.style==TransitionStyle::LadderExitTop ? Fixed::One() : -Fixed::One();
                    if(state.velocity.z*direction/speed<=Fixed::Half()) continue;
                }
                const auto bank=animations[row].bank;const auto& catalog=context.Read<HumanAnimationCatalog>();
                if(!bank || bank>catalog.banks.size()) throw std::logic_error("ladder requires a prepared animation bank");
                auto clip=catalog.banks[bank-1][24+std::uint32_t(portal.style)];
                if(!clip.clip || clip.frame_count<2 || clip.frames_per_second<=Fixed{}) throw std::logic_error("ladder transition animation is unavailable");
                clip.mode=engine::gameplay::ClipMode::Once;clip.frame={};clips[row]=clip;blends[row]={};
                matrices[row].transform=portal.geometry.destination;
                const auto& m=portal.geometry.destination.elements;
                poses[row].position={m[3],m[7],m[11]};poses[row].facing=Atan2(m[4],m[0]);control.facing=poses[row].facing;
                state.velocity={};state.grounded=0;state.just_jumped=0;state.state_locked=1;
                state.transition=1+std::uint32_t(portal.style);++state.transition_sequence;
                state.ladder_up_mask=enter && !owners.empty() && portal.style==TransitionStyle::LadderEnterTop;
                state.ladder_down_mask=enter && !owners.empty() && portal.style==TransitionStyle::LadderEnterBottom;
                break;
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::LadderTransitionSystem> {
    static constexpr std::string_view StableName="renegade.ladder_transition";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<renegade::HumanMotionSystem>;using After=SystemTypeList<engine::gameplay::PoseTrackSystem>;
};
}
