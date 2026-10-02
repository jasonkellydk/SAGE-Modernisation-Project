export module games.generalszh.gameplay.effects.algorithms.transition_creations;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.systems.transition_creation_system;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;

// The tick's TransitionCreationEvents, in order: ObjectCreationList::create(ocl, object, &pos, damageSource->getPosition(),
// INVALID_ANGLE): the list runs at the place with the object as its primary (its team, facing and veterancy) and the
// damage source's position as the secondary point.
export namespace generalszh::gameplay
{
inline void ApplyTransitionCreations(GameWorld &game)
{
	auto &world = game.world;
	auto *events = world.FindResource<TransitionCreationEvents>();
	if (events == nullptr || events->list.empty())
		return;
	const std::vector<TransitionCreationEvent> list = std::move(events->list);
	events->list.clear();
	for (const TransitionCreationEvent &event : list)
	{
		const content::TransitionCreations *creations = game.templates.TransitionCreationsOf(event.definition);
		if (creations == nullptr || event.module >= creations->modules.size())
			continue;
		const std::string &name = creations->modules[event.module].slots[event.state][event.slot].list;
		if (name.empty())
			continue;
		CreationSource source{event.position, event.facing, event.team, world.IsAlive(event.object) ? event.object : ecs::Entity{}, event.veterancy};
		source.secondary = event.secondary;
		RunCreationList(game, name, source);
	}
}
}
