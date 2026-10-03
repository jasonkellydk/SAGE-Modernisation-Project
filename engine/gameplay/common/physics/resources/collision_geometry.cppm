export module engine.gameplay.common.physics.resources.collision_geometry;
import std;
export import engine.level.collision.collision_scene;
import engine.ecs.core.resource_store;

export namespace engine::gameplay {
// An immutable level BVH, injected by the session composition root. Scheduled
// systems share read access; a completed loader transaction replaces it only
// while the world is idle.
struct CollisionGeometry {std::shared_ptr<const engine::level::CollisionScene3> scene;};
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::CollisionGeometry> {
    static constexpr std::string_view StableName="engine.gameplay.collision_geometry";
};
}
