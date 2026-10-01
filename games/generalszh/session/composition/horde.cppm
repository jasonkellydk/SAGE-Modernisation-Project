export module games.generalszh.session.composition.horde;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.containment.systems.passed_bonus_system;
import engine.gameplay.rts.harvesting.systems.harvest_system;
import engine.gameplay.rts.horde.systems.horde_system;
import engine.gameplay.rts.horde.components.horde;

// The horde domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterHordeComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Horde>();
}

// The horde domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterHordeSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::HordeSystem hordes;
	registry.Register(hordes);
}

// What the horde domain's systems run after (and the few they must precede), within the tick.
inline void OrderHordeSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::PassedBonusSystem, gameplay::HordeSystem>();
	registry.OrderBefore<gameplay::HarvestSystem, gameplay::HordeSystem>();
}
}
