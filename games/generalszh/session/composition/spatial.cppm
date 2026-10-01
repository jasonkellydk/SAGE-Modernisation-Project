export module games.generalszh.session.composition.spatial;
import std;
import engine.gameplay.common.spatial.resources.deck_surfaces;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.rts.collision.systems.body_collision_system;
import engine.gameplay.rts.containment.systems.container_classes_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.rts.stealth.systems.defector_system;
import engine.gameplay.rts.stealth.systems.stealth_system;
import engine.gameplay.rts.vision.systems.object_shroud_system;
import games.generalszh.gameplay.appearance.systems.extension_look_system;
import games.generalszh.gameplay.appearance.systems.panic_look_system;
import games.generalszh.gameplay.appearance.systems.steering_look_system;
import engine.gameplay.common.spatial.systems.dynamic_geometry_system;
import engine.gameplay.common.spatial.systems.snapshot_system;
import engine.gameplay.common.spatial.systems.spatial_index_system;
import engine.gameplay.common.spatial.components.attitude;
import engine.gameplay.common.spatial.components.body_extent;
import engine.gameplay.common.spatial.components.bounding_volume;
import engine.gameplay.common.spatial.components.carried;
import engine.gameplay.common.spatial.components.dynamic_geometry;
import engine.gameplay.common.spatial.components.object_shroud;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.spatial.components.surface_layer;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.components.transform;

// The spatial domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The spatial domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceSpatialResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::SpatialGather>();
	world.EmplaceResource<engine::gameplay::DeckSurfaces>();
}

inline void RegisterSpatialComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Transform>();
	world.RegisterComponent<engine::gameplay::BodyExtent>();
	world.RegisterComponent<engine::gameplay::ObjectShroud>();
	world.RegisterComponent<engine::gameplay::Targetable>();
	world.RegisterComponent<engine::gameplay::DynamicGeometry>();
	world.RegisterComponent<engine::gameplay::SurfaceLayer>();
	world.RegisterComponent<engine::gameplay::OffMap>();
	world.RegisterComponent<engine::gameplay::Carried>();
	world.RegisterComponent<engine::gameplay::BoundingVolume>();
	world.RegisterComponent<engine::gameplay::Attitude>();
}

// The spatial domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterSpatialSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::SpatialIndexSystem index;
	registry.Register(index);
	static engine::gameplay::DynamicGeometrySystem dynamicGeometries;
	registry.Register(dynamicGeometries);
	static engine::gameplay::SnapshotSystem snapshot;
	registry.Register(snapshot);
}

// What the spatial domain's systems run after (and the few they must precede), within the tick.
inline void OrderSpatialSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// How each player sees each object through the shroud, for the step's targeting (in the spatial index).
	registry.OrderBefore<gameplay::ObjectShroudSystem, gameplay::SpatialIndexSystem>();
	registry.OrderBefore<gameplay::ContainerClassesSystem, gameplay::SpatialIndexSystem>();
	registry.OrderBefore<gameplay::BodyCollisionSystem, gameplay::DynamicGeometrySystem>();
	registry.OrderBefore<gameplay::ParachuteLandingSystem, gameplay::SpatialIndexSystem>();
	registry.OrderBefore<gameplay::DefectorSystem, gameplay::SpatialIndexSystem>();
	// Hiding is settled before the tick's queries.
	registry.OrderBefore<gameplay::StealthSystem, gameplay::SpatialIndexSystem>();
	registry.OrderBefore<domain::SteeringLookSystem, gameplay::SnapshotSystem>();
	registry.OrderBefore<domain::ExtensionLookSystem, gameplay::SnapshotSystem>();
	registry.OrderBefore<domain::PanicLookSystem, gameplay::SnapshotSystem>();
	// Bodies move before this tick's spatial index is built.
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::SpatialIndexSystem>();
}
}
