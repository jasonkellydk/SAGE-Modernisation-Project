export module games.generalszh.session.composition.scripts;
import std;

export import engine.ecs.core.world;
import games.generalszh.gameplay.scripts.components.emptied_watch;

// The scripts domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterScriptsComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::EmptiedWatch>();
}
}
