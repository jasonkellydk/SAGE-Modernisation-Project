export module games.generalszh.session.composition.physics;
import std;
import engine.gameplay.common.physics.resources.fall_damage;
import engine.gameplay.common.physics.resources.landings;
import engine.gameplay.common.physics.resources.physics_settings;
import engine.gameplay.common.physics.resources.shock_waves;
import games.generalszh.content.physics.physics_content;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.aircraft.systems.jet_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import games.generalszh.gameplay.combat_drop.systems.combat_drop_systems;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.common.physics.systems.shock_wave_system;
import engine.gameplay.common.physics.components.bounce_sound;
import engine.gameplay.common.physics.components.physics_body;

// The physics domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The physics domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplacePhysicsResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::PhysicsSettings>(content::ZeroHourPhysicsSettings(setup.content.gameData.gravity));
	world.EmplaceResource<engine::gameplay::FallDamage>();
	world.EmplaceResource<engine::gameplay::ShockWaves>();
	world.EmplaceResource<engine::gameplay::Landings>();
}

inline void RegisterPhysicsComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::PhysicsBody>();
	world.RegisterComponent<engine::gameplay::BounceSound>();
}

// The physics domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterPhysicsSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::PhysicsSystem physics;
	registry.Register(physics);
	static engine::gameplay::ShockWaveSystem shockWaves;
	registry.Register(shockWaves);
	static engine::gameplay::FallingDamageSystem fallingDamage;
	registry.Register(fallingDamage);
}

// What the physics domain's systems run after (and the few they must precede), within the tick.
inline void OrderPhysicsSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// Shock waves push what the tick's impacts reached.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::ShockWaveSystem>();
	registry.OrderBefore<domain::RappelSystem, gameplay::PhysicsSystem>();
	registry.OrderBefore<gameplay::ParachuteSystem, gameplay::PhysicsSystem>();
	registry.OrderBefore<gameplay::FreeFallSystem, gameplay::FallingDamageSystem>();
	registry.OrderBefore<gameplay::StealthRevealSystem, gameplay::ShockWaveSystem>();
	registry.OrderBefore<gameplay::MineDrainSystem, gameplay::FallingDamageSystem>();
	registry.OrderBefore<gameplay::OverchargeDrainSystem, gameplay::FallingDamageSystem>();
	registry.OrderBefore<gameplay::BlastDamageSystem, gameplay::FallingDamageSystem>();
	registry.OrderBefore<gameplay::DescentSystem, gameplay::PhysicsSystem>();
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::FallingDamageSystem>();
	registry.OrderBefore<gameplay::JetDamageSystem, gameplay::FallingDamageSystem>();
}
}
