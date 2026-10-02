export module games.generalszh.session.composition.parachute;
import std;
import engine.gameplay.rts.parachute.resources.parachute_openings;
import engine.gameplay.rts.parachute.resources.parachute_landings;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.physics.systems.physics_system;
import engine.gameplay.common.status.systems.disable_systems;
import engine.gameplay.rts.death.systems.death_system;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.rts.parachute.components.parachute;

// The parachute domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The parachute domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceParachuteResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ParachuteOpenings>();
	world.EmplaceResource<engine::gameplay::ParachuteLandings>();
}

inline void RegisterParachuteComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Parachute>();
	world.RegisterComponent<engine::gameplay::ParachuteRider>();
}

// The parachute domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterParachuteSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::ParachuteSystem parachutes;
	registry.Register(parachutes);
	static engine::gameplay::ParachuteLandingSystem parachuteLandings;
	registry.Register(parachuteLandings);
	static engine::gameplay::FreeFallSystem freeFalls;
	registry.Register(freeFalls);
	static engine::gameplay::ParachuteLossSystem parachuteLosses;
	registry.Register(parachuteLosses);
}

// What the parachute domain's systems run after (and the few they must precede), within the tick.
inline void OrderParachuteSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	// A parachute's locomotor drives it before physics steps it; it lands, opens and places its rider after, before the
	// tick's queries.
	registry.OrderBefore<gameplay::DescentSystem, gameplay::ParachuteSystem>();
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::ParachuteLandingSystem>();
	// Free fall once physics has moved the body; a lost chute's rider let go after the tick's damage; queued damage
	// with the tick's impacts.
	registry.OrderBefore<gameplay::PhysicsSystem, gameplay::FreeFallSystem>();
	registry.OrderBefore<gameplay::ParachuteLandingSystem, gameplay::FreeFallSystem>();
	registry.OrderBefore<gameplay::DeathSystem, gameplay::ParachuteLossSystem>();
	registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::ParachuteSystem>();
	registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::ParachuteLandingSystem>();
	registry.OrderBefore<gameplay::DisableExpirySystem, gameplay::FreeFallSystem>();
}
}
