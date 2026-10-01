export module games.generalszh.session.composition.creation;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.combat.systems.neutron_flight_system;
import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.gameplay.creation.systems.ocl_timer_system;
import games.generalszh.gameplay.creation.components.ocl_timer;

// The creation domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The creation domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceCreationResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::OclTimerEvents>();
}

inline void RegisterCreationComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::OclTimer>();
}

// The creation domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterCreationSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::OclTimerSystem oclTimers;
	registry.Register(oclTimers);
}

// What the creation domain's systems run after (and the few they must precede), within the tick.
inline void OrderCreationSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::RailroadSystem, domain::OclTimerSystem>();
	registry.OrderBefore<domain::StickyBombSystem, domain::OclTimerSystem>();
	registry.OrderBefore<gameplay::NeutronFlightSystem, domain::OclTimerSystem>();
}
}
