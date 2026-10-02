export module games.generalszh.session.composition.match;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.death.systems.mount_system;
import engine.gameplay.rts.match.systems.victory_system;
import engine.gameplay.rts.match.components.victory_role;

// The match domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterMatchComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::VictoryRole>();
}

// The match domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterMatchSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::VictorySystem victory;
	registry.Register(victory);
}

// What the match domain's systems run after (and the few they must precede), within the tick.
inline void OrderMatchSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::MountSystem, gameplay::VictorySystem>();
}
}
