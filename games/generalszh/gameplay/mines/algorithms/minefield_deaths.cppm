export module games.generalszh.gameplay.mines.algorithms.minefield_deaths;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.lifecycle.resources.casualties;
import games.generalszh.content.mines.mine_content;
import games.generalszh.gameplay.mines.algorithms.minefields;

// GenerateMinefieldBehavior::onDie (GenerateOnlyOnDeath): those killed this tick lay their minefields where they were.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// GenerateMinefieldBehavior::onDie (GenerateOnlyOnDeath): those that died this tick lay their minefields where they were.
inline void LayDeathMinefields(GameWorld &game)
{
	for (const gp::Casualty &casualty : game.casualties.list)
	{
		if (casualty.departure != gp::Departure::Killed)
			continue;
		const auto generator = content::ReadMinefieldGenerator(game.templates.DefinitionAt(casualty.definition), game.templates.Content().gameData);
		if (generator && generator->onDeath)
			PlaceMines(game, {casualty.entity, casualty.definition, casualty.transform, casualty.team}, false);
	}
}
}
