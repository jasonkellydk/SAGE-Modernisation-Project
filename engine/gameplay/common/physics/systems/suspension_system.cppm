export module engine.gameplay.common.physics.systems.suspension_system;
import std;
export import engine.gameplay.common.physics.components.suspension;
export import engine.gameplay.common.physics.resources.suspension_snapshots;
export import engine.gameplay.common.timing.systems.pose_track_system;
export import engine.gameplay.common.physics.resources.collision_geometry;
export import engine.ecs.system.chunk_outputs;
import Engine.Core.Math.FixedAngle;
export namespace engine::gameplay {
// One wheel per archetype row. The game binds sockets and collision masks;
// the shared mechanism has no knowledge of model names or vehicle presets.




}
export namespace engine::gameplay {
struct SuspensionSystem {
    using Query=ecs::Query<ecs::Read<Suspension>,ecs::Write<SuspensionState>>;
    using Lookup=ecs::Lookup<ecs::Read<AffinePose>>;
    using Resources=ecs::Resources<ecs::Read<CollisionGeometry>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;const auto wheels=chunk.Get<Suspension>();const auto states=chunk.Get<SuspensionState>();
        const auto poses=context.Lookup<Lookup>();const auto& geometry=context.Read<CollisionGeometry>();
        for(std::size_t row=0;row<wheels.size();++row) {
            const auto& wheel=wheels[row];auto& state=states[row];
            if(!poses.IsAlive(wheel.parent)) {context.Commands().Destroy(chunk.Entities()[row]);continue;}
            const auto* parent=poses.Get<AffinePose>(wheel.parent);if(!parent || wheel.travel<=Fixed{}) continue;
            const auto socket=parent->transform*wheel.socket;const auto start=socket.Point({});
            const auto move=socket.Point({{},{},-wheel.travel})-start;
            const auto hit=geometry.scene ? geometry.scene->Cast({start,{}},move,wheel.categories) : std::nullopt;
            const auto fraction=hit ? hit->fraction : Fixed::One();const auto contact=start+move*fraction;
            state.displacement=-wheel.travel*fraction*wheel.translation_scale;state.contact=hit && fraction<Fixed::One();
            if(state.initialized && state.contact && wheel.radius>Fixed{}) {
                const auto forward=socket.Point({Fixed::One(),{}, {}})-start;
                state.rotation-=Dot(contact-state.last_contact,forward)/wheel.radius;
                const auto period=(Radians(TurnFromDegrees(180))*Fixed::FromInt(2)).Raw();const auto raw=state.rotation.Raw()%period;
                state.rotation=Fixed::FromRaw(raw<0 ? raw+period : raw);
            }
            state.last_contact=contact;state.initialized=1;
        }
    }
};
struct SuspensionSnapshotSystem {
    using Query=ecs::Query<ecs::Read<Suspension>,ecs::Read<SuspensionState>>;
    using Resources=ecs::Resources<ecs::Write<SuspensionSnapshots>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<SuspensionSnapshots>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto wheels=chunk.Get<Suspension>();const auto states=chunk.Get<SuspensionState>();auto& out=context.Write<SuspensionSnapshots>().Slot(context);
        for(std::size_t row=0;row<wheels.size();++row) if(states[row].initialized) out.push_back({wheels[row].parent,wheels[row].position_bone,wheels[row].rotation_bone,states[row].displacement,states[row].rotation});
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::SuspensionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.suspension";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::PoseTrackSystem>;
};
template<> struct SystemTraits<engine::gameplay::SuspensionSnapshotSystem> {
    static constexpr std::string_view StableName="engine.gameplay.suspension_snapshot";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
