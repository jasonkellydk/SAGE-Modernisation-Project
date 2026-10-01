export module games.generalszh.session.composition.teams;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.death.systems.death_system;
import games.generalszh.gameplay.teams.systems.tech_building_system;
import games.generalszh.gameplay.teams.components.tech_building;

// The teams domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterTeamsComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::TechBuilding>();
}

// The teams domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterTeamsSystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::TechBuildingSystem techBuildings;
	registry.Register(techBuildings);
}

// What the teams domain's systems run after (and the few they must precede), within the tick.
inline void OrderTeamsSystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<gameplay::DeathSystem, domain::TechBuildingSystem>();
}
}
