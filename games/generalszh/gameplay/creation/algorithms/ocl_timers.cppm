export module games.generalszh.gameplay.creation.algorithms.ocl_timers;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.creation.systems.ocl_timer_system;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import games.generalszh.gameplay.powers.algorithms.power_trigger;

// The tick's OclTimerEvents, in order (OCLUpdate::update's creation): from the nearest edge of the map
// (findClosestEdgePoint; CreateAtEdge) or where it stands, its list (a faction-triggered one's for its owner's side:
// the first FactionOCL of that side; none, nothing) is made at it towards the timer's position (ObjectCreationList::create
// with it as the primary object: a delivery run's transport joins its player's default team).
export namespace generalszh::gameplay
{
inline void ApplyOclTimers(GameWorld &game, const std::vector<std::string> &playerSides)
{
	auto &world = game.world;
	auto *resource = world.FindResource<OclTimerEvents>();
	if (resource == nullptr)
		return;
	std::vector<OclTimerEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	const auto &content = game.templates.Content();
	for (const OclTimerEvent &event : events)
	{
		const OclTimerConfig *config = game.templates.OclTimerOf(event.definition);
		if (config == nullptr || !world.IsAlive(event.source))
			continue;
		std::string_view list = config->list;
		if (config->factionTriggered)
		{
			list = {};
			const std::string side = event.player < playerSides.size() ? playerSides[event.player] : std::string{};
			for (const auto &[faction, name] : config->factionLists)
				if (faction == side)
				{
					list = name;
					break;
				}
		}
		if (list.empty())
			continue;
		const Engine::Math::FixedVector3 primary = config->atEdge ? game.ground.ClosestEdgePoint(event.position.XY()) : event.position;
		const auto runs = content.powers.deliveries.find(list);
		if (runs != content.powers.deliveries.end() && !runs->second.empty())
		{
			const std::uint32_t team = DefaultTeamOf(game, event.source, event.player);
			for (const content::DeliveryNugget &run : runs->second)
				detail::Deliver(game, run, primary, event.position.XY(), team, event.source);
		}
		else
			RunCreationList(game, list, {primary, event.facing, event.team, event.source, 0u, 0u, event.position});
	}
}
}
