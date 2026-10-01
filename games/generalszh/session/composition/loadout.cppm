export module games.generalszh.session.composition.loadout;
import std;
import engine.gameplay.rts.loadout.resources.loadout_catalog;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import engine.gameplay.rts.slaves.systems.spawner_system;
import engine.gameplay.rts.veterancy.systems.veterancy_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.loadout.systems.loadout_system;
import engine.gameplay.rts.loadout.components.loadout;

// The loadout domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The loadout domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceLoadoutResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::LoadoutArmings>();
}

inline void RegisterLoadoutComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Loadout>();
}

// The loadout domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterLoadoutSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::LoadoutSystem loadouts;
	registry.Register(loadouts);
}

// What the loadout domain's systems run after (and the few they must precede), within the tick.
inline void OrderLoadoutSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::ParachuteLossSystem, gameplay::LoadoutSystem>();
	registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::LoadoutSystem>();
	registry.OrderBefore<gameplay::VeterancySystem, gameplay::LoadoutSystem>();
	registry.OrderBefore<gameplay::SpawnerSystem, gameplay::LoadoutSystem>();
}
}
