export module games.generalszh.session.composition.production;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.containment.systems.rider_regen_system;
import engine.gameplay.rts.slaves.systems.spawner_system;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import engine.gameplay.rts.production.systems.production_system;
import games.generalszh.gameplay.production.systems.drone_repair_system;
import games.generalszh.gameplay.production.systems.production_refund_system;
import games.generalszh.gameplay.production.systems.production_allowance_system;
import engine.gameplay.rts.production.components.production_doors;
import engine.gameplay.rts.production.components.production_exit_gate;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.production.components.rally_point;
import games.generalszh.gameplay.production.components.cost_modifying;

// The production domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The production domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceProductionResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ProductionDone>();
}

inline void RegisterProductionComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::CostModifying>();
	world.RegisterComponent<engine::gameplay::RallyPoint>();
	world.RegisterComponent<engine::gameplay::ProductionQueue>();
	world.RegisterComponent<engine::gameplay::ProductionDoors>();
	world.RegisterComponent<engine::gameplay::ProductionExitGate>();
}

// The production domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterProductionSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::ProductionRefundSystem productionRefunds;
	registry.Register(productionRefunds);
	static generalszh::gameplay::ProductionAllowanceSystem productionAllowance;
	registry.Register(productionAllowance);
	static engine::gameplay::ProductionSystem production;
	registry.Register(production);
	static generalszh::gameplay::DroneRepairSystem droneRepairs;
	registry.Register(droneRepairs);
}

// What the production domain's systems run after (and the few they must precede), within the tick.
inline void OrderProductionSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::ProductionRefundSystem, gameplay::ProductionSystem>();
	registry.OrderBefore<domain::ProductionRefundSystem, domain::ProductionAllowanceSystem>();
	registry.OrderBefore<domain::ProductionAllowanceSystem, gameplay::ProductionSystem>();
	registry.OrderBefore<gameplay::SpawnerSystem, gameplay::ProductionSystem>();
	registry.OrderBefore<gameplay::RiderRegenSystem, domain::DroneRepairSystem>();
	registry.OrderBefore<domain::HealSeekSystem, domain::DroneRepairSystem>();
	registry.OrderBefore<gameplay::ConstructionSystem, domain::DroneRepairSystem>();
}
}
