export module games.generalszh.session.composition.hacking;
import std;
import games.generalszh.gameplay.hacking.resources.hack_cues;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.hacking.components.internet_hack;

// The hacking domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The hacking domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceHackingResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::HackEvents>();
	world.EmplaceResource<generalszh::gameplay::HackCues>();
}

inline void RegisterHackingComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::InternetHack>();
}

// The hacking domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterHackingSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::InternetHackSystem internetHacks;
	registry.Register(internetHacks);
}
}
