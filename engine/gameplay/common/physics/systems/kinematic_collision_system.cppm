export module engine.gameplay.common.physics.systems.kinematic_collision_system;
import std;
export import engine.gameplay.common.physics.components.kinematic_collider;
export import engine.gameplay.common.physics.resources.kinematic_collision;
export import engine.gameplay.common.physics.resources.collision_geometry;
export import engine.gameplay.common.spatial.components.affine_pose;
export import engine.gameplay.common.appearance.systems.clip_request_system;
export namespace engine::gameplay {
// Publish the completed tick's posed collision alongside the immutable static
// BVH. Parallel chunks own independent triangle columns; publication is a
// deterministic reduction and never rebuilds the static terrain BVH.
struct KinematicCollisionSystem {
    using Query=ecs::Query<ecs::Read<AffinePose>,ecs::Read<KinematicCollider>>;
    using Lookup=ecs::Lookup<ecs::Read<ClipPlayback>>;
    using Resources=ecs::Resources<ecs::Read<KinematicCollisionLibrary>,ecs::Write<KinematicCollisionOutputs>,ecs::Write<CollisionGeometry>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<KinematicCollisionOutputs>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto poses=chunk.Get<AffinePose>();const auto colliders=chunk.Get<KinematicCollider>();
        const auto clips=context.Lookup<Lookup>();const auto& models=context.Read<KinematicCollisionLibrary>().models;
        auto& output=context.Write<KinematicCollisionOutputs>().Slot(context);
        for(std::size_t row=0;row<colliders.size();++row) {
            const auto& collider=colliders[row];if(!collider.enabled || !collider.categories) continue;
            if(collider.definition>=models.size()) throw std::out_of_range("kinematic collision definition");
            const auto* clip=clips.Get<ClipPlayback>(chunk.Entities()[row]);
            const auto& model=models[collider.definition];
            if(clip && clip->clip>model.clips.size()) throw std::out_of_range("kinematic collision clip");
            auto geometry=clip && clip->clip ? engine::level::SampleCollisionTrack(model.clips[clip->clip-1],clip->frame) : model.rest;
            for(std::size_t triangle=0;triangle<geometry.first.size();++triangle) {
                geometry.first[triangle]=poses[row].transform.Point(geometry.first[triangle]);
                geometry.second[triangle]=poses[row].transform.Point(geometry.second[triangle]);
                geometry.third[triangle]=poses[row].transform.Point(geometry.third[triangle]);
                geometry.categories[triangle]&=collider.categories;
            }
            output.push_back({std::move(geometry),collider.subject});
        }
    }
    void AfterChunks(Query&,ecs::SystemContext& context) const {
        const auto& library=context.Read<KinematicCollisionLibrary>();
        // A session without this capability may inject CollisionGeometry
        // directly. Only an installed library owns publication.
        if(!library.static_scene && library.models.empty()) return;
        auto dynamic=std::make_shared<engine::level::CollisionScene3>();
        context.Write<KinematicCollisionOutputs>().ForEach([&](const auto& output) {
            engine::level::AppendModelCollision(*dynamic,output.geometry,{},output.subject);
        });dynamic->Prepare();
        auto composed=std::make_shared<engine::level::CollisionScene3>();
        if(library.static_scene) composed->AddLayer(library.static_scene);
        if(dynamic->TriangleCount()) composed->AddLayer(std::move(dynamic));
        composed->Prepare();context.Write<CollisionGeometry>().scene=std::move(composed);
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::KinematicCollisionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.kinematic_collision";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::ClipRequestSystem>;
};
}
