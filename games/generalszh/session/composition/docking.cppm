export module games.generalszh.session.composition.docking;
import std;
import engine.gameplay.rts.docking.resources.dock_repairs;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.harvesting.systems.harvest_system;
import engine.gameplay.rts.movement.systems.face_target_system;
import engine.gameplay.rts.movement.systems.locomotor_damage_system;
import engine.gameplay.rts.movement.systems.move_path_system;
import engine.gameplay.rts.movement.systems.wander_system;
import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.docking.systems.repair_dock_system;
import engine.gameplay.rts.docking.components.dock;
import engine.gameplay.rts.docking.components.dock_look;
import engine.gameplay.rts.docking.components.docking;
import engine.gameplay.rts.docking.components.repair_dock;

// The docking domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The docking domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceDockingResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::DockRepairs>();
}

inline void RegisterDockingComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Dock>();
	world.RegisterComponent<engine::gameplay::DockLook>();
	world.RegisterComponent<engine::gameplay::Docking>();
	world.RegisterComponent<engine::gameplay::RepairDock>();
}

// The docking domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterDockingSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::DockSystem docks;
	registry.Register(docks);
	static engine::gameplay::RepairDockSystem repairDocks;
	registry.Register(repairDocks);
}

// What the docking domain's systems run after (and the few they must precede), within the tick.
inline void OrderDockingSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::InternetHackSystem, gameplay::DockSystem>();
	registry.OrderBefore<domain::RepulsionSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::MovePathSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::WanderSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::RepairDockSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::DockSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::RepairDockSystem, gameplay::DockSystem>();
	registry.OrderBefore<gameplay::HarvestSystem, gameplay::RepairDockSystem>();
	registry.OrderBefore<gameplay::HarvestSystem, gameplay::DockSystem>();
}
}
