export module games.generalszh.gameplay.topple.algorithms.topple_stumps;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.death.components.blast_wave;
import engine.gameplay.rts.topple.resources.topple_events;
import games.generalszh.content.topple.topple_content;
import games.generalszh.gameplay.objects.algorithms.object_factory;

// ToppleUpdate::applyTopplingForce: a felled tree leaves its stump where it stood (burned if the tree was).
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// A felled tree leaves its stump standing where it stood (ToppleUpdate::applyTopplingForce: burned if the tree was).
inline void LeaveToppleStumps(GameWorld &game)
{
	game.world.Resource<gp::ToppleEvents>().ForEach([&](const gp::ToppleEvent &event) {
		if (event.kind != gp::ToppleEvent::Kind::Started || !game.world.IsAlive(event.entity))
			return;
		const auto *definition = game.world.Get<gp::DefinitionRef>(event.entity);
		if (definition == nullptr)
			return;
		const auto topple = content::ReadObjectTopple(game.templates.DefinitionAt(definition->index));
		if (!topple || topple->stump.empty())
			return;
		const auto *member = game.world.Get<gp::TeamMember>(event.entity);
		const ecs::Entity stump = SpawnObject(game, topple->stump, event.position.XY(), event.facing, member != nullptr ? member->team : 0u, "");
		if (game.world.Get<gp::Scorched>(event.entity) != nullptr && game.world.IsAlive(stump) && game.world.Get<gp::Scorched>(stump) == nullptr)
			game.world.Add<gp::Scorched>(stump);
	});
}
}
