export module games.renegade.gameplay.humans.systems.motion_system;
import engine.gameplay.common.physics.systems.suspension_system;
import std;
import engine.gameplay.common.timing.systems.pose_track_system;
export import games.renegade.gameplay.humans.components.motion;
export import engine.gameplay.common.physics.algorithms.ground_motion;
export import engine.gameplay.common.physics.resources.collision_geometry;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.affine_pose;
export import engine.ecs.system.system;
export import engine.gameplay.common.input.components.input_permission;
export import engine.gameplay.common.spatial.systems.rigid_heading_system;

export namespace renegade {
// HumanPhys normal/ballistic movement policy. Per-entity state is held in
// archetype columns; the shared level and physics modules supply collision.
struct HumanMotionSystem {
    using Query=ecs::Query<ecs::Write<engine::gameplay::Transform>,ecs::Write<engine::gameplay::AffinePose>,
        ecs::Read<HumanMovement>,ecs::Write<HumanState>,ecs::Write<HumanControl>,
        ecs::Read<engine::gameplay::SweptHull>,ecs::Write<engine::gameplay::SweepContacts>>;
    using Resources=ecs::Resources<ecs::Read<engine::gameplay::CollisionGeometry>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::InputPermission>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;using namespace engine::gameplay;
        const auto& geometry=context.Read<CollisionGeometry>();if(!geometry.scene) return;
        const auto poses=chunk.Get<Transform>();const auto matrices=chunk.Get<AffinePose>();const auto presets=chunk.Get<HumanMovement>();
        const auto states=chunk.Get<HumanState>();const auto controls=chunk.Get<HumanControl>();const auto hulls=chunk.Get<SweptHull>();const auto contacts=chunk.Get<SweepContacts>();
        const auto dt=context.Time().SecondsPerTick();
        const auto distance=Fixed::FromRatio(1,10),epsilon=Fixed::FromRatio(1,50);
        for(std::size_t row=0;row<poses.size();++row) {
            auto& pose=poses[row];auto& state=states[row];auto& control=controls[row];const auto& preset=presets[row].definition;const auto& hull=hulls[row];
            const auto* permission=context.Lookup<Lookup>().Get<InputPermission>(chunk.Entities()[row]);
            const bool enabled=control.enabled && (!permission || permission->enabled);
            if(state.transition) {
                // HumanState::TRANSITION is immovable and user-controlled.
                // The authored pose supplies visible motion until completion.
                state.velocity={};control.jump=0;control.facing=pose.facing;contacts[row]={};continue;
            }
            if(enabled && !state.ladder) pose.facing=control.facing;else control.facing=pose.facing;
            contacts[row]={};const auto start=pose.position;
            const auto jump=std::exchange(control.jump,0u);const auto normal_z=Cos(TurnFromRadians(preset.slide_angle));
            if(state.ladder) {
                // SoldierGameObj::Apply_Control remaps forward to vertical
                // with CLIMB_SCALE=.3, retaining forward for networking.
                // Releasing forward clears the entry direction masks.
                const auto forward=enabled ? control.forward : Fixed{};
                if(forward==Fixed{}) {state.ladder_up_mask=0;state.ladder_down_mask=0;}
                auto up=forward*Fixed::FromRatio(3,10);
                if(state.ladder_up_mask) up=std::min(up,Fixed{});
                if(state.ladder_down_mask) up=(std::max)(up,Fixed{});
                const FixedVector3 requested{Fixed{},Fixed{},up*preset.speed*dt};
                pose.position=start+SweepAndSlide(*geometry.scene,start,requested,hull,contacts[row]);
                state.velocity=(pose.position-start)/dt;state.grounded=0;state.just_jumped=0;
                const auto cosine=Cos(pose.facing),sine=Sin(pose.facing);
                matrices[row].transform.elements={cosine,-sine,Fixed{},pose.position.x,sine,cosine,Fixed{},pose.position.y,Fixed{},Fixed{},Fixed::One(),pose.position.z};
                continue;
            }
            const auto ground=state.velocity.z<=Fixed{} && !state.just_jumped ? ProbeGround(*geometry.scene,start,hull,distance) : std::nullopt;
            state.grounded=ground && !ground->starts_overlapping && ground->normal.z>=normal_z;
            state.ground_normal=ground ? ground->normal : FixedVector3{};
            const FixedVector3 local{enabled ? control.forward : Fixed{},enabled ? control.left : Fixed{},Fixed{}};
            const auto cosine=Cos(pose.facing),sine=Sin(pose.facing);
            const FixedVector3 input{local.x*cosine-local.y*sine,local.x*sine+local.y*cosine,Fixed{}};
            FixedVector3 requested;
            if(state.grounded) {
                auto desired=input;const auto n=state.ground_normal;
                auto axis=Cross(FixedVector3{Fixed{},Fixed{},Fixed::One()},n);const auto sine_angle=Length(axis);
                if(sine_angle>Fixed{}) {axis=axis/sine_angle;desired=desired*n.z+Cross(axis,desired)*sine_angle+axis*(Dot(axis,desired)*(Fixed::One()-n.z));}
                const auto down=Normalize(n*n.z-FixedVector3{Fixed{},Fixed{},Fixed::One()});
                if(normal_z<Fixed::One()) desired=desired*(Fixed::One()+Fixed::FromRatio(1,10)*((Fixed::One()-n.z)/(Fixed::One()-normal_z))*Dot(desired,down));
                state.velocity=desired*preset.speed;
                if(jump && enabled) {state.velocity.z=preset.jump_velocity;state.just_jumped=1;state.grounded=0;}
                requested=state.velocity*dt;
                if(state.just_jumped) pose.position=start+SweepAndSlide(*geometry.scene,start,requested,hull,contacts[row]);
                else {
                    const auto horizontal=Length(FixedVector2{requested.x,requested.y});
                    const auto tangent=normal_z>Fixed{} ? Sin(TurnFromRadians(preset.slide_angle))/normal_z : Fixed{};
                    const auto moved=GroundMotion(*geometry.scene,start,requested,hull,{preset.step_height,Abs(requested.z)+horizontal*tangent+distance,normal_z,epsilon});
                    pose.position=start+moved.applied;contacts[row]=moved.contacts;state.grounded=moved.support.has_value();
                    if(moved.support) state.ground_normal=moved.support->normal;
                }
                state.velocity=(pose.position-start)/dt;
                if(!state.just_jumped) state.velocity.z=std::min(state.velocity.z,Fixed{});
            } else {
                // humanphys.cpp Ballistic_Move: analytical displacement first,
                // then gravity and the original 0.1 air-control acceleration.
                const auto acceleration=Fixed::FromRatio(-98,10)*preset.gravity_scale;
                requested=state.velocity*dt;requested.z+=Fixed::Half()*acceleration*dt*dt;
                pose.position=start+SweepAndSlide(*geometry.scene,start,requested,hull,contacts[row]);
                state.velocity.x=(pose.position.x-start.x)/dt;state.velocity.y=(pose.position.y-start.y)/dt;state.velocity.z+=acceleration*dt;
                for(unsigned i=0;i<contacts[row].count;++i) {
                    const auto n=contacts[row].normals[i];const auto dot=Dot(state.velocity,n);
                    if(dot<Fixed{}) state.velocity=state.velocity-n*dot;
                    if(n.z>=normal_z) {state.grounded=1;state.ground_normal=n;state.just_jumped=0;}
                }
                state.velocity.x+=Fixed::FromRatio(1,10)*preset.speed*input.x;state.velocity.y+=Fixed::FromRatio(1,10)*preset.speed*input.y;
                const auto horizontal=Length(FixedVector2{state.velocity.x,state.velocity.y});
                if(horizontal>preset.speed) {state.velocity.x*=preset.speed/horizontal;state.velocity.y*=preset.speed/horizontal;}
                if(state.velocity.z<=Fixed{}) state.just_jumped=0;
            }
            auto& m=matrices[row].transform.elements;
            m={cosine,-sine,Fixed{},pose.position.x,sine,cosine,Fixed{},pose.position.y,Fixed{},Fixed{},Fixed::One(),pose.position.z};
        }
    }
};
// A script's physical facing also becomes the soldier's retained control
// heading, so the next movement tick cannot restore a stale sample.
struct HumanHeadingSystem {
    using Query=ecs::Query<ecs::Write<HumanControl>>;
    using Resources=ecs::Resources<ecs::Read<engine::gameplay::HeadingRequests>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto controls=chunk.Get<HumanControl>();const auto entities=chunk.Entities();
        for(std::size_t row=0;row<controls.size();++row) {
            const engine::gameplay::HeadingRequest* latest{};
            context.Read<engine::gameplay::HeadingRequests>().ForEach([&](const auto& request) {
                if(request.subject==entities[row] && (!latest || request.order>=latest->order)) latest=&request;
            });if(latest) controls[row].facing=latest->heading;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::HumanHeadingSystem> {
    static constexpr std::string_view StableName="renegade.human_heading";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::RigidHeadingSystem>;
};
template<> struct SystemTraits<renegade::HumanMotionSystem> {
    static constexpr std::string_view StableName="renegade.human_motion";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<engine::gameplay::SuspensionSystem>;using After=SystemTypeList<engine::gameplay::PoseTrackSystem>;
};
}
