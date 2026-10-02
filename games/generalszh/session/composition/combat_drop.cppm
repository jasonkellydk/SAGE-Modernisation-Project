export module games.generalszh.session.composition.combat_drop;
import std;
import games.generalszh.gameplay.combat_drop.resources.deferred_orders;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.status.systems.disable_systems;
import engine.gameplay.rts.movement.systems.descent_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import games.generalszh.gameplay.combat_drop.systems.combat_drop_systems;
import games.generalszh.gameplay.combat_drop.components.combat_drop;

// The combat drop domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The combat drop domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceCombatDropResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::RopesInUse>();
	world.EmplaceResource<generalszh::gameplay::RappelLandings>();
	world.EmplaceResource<generalszh::gameplay::DeferredOrders>();
}

inline void RegisterCombatDropComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::CombatDrop>();
	world.RegisterComponent<generalszh::gameplay::OnRope>();
	world.RegisterComponent<generalszh::gameplay::Rappel>();
	world.RegisterComponent<generalszh::gameplay::FallingRope>();
}

// The combat drop domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterCombatDropSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::RopeHangerSystem ropeHangers;
	registry.Register(ropeHangers);
	static generalszh::gameplay::CombatDropSystem combatDrops;
	registry.Register(combatDrops);
	static generalszh::gameplay::RappelSystem rappels;
	registry.Register(rappels);
}

// What the combat drop domain's systems run after (and the few they must precede), within the tick.
inline void OrderCombatDropSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	// The combat drops (ChinookAIUpdate / AIRappelState), before physics steps what they hold.
	registry.OrderBefore<gameplay::ParachuteSystem, domain::RopeHangerSystem>();
	registry.OrderBefore<gameplay::DescentSystem, domain::RopeHangerSystem>();
	registry.OrderBefore<gameplay::DisableExpirySystem, domain::CombatDropSystem>();
	registry.OrderBefore<gameplay::ParachuteSystem, domain::RappelSystem>();
	registry.OrderBefore<gameplay::DescentSystem, domain::RappelSystem>();
	registry.OrderBefore<domain::RopeHangerSystem, domain::CombatDropSystem>();
	registry.OrderBefore<domain::CombatDropSystem, domain::RappelSystem>();
}
}
