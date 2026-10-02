export module games.generalszh.session.composition.topple;
import std;
import engine.gameplay.rts.topple.resources.topple_events;
import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.death.systems.blast_wave_system;
import engine.gameplay.rts.topple.systems.topple_system;
import engine.gameplay.rts.topple.components.topple;

// The topple domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The topple domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceToppleResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ToppleSettings>(engine::gameplay::ToppleSettings{*content::DeathTypeIndex("TOPPLED")});
	world.EmplaceResource<engine::gameplay::ToppleEvents>();
	world.EmplaceResource<engine::gameplay::TopplePushes>();
}

inline void RegisterToppleComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Topple>();
}

// The topple domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterToppleSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::ToppleSystem topple;
	registry.Register(topple);
}

// What the topple domain's systems run after (and the few they must precede), within the tick.
inline void OrderToppleSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	// A missile's blasts push things over before the tick's toppling; their damage joins the tick's after the impacts.
	registry.OrderBefore<gameplay::BlastWaveSystem, gameplay::ToppleSystem>();
}
}
