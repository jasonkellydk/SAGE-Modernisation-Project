export module games.generalszh.session.composition.slaves;
import std;
import engine.gameplay.rts.slaves.resources.slave_orders;
import engine.gameplay.rts.slaves.resources.spawn_requests;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.area_healing_system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.death.systems.death_system;
import engine.gameplay.rts.docking.systems.dock_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.movement.systems.locomotor_damage_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import games.generalszh.gameplay.powers.systems.particle_cannon_system;
import engine.gameplay.rts.slaves.systems.disable_follow_system;
import engine.gameplay.rts.slaves.systems.hive_damage_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.slaves.systems.spawner_system;
import engine.gameplay.rts.slaves.components.hive_body;
import engine.gameplay.rts.slaves.components.slaved;
import engine.gameplay.rts.slaves.components.spawn_points;
import engine.gameplay.rts.slaves.components.spawner;

// The slaves domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The slaves domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceSlavesResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::SpawnRequests>();
	world.EmplaceResource<engine::gameplay::SlaveOrders>();
}

inline void RegisterSlavesComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Slaved>();
	world.RegisterComponent<engine::gameplay::Spawner>();
	world.RegisterComponent<engine::gameplay::SpawnPoints>();
	world.RegisterComponent<engine::gameplay::HiveBody>();
}

// The slaves domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterSlavesSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::SlavedSystem slaves;
	registry.Register(slaves);
	static engine::gameplay::SlaveRepairSystem slaveRepairs;
	registry.Register(slaveRepairs);
	static engine::gameplay::DisableFollowSystem disableFollow;
	registry.Register(disableFollow);
	static engine::gameplay::HiveDamageSystem hiveDamage;
	registry.Register(hiveDamage);
	static engine::gameplay::SpawnerSystem spawners;
	registry.Register(spawners);
}

// What the slaves domain's systems run after (and the few they must precede), within the tick.
inline void OrderSlavesSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::SlavedSystem, gameplay::SlaveRepairSystem>();
	registry.OrderBefore<gameplay::AreaHealingSystem, gameplay::SlaveRepairSystem>();
	registry.OrderBefore<gameplay::SubdualSystem, gameplay::DisableFollowSystem>();
	registry.OrderBefore<gameplay::HordeSystem, gameplay::SlavedSystem>();
	registry.OrderBefore<gameplay::LocomotorDamageSystem, gameplay::SlavedSystem>();
	registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::SpawnerSystem>();
	registry.OrderBefore<domain::ParticleCannonSystem, gameplay::HiveDamageSystem>();
	registry.OrderBefore<gameplay::PendingDamageSystem, gameplay::HiveDamageSystem>();
	registry.OrderBefore<gameplay::JetSystem, gameplay::HiveDamageSystem>();
	registry.OrderBefore<gameplay::DisableFollowSystem, gameplay::SpawnerSystem>();
	registry.OrderBefore<gameplay::DockSystem, gameplay::SlavedSystem>();
	// Spawners see this tick's deaths (their own and their spawns').
	registry.OrderBefore<gameplay::DeathSystem, gameplay::SpawnerSystem>();
}
}
