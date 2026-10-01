export module games.generalszh.session.composition.lifetime;
import std;
import engine.gameplay.common.lifetime.resources.expirations;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import engine.gameplay.common.lifetime.systems.lifetime_system;
import engine.gameplay.common.lifetime.components.lifetime;

// The lifetime domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The lifetime domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceLifetimeResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::Expirations>();
	world.EmplaceResource<engine::gameplay::Deletions>();
}

inline void RegisterLifetimeComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Lifetime>();
}

// The lifetime domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterLifetimeSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::LifetimeSystem lifetime;
	registry.Register(lifetime);
}

// What the lifetime domain's systems run after (and the few they must precede), within the tick.
inline void OrderLifetimeSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::GrantStealthSystem, gameplay::LifetimeSystem>();
}
}
