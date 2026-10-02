export module games.generalszh.session.composition.lifecycle;
import std;
import engine.gameplay.rts.lifecycle.resources.removals;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import games.generalszh.gameplay.production.systems.production_refund_system;
import games.generalszh.gameplay.teams.systems.tech_building_system;
import games.generalszh.gameplay.upgrades.systems.upgrade_effect_system;
import engine.gameplay.rts.lifecycle.systems.removal_system;

export namespace generalszh::session::composition
{
// The lifecycle domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceLifecycleResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::Removals>();
}

// The lifecycle domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterLifecycleSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::RemovalSystem removal;
	registry.Register(removal);
}

// What the lifecycle domain's systems run after (and the few they must precede), within the tick.
inline void OrderLifecycleSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::UpgradeEffectSystem, gameplay::RemovalSystem>();
	registry.OrderBefore<domain::ProductionRefundSystem, gameplay::RemovalSystem>();
	registry.OrderBefore<domain::TechBuildingSystem, gameplay::RemovalSystem>();
}
}
