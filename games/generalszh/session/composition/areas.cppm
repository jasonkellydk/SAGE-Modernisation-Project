export module games.generalszh.session.composition.areas;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.areas.systems.area_presence_system;
import engine.gameplay.common.areas.components.area_presence;

// The areas domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterAreasComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::AreaPresence>();
}

// The areas domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterAreasSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::AreaPresenceSystem areaPresence;
	registry.Register(areaPresence);
}
}
