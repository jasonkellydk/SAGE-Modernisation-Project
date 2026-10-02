export module games.generalszh.session.composition.railroad;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import engine.gameplay.rts.containment.systems.unloading_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.rts.harvesting.systems.harvest_roster_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.slaves.systems.hive_damage_system;
import games.generalszh.gameplay.ai.systems.repulsion_system;
import games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import games.generalszh.gameplay.crates.systems.crate_touch_system;
import games.generalszh.gameplay.railroad.systems.railroad_collision_system;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.gameplay.railroad.components.railcar;

// The railroad domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterRailroadComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::Railcar>();
}

// The railroad domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterRailroadSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::RailroadSystem railroad;
	registry.Register(railroad);
	static generalszh::gameplay::RailroadCollisionSystem railroadCollisions;
	registry.Register(railroadCollisions);
}

// What the railroad domain's systems run after (and the few they must precede), within the tick.
inline void OrderRailroadSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::HarvestRosterSystem, domain::RailroadSystem>();
	registry.OrderBefore<domain::CrateTouchSystem, domain::RailroadSystem>();
	registry.OrderBefore<gameplay::AutoDepositSystem, domain::RailroadSystem>();
	registry.OrderBefore<domain::RepulsionSystem, domain::RailroadSystem>();
	registry.OrderBefore<gameplay::ScorchSystem, domain::RailroadSystem>();
	registry.OrderBefore<domain::ScriptedEvacuationSystem, domain::RailroadSystem>();
	registry.OrderBefore<gameplay::PropagandaScanSystem, domain::RailroadSystem>();
	registry.OrderBefore<gameplay::BlastWaveSystem, domain::RailroadSystem>();
	registry.OrderBefore<gameplay::GarrisonKillDamageSystem, domain::RailroadCollisionSystem>();
	registry.OrderBefore<gameplay::HiveDamageSystem, domain::RailroadCollisionSystem>();
	registry.OrderBefore<gameplay::PendingDamageSystem, domain::RailroadCollisionSystem>();
	registry.OrderBefore<gameplay::JetSystem, domain::RailroadCollisionSystem>();
	registry.OrderBefore<gameplay::UnloadingSystem, domain::RailroadCollisionSystem>();
}
}
