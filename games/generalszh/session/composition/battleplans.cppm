export module games.generalszh.session.composition.battleplans;
import std;
import games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import games.generalszh.gameplay.battleplans.resources.battle_plan_players;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.delivery.systems.delivery_system;
import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.battleplans.systems.battle_plan_system;
import games.generalszh.gameplay.battleplans.components.battle_plan;

// The battleplans domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The battleplans domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceBattleplansResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::BattlePlanPlayers>();
	world.EmplaceResource<generalszh::gameplay::BattlePlanEvents>();
	world.EmplaceResource<generalszh::gameplay::BattlePlanCues>();
}

inline void RegisterBattleplansComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::BattlePlan>();
}

// The battleplans domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterBattleplansSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::BattlePlanSystem battlePlans;
	registry.Register(battlePlans);
}

// What the battleplans domain's systems run after (and the few they must precede), within the tick.
inline void OrderBattleplansSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::DeliverySystem, domain::BattlePlanSystem>();
	registry.OrderBefore<domain::InternetHackSystem, domain::BattlePlanSystem>();
}
}
