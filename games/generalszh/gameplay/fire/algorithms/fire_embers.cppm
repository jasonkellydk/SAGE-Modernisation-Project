export module games.generalszh.gameplay.fire.algorithms.fire_embers;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.death.resources.death_events;
import Engine.Core.Math.Matrix3;
import engine.gameplay.common.fire.resources.ignitions;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import games.generalszh.content.fire.fire_content;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;

// FlammableUpdate's spreading fire (OCLEmbers): where this tick's fire spread, its embers' creation list runs.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// Spreading fire throws its embers (OCLEmbers) where it burns.
inline void ThrowFireEmbers(GameWorld &game)
{
	game.world.Resource<gp::SpreadTries>().ForEach([&](const gp::SpreadTry &spread) {
		const auto *definition = game.world.IsAlive(spread.entity) ? game.world.Get<gp::DefinitionRef>(spread.entity) : nullptr;
		if (definition == nullptr)
			return;
		const auto fire = content::ReadObjectFireSpread(game.templates.DefinitionAt(definition->index), game.step);
		if (!fire || fire->embers.empty())
			return;
		const auto *member = game.world.Get<gp::TeamMember>(spread.entity);
		const auto facing = game.world.Get<gp::Transform>(spread.entity)->facing;
		RunCreationList(game, fire->embers, {spread.position, facing, member != nullptr ? member->team : gp::NoTeam});
	});
}
}
