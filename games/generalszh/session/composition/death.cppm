export module games.generalszh.session.composition.death;
import std;
import engine.gameplay.rts.death.resources.blast_waves;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.areas.systems.area_presence_system;
import engine.gameplay.rts.collision.systems.collision_systems;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.containment.systems.cargo_transfer_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.match.systems.victory_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.rts.slaves.systems.spawner_system;
import engine.gameplay.rts.veterancy.systems.veterancy_system;
import engine.gameplay.rts.vision.systems.vision_system;
import games.generalszh.gameplay.powers.systems.spy_vision_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.death.systems.crash_collision_system;
import engine.gameplay.rts.death.systems.death_system;
import engine.gameplay.rts.death.systems.height_die_system;
import engine.gameplay.rts.death.systems.mount_system;
import engine.gameplay.rts.death.systems.slow_death_system;
import engine.gameplay.rts.death.systems.structure_topple_system;
import engine.gameplay.rts.death.components.blast_wave;
import engine.gameplay.rts.death.components.collapse;
import engine.gameplay.rts.death.components.crash;
import engine.gameplay.rts.death.components.death_credit;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.death.components.height_die;
import engine.gameplay.rts.death.components.structure_topple;

// The death domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The death domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceDeathResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ParticleClears>();
	world.EmplaceResource<engine::gameplay::BlastWaves>();
}

inline void RegisterDeathComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::HeightDie>();
	world.RegisterComponent<engine::gameplay::DeathCredit>();
	world.RegisterComponent<engine::gameplay::Mortality>();
	world.RegisterComponent<engine::gameplay::Dying>();
	world.RegisterComponent<engine::gameplay::Crash>();
	world.RegisterComponent<engine::gameplay::Collapse>();
	world.RegisterComponent<engine::gameplay::StructureTopple>();
	world.RegisterComponent<engine::gameplay::ScriptedTopple>();
	world.RegisterComponent<engine::gameplay::BlastWave>();
	world.RegisterComponent<engine::gameplay::Scorched>();
}

// The death domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterDeathSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::HeightDieSystem heightDies;
	registry.Register(heightDies);
	static engine::gameplay::CrashCollisionSystem crashCollisions;
	registry.Register(crashCollisions);
	static engine::gameplay::BlastWaveSystem blastWaves;
	registry.Register(blastWaves);
	static engine::gameplay::BlastDamageSystem blastDamage;
	registry.Register(blastDamage);
	static engine::gameplay::ScorchSystem scorch;
	registry.Register(scorch);
	static engine::gameplay::MountSystem mounts;
	registry.Register(mounts);
	static engine::gameplay::DeathSystem death;
	registry.Register(death);
	static engine::gameplay::SlowDeathSystem slowDeath;
	registry.Register(slowDeath);
	static engine::gameplay::StructureToppleSystem structureTopples;
	registry.Register(structureTopples);
}

// What the death domain's systems run after (and the few they must precede), within the tick.
inline void OrderDeathSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::VeterancySystem, gameplay::DeathSystem>();
	// A spiralling wreck's collisions after it moves, with this tick's colliders.
	registry.OrderBefore<gameplay::SlowDeathSystem, gameplay::CrashCollisionSystem>();
	registry.OrderBefore<gameplay::ColliderIndexSystem, gameplay::CrashCollisionSystem>();
	registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::SlowDeathSystem>();
	registry.OrderBefore<gameplay::VisionSystem, gameplay::CrashCollisionSystem>();
	registry.OrderBefore<domain::SpyVisionSystem, gameplay::SlowDeathSystem>();
	registry.OrderBefore<gameplay::AreaPresenceSystem, gameplay::HeightDieSystem>();
	registry.OrderBefore<gameplay::AreaPresenceSystem, gameplay::SlowDeathSystem>();
	// Mounted things ride on and die with their carriers, with the tick's deaths.
	registry.OrderBefore<gameplay::MountSystem, gameplay::DeathSystem>();
	registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::BlastDamageSystem>();
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::BlastDamageSystem>();
	registry.OrderBefore<gameplay::VictorySystem, gameplay::DeathSystem>();
	// Things that hit the ground die with the tick's deaths, after the match looked at who is left.
	registry.OrderBefore<gameplay::VictorySystem, gameplay::HeightDieSystem>();
	// What was let go this tick falls from where it was put; its height death joins this tick's deaths.
	registry.OrderBefore<gameplay::CargoTransferSystem, gameplay::HeightDieSystem>();
	registry.OrderBefore<gameplay::HeightDieSystem, gameplay::DeathSystem>();
	registry.OrderBefore<gameplay::SpawnerSystem, gameplay::SlowDeathSystem>();
}
}
