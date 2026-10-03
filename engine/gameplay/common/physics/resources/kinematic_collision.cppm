export module engine.gameplay.common.physics.resources.kinematic_collision;
import std;
export import engine.level.model.collision_mesh;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.core.resource_store;
export namespace engine::gameplay {
struct KinematicCollisionDefinition {
    engine::level::ModelCollision3 rest;
    std::vector<engine::level::ModelCollisionTrack3> clips;
};
struct KinematicCollisionLibrary {
    std::shared_ptr<const engine::level::CollisionScene3> static_scene;
    std::vector<KinematicCollisionDefinition> models;
};
struct KinematicCollisionTriangles {
    engine::level::ModelCollision3 geometry;
    std::uint64_t subject{};
};
struct KinematicCollisionOutputs : ecs::ChunkOutputs<KinematicCollisionTriangles> {};
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::KinematicCollisionLibrary> {
    static constexpr std::string_view StableName="engine.gameplay.kinematic_collision_library";
};
template<> struct ResourceTraits<engine::gameplay::KinematicCollisionOutputs> {
    static constexpr std::string_view StableName="engine.gameplay.kinematic_collision_outputs";
};
}
