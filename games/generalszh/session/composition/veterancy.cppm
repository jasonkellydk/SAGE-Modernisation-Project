export module games.generalszh.session.composition.veterancy;
import std;
import engine.gameplay.rts.veterancy.resources.experience_awards;
import engine.gameplay.rts.veterancy.resources.promotions;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.health_system;
import games.generalszh.gameplay.crates.systems.pilot_seek_system;
import engine.gameplay.rts.veterancy.systems.veterancy_system;
import engine.gameplay.rts.veterancy.components.experience;

// The veterancy domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The veterancy domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceVeterancyResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::ExperienceAwards>();
	world.EmplaceResource<engine::gameplay::Promotions>();
}

inline void RegisterVeterancyComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Experience>();
}

// The veterancy domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterVeterancySystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::VeterancySystem veterancy;
	registry.Register(veterancy);
}

// What the veterancy domain's systems run after (and the few they must precede), within the tick.
inline void OrderVeterancySystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::HealthSystem, gameplay::VeterancySystem>();
	registry.OrderBefore<domain::PilotSeekSystem, gameplay::VeterancySystem>();
}
}
