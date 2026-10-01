export module games.generalszh.session.composition.harvesting;
import std;
import engine.gameplay.rts.harvesting.resources.harvest_catalog;
import engine.gameplay.rts.harvesting.resources.harvest_roster;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.movement.systems.face_target_system;
import engine.gameplay.rts.movement.systems.move_path_system;
import engine.gameplay.rts.movement.systems.wander_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import games.generalszh.gameplay.abilities.systems.command_button_hunt_system;
import games.generalszh.gameplay.ai.systems.attack_squad_system;
import games.generalszh.gameplay.ai.systems.guard_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.ai.systems.tunnel_guard_system;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.crates.systems.crate_touch_system;
import engine.gameplay.rts.harvesting.systems.harvest_roster_system;
import engine.gameplay.rts.harvesting.systems.harvest_system;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.harvesting.components.resource_store;

// The harvesting domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The harvesting domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceHarvestingResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::HarvestRoster>();
	world.EmplaceResource<engine::gameplay::HarvestEvents>();
}

inline void RegisterHarvestingComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Harvester>();
	world.RegisterComponent<engine::gameplay::ResourceStore>();
	world.RegisterComponent<engine::gameplay::ResourceDepot>();
}

// The harvesting domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterHarvestingSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::HarvestRosterSystem harvestRoster;
	registry.Register(harvestRoster);
	static engine::gameplay::HarvestSystem harvest;
	registry.Register(harvest);
}

// What the harvesting domain's systems run after (and the few they must precede), within the tick.
inline void OrderHarvestingSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::RepulsionSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::FaceTargetSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::MovePathSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::WanderSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::AttackSquadSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::TunnelGuardSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::GuardSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::CommandButtonHuntSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::EmpPulseSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::ScorchSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<domain::CrateTouchSystem, gameplay::HarvestSystem>();
	registry.OrderBefore<gameplay::HarvestRosterSystem, gameplay::HarvestSystem>();
}
}
