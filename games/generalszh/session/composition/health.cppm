export module games.generalszh.session.composition.health;
import std;
import engine.gameplay.common.health.resources.incoming_damage;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.fire.systems.flammability_system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.collision.systems.crush_system;
import engine.gameplay.rts.combat.systems.countermeasures_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.slaves.systems.hive_damage_system;
import games.generalszh.gameplay.powers.systems.particle_cannon_system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.health.systems.pending_damage_system;
import engine.gameplay.common.health.systems.status_damage_system;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.common.health.components.damage_scalar;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.health_floor;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.common.health.components.pending_damage;
import engine.gameplay.common.health.components.second_life;
import engine.gameplay.common.health.components.subdual;

// The health domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The health domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceHealthResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::IncomingDamage>();
	world.EmplaceResource<engine::gameplay::Deaths>();
	world.EmplaceResource<engine::gameplay::Hits>();
	world.EmplaceResource<engine::gameplay::SecondLives>();
	world.EmplaceResource<engine::gameplay::SubdualChanges>();
}

inline void RegisterHealthComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::InactiveBody>();
	world.RegisterComponent<engine::gameplay::Health>();
	world.RegisterComponent<engine::gameplay::HealthFloor>();
	world.RegisterComponent<engine::gameplay::DamageScalar>();
	world.RegisterComponent<engine::gameplay::Subdual>();
	world.RegisterComponent<engine::gameplay::SecondLife>();
	world.RegisterComponent<engine::gameplay::PendingDamage>();
}

// The health domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterHealthSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::HealthSystem health;
	registry.Register(health);
	static engine::gameplay::SubdualSystem subdual;
	registry.Register(subdual);
	static engine::gameplay::StatusDamageSystem statusDamage;
	registry.Register(statusDamage);
	static engine::gameplay::PendingDamageSystem pendingDamage;
	registry.Register(pendingDamage);
}

// What the health domain's systems run after (and the few they must precede), within the tick.
inline void OrderHealthSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::GarrisonKillDamageSystem, gameplay::HealthSystem>();
	// Subdual damage shed and weighed after the tick's damage, its disables given with the others.
	registry.OrderBefore<gameplay::HealthSystem, gameplay::SubdualSystem>();
	// Statuses from STATUS damage given and healed after the tick's damage.
	registry.OrderBefore<gameplay::HealthSystem, gameplay::StatusDamageSystem>();
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::FallingDamageSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::CrushSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::MineDrainSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::CountermeasuresSystem, gameplay::HealthSystem>();
	registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::PendingDamageSystem>();
	registry.OrderBefore<domain::ParticleCannonSystem, gameplay::HealthSystem>();
	registry.OrderBefore<gameplay::HiveDamageSystem, gameplay::HealthSystem>();
	registry.OrderBefore<gameplay::JetDamageSystem, gameplay::HealthSystem>();
}
}
