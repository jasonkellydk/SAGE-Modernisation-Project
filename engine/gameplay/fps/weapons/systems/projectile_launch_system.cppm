export module engine.gameplay.fps.weapons.systems.projectile_launch_system;
import std;
export import engine.gameplay.fps.weapons.components.projectile;
export import engine.gameplay.fps.weapons.systems.fire_sequence_system;
export import engine.gameplay.common.spatial.components.affine_pose;
export import engine.gameplay.common.appearance.components.model_override;
export import engine.gameplay.common.physics.resources.collision_geometry;
import engine.gameplay.common.spatial.systems.affine_snapshot_system;
import engine.ecs.system.chunk_outputs;
export namespace engine::gameplay {
struct ProjectileVisual {
    ecs::Entity entity;std::uint32_t model{},expired{};
    Engine::Math::FixedAffineTransform3 birth_pose,pose;
    std::uint64_t birth_tick{};
};
using ProjectileVisuals=ecs::ChunkOutputs<ProjectileVisual>;
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::ProjectileVisuals> {static constexpr std::string_view StableName="engine.gameplay.projectile_visuals";};
}
export namespace engine::gameplay {
struct ProjectileLaunchSystem {
    using Query=ecs::Query<ecs::Read<FireSequence>,ecs::Read<FireTrigger>,ecs::Write<ProjectileLauncher>>;
    using Lookup=ecs::Lookup<ecs::Read<AffinePose>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;const auto launchers=chunk.Get<ProjectileLauncher>();const auto shots=chunk.Get<FireSequence>();const auto triggers=chunk.Get<FireTrigger>();const auto poses=context.Lookup<Lookup>();
        for(std::size_t row=0;row<launchers.size();++row) {
            auto& launcher=launchers[row];const auto count=shots[row].shots-launcher.previous_shots;launcher.previous_shots=shots[row].shots;
            if(!count || launcher.speed<=Fixed{} || launcher.range<=Fixed{} || !poses.IsAlive(launcher.muzzle)) continue;const auto* muzzle=poses.Get<AffinePose>(launcher.muzzle);if(!muzzle) continue;
            const auto start=muzzle->transform.Point({});
            const auto muzzle_forward=FixedVector3{muzzle->transform.elements[0],muzzle->transform.elements[4],muzzle->transform.elements[8]};
            auto direction=launcher.aim==ProjectileAim::MuzzleForward ? muzzle_forward : triggers[row].target-start;
            auto length=Length(direction);
            if(launcher.aim==ProjectileAim::ConstrainedTarget) {
                const auto muzzle_length=Length(muzzle_forward);if(muzzle_length<=Fixed{}) continue;const auto forward=muzzle_forward/muzzle_length;
                if(length<=Fixed{} || Dot(direction/length,forward)<launcher.target_cone_cosine) {direction=forward;length=Fixed::One();}
            }
            if(length<=Fixed{}) continue;direction=direction/length;
            auto side=Cross(FixedVector3{{},{},Fixed::One()},direction);const auto side_length=Length(side);if(side_length>Fixed{}) side=side/side_length;else side={Fixed::One(),{}, {}};const auto up=Cross(direction,side);
            AffinePose pose;pose.transform.elements={direction.x,side.x,up.x,start.x,direction.y,side.y,up.y,start.y,direction.z,side.z,up.z,start.z};
            for(std::uint32_t shot=0;shot<count;++shot) {
                const auto entity=context.Commands().Create();context.Commands().Add<AffinePose>(entity,pose);
                context.Commands().Add<ModelOverride>(entity,ModelOverride{launcher.model});
                LinearFlight flight{direction*launcher.speed,launcher.gravity,launcher.range,launcher.categories};flight.birth_pose=pose.transform;flight.birth_tick=context.Tick();context.Commands().Add<LinearFlight>(entity,flight);
            }
        }
    }
};
struct LinearFlightSystem {
    using Query=ecs::Query<ecs::Write<AffinePose>,ecs::Write<LinearFlight>>;
    using Resources=ecs::Resources<ecs::Read<CollisionGeometry>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;const auto poses=chunk.Get<AffinePose>();const auto flights=chunk.Get<LinearFlight>();const auto& scene=context.Read<CollisionGeometry>();
        for(std::size_t row=0;row<flights.size();++row) {
            auto& flight=flights[row];auto& pose=poses[row];const auto time=context.Time().SecondsPerTick();if(flight.expired) continue;
            flight.velocity.z-=flight.gravity*time;auto move=flight.velocity*time;const auto distance=Length(move);
            if(distance<=Fixed{} || flight.remaining<=Fixed{}) {flight.expired=1;continue;}
            if(distance>flight.remaining) move=move*(flight.remaining/distance);
            const auto start=pose.transform.Point({});const auto hit=scene.scene ? scene.scene->Cast({start,{}},move,flight.categories) : std::nullopt;
            const auto point=start+move*(hit ? hit->fraction : Fixed::One());pose.transform.elements[3]=point.x;pose.transform.elements[7]=point.y;pose.transform.elements[11]=point.z;flight.remaining-=distance;
            if(hit || flight.remaining<=Fixed{}) flight.expired=1;
        }
    }
};
// Publication happens before deferred destruction is committed. Even a
// projectile that expires on its first flight tick retains its birth pose
// and final position for consumers of that completed simulation tick.
struct ProjectileSnapshotSystem {
    using Query=ecs::Query<ecs::Read<AffinePose>,ecs::Read<ModelOverride>,ecs::Read<LinearFlight>>;
    using Resources=ecs::Resources<ecs::Write<ProjectileVisuals>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<ProjectileVisuals>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto poses=chunk.Get<AffinePose>();const auto models=chunk.Get<ModelOverride>();const auto flights=chunk.Get<LinearFlight>();const auto entities=chunk.Entities();auto& out=context.Write<ProjectileVisuals>().Slot(context);
        for(std::size_t row=0;row<flights.size();++row) {
            out.push_back({entities[row],models[row].model,flights[row].expired,flights[row].birth_pose,poses[row].transform,flights[row].birth_tick});
            if(flights[row].expired) context.Commands().Destroy(entities[row]);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::ProjectileLaunchSystem> {
    static constexpr std::string_view StableName="engine.gameplay.projectile_launch";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::FireSequenceSystem,engine::gameplay::AffineSnapshotSystem>;
};
template<> struct SystemTraits<engine::gameplay::LinearFlightSystem> {
    static constexpr std::string_view StableName="engine.gameplay.linear_flight";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<engine::gameplay::AffineSnapshotSystem>;using After=SystemTypeList<>;
};
template<> struct SystemTraits<engine::gameplay::ProjectileSnapshotSystem> {
    static constexpr std::string_view StableName="engine.gameplay.projectile_snapshot";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::LinearFlightSystem>;
};
}
