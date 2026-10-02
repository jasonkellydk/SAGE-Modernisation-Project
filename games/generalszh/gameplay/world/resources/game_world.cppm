export module games.generalszh.gameplay.world.resources.game_world;
import std;

export import engine.ecs.core.world;
export import engine.time.simulation_time;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.resources.name_registry;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.lifecycle.resources.casualties;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.gameplay.teams.resources.team_templates;
export import Engine.Core.Math.FixedRandom;

// The game state that orders, scripts and special powers change between
// ticks: the world and the resources they keep in step. Owned by the session;
// the domain algorithms take it by reference.
export namespace generalszh::gameplay
{
struct GameWorld
{
	ecs::World &world;
	const engine::level::Level &level;
	const engine::time::FixedStep &step;
	engine::gameplay::GroundHeight &ground;
	engine::gameplay::WaypointGraph &waypoints;
	engine::gameplay::TeamRoster &roster;
	engine::gameplay::NameRegistry &names;
	engine::gameplay::CargoManifest &manifest;
	engine::gameplay::KillRequests &kills;
	engine::gameplay::Casualties &casualties;
	ObjectTemplates &templates;
	TeamTemplates &teams;
	// The session's stream for the choices made outside systems (build variations).
	Engine::Math::RandomStream &random;
	const std::uint64_t &tick;
};
}
