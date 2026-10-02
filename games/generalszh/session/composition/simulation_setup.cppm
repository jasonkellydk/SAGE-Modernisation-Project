export module games.generalszh.session.composition.simulation_setup;
import std;

export import engine.ecs.core.world;
export import engine.level.model.level;
export import Engine.Core.Math.FixedVector;
export import engine.time.simulation_time;
export import games.generalszh.content.loading.game_content;

// What the simulation's domains are composed from (the session's composition): the game's content, the level, the
// tick's length, the match's seed and the playable area's size.
export namespace generalszh::session::composition
{
struct SimulationSetup
{
	const content::GameContent &content;
	const engine::level::Level &level;
	engine::time::FixedStep step;
	std::uint64_t seed{0};
	Engine::Math::FixedVector2 playable;
};
}
