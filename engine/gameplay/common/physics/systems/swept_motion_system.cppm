export module engine.gameplay.common.physics.systems.swept_motion_system;
import std;
export import engine.ecs.system.system;
export import engine.gameplay.common.physics.algorithms.sweep_and_slide;
export import engine.gameplay.common.physics.components.swept_motion;
export import engine.gameplay.common.physics.resources.collision_geometry;
export import engine.gameplay.common.spatial.components.transform;

export namespace engine::gameplay {
struct SweptMotionSystem {
    using Query=ecs::Query<ecs::Write<Transform>,ecs::Read<SweptHull>,ecs::Write<SweptMotion>,ecs::Write<SweepContacts>>;
    using Resources=ecs::Resources<ecs::Read<CollisionGeometry>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto poses=chunk.Get<Transform>();const auto hulls=chunk.Get<SweptHull>();
        const auto motions=chunk.Get<SweptMotion>();const auto contacts=chunk.Get<SweepContacts>();
        const auto& geometry=context.Read<CollisionGeometry>();
        for(std::size_t row=0;row<poses.size();++row) {
            contacts[row]={};motions[row].applied={};
            const auto requested=std::exchange(motions[row].requested,{});
            if(!geometry.scene) continue; // No scene publication, no unchecked movement.
            motions[row].applied=SweepAndSlide(*geometry.scene,poses[row].position,requested,hulls[row],contacts[row]);
            poses[row].position=poses[row].position+motions[row].applied;
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::SweptMotionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.swept_motion";
    static constexpr std::size_t PieceRows=32;
    static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
