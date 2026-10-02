export module games.generalszh.gameplay.orders.algorithms.view_targets;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.production.algorithms.build_cost;
import engine.gameplay.common.identity.components.definition_ref;

// What CommandXlat's view keys pick among the local player's objects, walked as Player::iterateObjects does (team by
// team, each team's newest member first): read only, for the client.
// - viewCommandCenter (VIEW_COMMAND_CENTER, H) with findCommandCenterOrMostExpensiveBuilding: every command center
//   seen replaces the place (the last one walked wins); until one is seen, a structure costing more to build
//   (calcCostToBuild for the player) than the best so far does; other objects are skipped.
// - iNeedAHero (SELECT_HERO, Ctrl+H) with amIAHero: the first KINDOF_HERO object walked.
export namespace generalszh::gameplay
{
namespace view_detail
{
template<typename Visit>
void ForPlayerObjects(const GameWorld &game, std::uint32_t player, Visit &&visit)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.owner != player)
			continue;
		for (auto it = record.members.rbegin(); it != record.members.rend(); ++it)
			if (game.world.IsAlive(*it) && !visit(*it))
				return;
	}
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *reference = game.world.Get<engine::gameplay::DefinitionRef>(entity);
	return reference != nullptr ? &game.templates.DefinitionAt(reference->index) : nullptr;
}
}

// The object whose place VIEW_COMMAND_CENTER looks at (none: nothing to look at, no structure owned).
inline std::optional<ecs::Entity> CommandCenterToView(GameWorld &game, std::uint32_t player)
{
	std::optional<ecs::Entity> place;
	bool commandCenter = false;
	std::int64_t value = -1;
	view_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const content::ObjectDefinition *definition = view_detail::DefinitionOf(game, entity);
		if (definition == nullptr)
			return true;
		if (definition->Is("COMMANDCENTER"))
		{
			commandCenter = true;
			place = entity;
		}
		else if (!commandCenter && definition->Is("STRUCTURE"))
		{
			const std::int64_t cost = CostToBuild(game, player, *definition);
			if (cost > value)
			{
				value = cost;
				place = entity;
			}
		}
		return true;
	});
	return place;
}

// The hero SELECT_HERO picks (none: the player has none).
inline std::optional<ecs::Entity> FirstHero(const GameWorld &game, std::uint32_t player)
{
	std::optional<ecs::Entity> hero;
	view_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const content::ObjectDefinition *definition = view_detail::DefinitionOf(game, entity);
		if (definition != nullptr && definition->Is("HERO"))
		{
			hero = entity;
			return false;
		}
		return true;
	});
	return hero;
}
}
