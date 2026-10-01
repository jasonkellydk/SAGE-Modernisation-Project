export module games.generalszh.session.composition.poison;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.fire.systems.flammability_system;
import engine.gameplay.common.poison.systems.poison_system;
import engine.gameplay.common.poison.components.poison;

// The poison domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterPoisonComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Poison>();
}

// The poison domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterPoisonSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::PoisonSystem poison;
	registry.Register(poison);
}

// What the poison domain's systems run after (and the few they must precede), within the tick.
inline void OrderPoisonSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::FlammabilitySystem, gameplay::PoisonSystem>();
}
}
