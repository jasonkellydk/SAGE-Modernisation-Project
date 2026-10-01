export module games.generalszh.session.composition.vision;
import std;
import engine.gameplay.rts.vision.resources.shroud_map;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.rts.death.systems.death_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.vision.systems.dynamic_clearing_system;
import engine.gameplay.rts.vision.systems.object_shroud_system;
import engine.gameplay.rts.vision.systems.vision_system;
import engine.gameplay.rts.vision.components.dynamic_clearing;
import engine.gameplay.rts.vision.components.partition_footprint;
import engine.gameplay.rts.vision.components.vision;

// The vision domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The vision domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceVisionResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ShroudMap>();
}

inline void RegisterVisionComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Vision>();
	world.RegisterComponent<engine::gameplay::PartitionFootprint>();
	world.RegisterComponent<engine::gameplay::VisionSpies>();
	world.RegisterComponent<engine::gameplay::DynamicClearing>();
}

// The vision domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterVisionSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::VisionSystem vision;
	registry.Register(vision);
	static engine::gameplay::ObjectShroudSystem objectShroud;
	registry.Register(objectShroud);
	static engine::gameplay::DynamicClearingSystem dynamicClearings;
	registry.Register(dynamicClearings);
}

// What the vision domain's systems run after (and the few they must precede), within the tick.
inline void OrderVisionSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::DeathSystem, gameplay::VisionSystem>();
	registry.OrderBefore<gameplay::RemovalSystem, gameplay::VisionSystem>();
	// From where the pre-step movers leave things (the original asks with current positions).
	registry.OrderBefore<gameplay::DescentSystem, gameplay::ObjectShroudSystem>();
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ObjectShroudSystem>();
}
}
