export module games.generalszh.session.composition.blocking;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.blocking.components.blocking_unit;
import engine.gameplay.rts.blocking.components.blocked_state;
import engine.gameplay.rts.blocking.components.block_contact;
import engine.gameplay.rts.blocking.systems.unit_blocking_system;
import engine.gameplay.rts.blocking.systems.blocked_repath_system;
import engine.gameplay.rts.blocking.systems.unit_cell_system;
import engine.gameplay.rts.navigation.resources.unit_cells;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.common.spatial.systems.snapshot_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import engine.gameplay.rts.docking.systems.dock_system;
import games.generalszh.gameplay.movement.systems.destination_adjust_system;
import games.generalszh.gameplay.movement.systems.move_away_system;
import engine.gameplay.rts.blocking.resources.move_away_requests;
import engine.gameplay.rts.movement.components.move_away;

// The blocking domain (ground units held up by the units in their way): its components, systems and their order.
export namespace generalszh::session::composition
{
// Where the ground units stand, for the pathfinder (derived each tick).
inline void EmplaceBlockingResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::UnitCells>();
	world.EmplaceResource<engine::gameplay::UnitCellGather>();
	world.EmplaceResource<engine::gameplay::MoveAwayRequests>();
	world.EmplaceResource<engine::gameplay::UnitSettles>();
}

inline void RegisterBlockingComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::BlockingUnit>();
	world.RegisterComponent<engine::gameplay::BlockedState>();
	world.RegisterComponent<engine::gameplay::BlockContact>();
	world.RegisterComponent<engine::gameplay::MoveAway>();
}

// The blocking domain's systems, registered with the simulation schedule (stateless: one shared instance each).
inline void RegisterBlockingSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::UnitBlockingSystem blocking;
	registry.Register(blocking);
	static engine::gameplay::BlockedRepathSystem repath;
	registry.Register(repath);
	static engine::gameplay::UnitCellSystem unitCells;
	registry.Register(unitCells);
	static generalszh::gameplay::MoveAwaySystem moveAway;
	registry.Register(moveAway);
}

// A unit's move weighs being held up before routes are planned (its AI's state update before the pathfinder's queue);
// the units in its way are weighed once everything moved and the colliders are indexed (the partition's collisions at
// the end of the frame).
inline void OrderBlockingSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::BlockedRepathSystem, gameplay::RouteRequestSystem>();
	registry.OrderBefore<domain::DestinationAdjustSystem, gameplay::BlockedRepathSystem>();
	registry.OrderBefore<gameplay::DockSystem, gameplay::BlockedRepathSystem>();
	// The units in the way weighed last in the tick, as everything was left.
	registry.OrderBefore<gameplay::SnapshotSystem, gameplay::UnitBlockingSystem>();
	// Then the units asked make way.
	registry.OrderBefore<gameplay::UnitBlockingSystem, domain::MoveAwaySystem>();
	// The units' cells once physics has set the bodies down, before the tick's simulation.
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::UnitCellSystem>();
	registry.OrderBefore<gameplay::FreeFallSystem, gameplay::UnitCellSystem>();
}
}
