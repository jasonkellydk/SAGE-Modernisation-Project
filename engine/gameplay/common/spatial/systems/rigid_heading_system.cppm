export module engine.gameplay.common.spatial.systems.rigid_heading_system;
import std;
export import engine.gameplay.common.spatial.systems.heading_target_system;
export import engine.gameplay.common.spatial.components.affine_pose;

export namespace engine::gameplay {
// Absolute rigid planar rotation: replace the basis with a unit Z rotation,
// retain translation. Scaled/articulated orientation uses different policies.
struct RigidHeadingSystem {
    using Query=ecs::Query<ecs::Write<Transform>,ecs::OptionalWrite<AffinePose>>;
    using Resources=ecs::Resources<ecs::Read<HeadingRequests>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;const auto poses=chunk.Get<Transform>();const auto matrices=chunk.Get<AffinePose>();const auto entities=chunk.Entities();
        for(std::size_t row=0;row<poses.size();++row) {
            const HeadingRequest* latest{};context.Read<HeadingRequests>().ForEach([&](const auto& request) {
                if(request.subject==entities[row] && (!latest || request.order>=latest->order)) latest=&request;
            });
            if(!latest) continue;auto& pose=poses[row];pose.facing=latest->heading;
            if(!matrices.empty()) {const auto c=Cos(pose.facing),s=Sin(pose.facing);matrices[row].transform.elements={c,-s,Fixed{},pose.position.x,s,c,Fixed{},pose.position.y,Fixed{},Fixed{},Fixed::One(),pose.position.z};}
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::RigidHeadingSystem> {
    static constexpr std::string_view StableName="engine.gameplay.rigid_heading";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::HeadingTargetSystem>;
};
}
