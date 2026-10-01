export module games.generalszh.session.composition.propaganda;
import std;
import engine.gameplay.rts.propaganda.resources.propaganda_scans;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.healing.systems.area_healing_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.propaganda.systems.propaganda_system;
import engine.gameplay.rts.propaganda.components.propaganda;

// The propaganda domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The propaganda domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplacePropagandaResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::PropagandaScans>();
	world.EmplaceResource<engine::gameplay::PropagandaHeals>();
}

inline void RegisterPropagandaComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::PropagandaTower>();
	world.RegisterComponent<engine::gameplay::PropagandaInfluence>();
}

// The propaganda domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterPropagandaSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::PropagandaScanSystem propagandaScan;
	registry.Register(propagandaScan);
	static engine::gameplay::PropagandaInfluenceSystem propagandaInfluence;
	registry.Register(propagandaInfluence);
}

// What the propaganda domain's systems run after (and the few they must precede), within the tick.
inline void OrderPropagandaSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::SlaveRepairSystem, gameplay::PropagandaInfluenceSystem>();
	// Propaganda heals join the tick's heal pulses once area healing has gathered them, before they are applied.
	registry.OrderBefore<gameplay::AreaHealingSystem, gameplay::PropagandaInfluenceSystem>();
}
}
