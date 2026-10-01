export module games.generalszh.session.composition.navigation;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.navigation.systems.obstacle_system;
import engine.gameplay.rts.navigation.components.ignored_obstacle;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.components.pathfind_goal;

// The navigation domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterNavigationComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::NavigationAgent>();
	world.RegisterComponent<engine::gameplay::NavigationObstacle>();
	world.RegisterComponent<engine::gameplay::Route>();
	world.RegisterComponent<engine::gameplay::IgnoredObstacle>();
	world.RegisterComponent<engine::gameplay::PathfindGoal>();
}

// The navigation domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterNavigationSystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::ObstacleSystem obstacles;
	registry.Register(obstacles);
}
}
