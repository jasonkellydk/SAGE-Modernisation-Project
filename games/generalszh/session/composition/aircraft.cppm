export module games.generalszh.session.composition.aircraft;
import std;
import engine.gameplay.rts.aircraft.resources.jet_damage;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.death.systems.crash_collision_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.emp.systems.emp_pulse_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.mines.systems.demo_trap_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.movement.systems.route_request_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import games.generalszh.gameplay.battleplans.systems.battle_plan_system;
import games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import games.generalszh.gameplay.crates.systems.crate_touch_system;
import games.generalszh.gameplay.flight_deck.systems.flight_deck_systems;
import engine.gameplay.rts.aircraft.systems.jet_system;
import games.generalszh.gameplay.aircraft.systems.airfield_heal_system;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.rts.aircraft.components.jet;
import games.generalszh.gameplay.aircraft.components.airfield_healing;

// The aircraft domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The aircraft domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceAircraftResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::JetDamage>();
	world.EmplaceResource<generalszh::gameplay::AirfieldHeals>();
}

inline void RegisterAircraftComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::AirfieldHealing>();
	world.RegisterComponent<engine::gameplay::Airfield>();
	world.RegisterComponent<engine::gameplay::Jet>();
}

// The aircraft domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterAircraftSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::JetDamageSystem jetDamage;
	registry.Register(jetDamage);
	static generalszh::gameplay::AirfieldHealSystem airfieldHeals;
	registry.Register(airfieldHeals);
	static engine::gameplay::JetSystem jets;
	registry.Register(jets);
}

// What the aircraft domain's systems run after (and the few they must precede), within the tick.
inline void OrderAircraftSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::SlavedSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::HordeSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::CrashCollisionSystem, domain::AirfieldHealSystem>();
	registry.OrderBefore<domain::DeckRosterSystem, domain::AirfieldHealSystem>();
	registry.OrderBefore<domain::BattlePlanSystem, gameplay::JetSystem>();
	registry.OrderBefore<domain::CooldownCreationSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::AutoDepositSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::EmpPulseSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::DemoTrapSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::MineDrainSystem, gameplay::JetDamageSystem>();
	registry.OrderBefore<gameplay::PropagandaScanSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::JetDamageSystem>();
	registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::ScorchSystem, gameplay::JetSystem>();
	registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::JetDamageSystem>();
	registry.OrderBefore<domain::CrateTouchSystem, gameplay::JetSystem>();
	// Jets take over their own movement after the containers have unloaded theirs.
	registry.OrderBefore<gameplay::UnloadingSystem, gameplay::JetSystem>();
	// Jets circling a dead airfield hurt after the jets' step and the tick's impacts, before falls.
	registry.OrderBefore<gameplay::JetSystem, gameplay::JetDamageSystem>();
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::JetDamageSystem>();
}
}
