export module engine.gameplay.rts.navigation.algorithms.waypoint_steps;
import std;

export import engine.gameplay.rts.navigation.resources.waypoint_graph;
export import Engine.Core.Math.FixedRandom;

// How a waypoint path is walked (AIFollowWaypointPathState, without ALLOW_BACKTRACK): from the waypoint it is at and the
// one it came from (WaypointGraph indices, None for none), which comes next.
export namespace engine::gameplay
{
// AIFollowWaypointPathState::hasNextWaypoint: a waypoint with links has a next one unless its only link leads back to
// where it came from.
inline bool HasNextWaypoint(const WaypointGraph &graph, std::uint32_t current, std::uint32_t prior)
{
	const auto links = graph.Links(current);
	if (links.empty())
		return false;
	if (prior == WaypointGraph::None || links.size() > 1)
		return true;
	return links.front() != prior;
}

// AIFollowWaypointPathState::getNextWaypoint: at random among its links, never straight back (GameLogicRandomValue);
// none: no next one.
inline std::uint32_t NextWaypoint(const WaypointGraph &graph, std::uint32_t current, std::uint32_t prior, Engine::Math::RandomStream &random)
{
	if (!HasNextWaypoint(graph, current, prior))
		return WaypointGraph::None;
	const auto links = graph.Links(current);
	const std::int64_t count = static_cast<std::int64_t>(links.size());
	std::int64_t skip = -1;
	for (std::int64_t index = 0; index < count; ++index)
		if (links[static_cast<std::size_t>(index)] == prior)
		{
			skip = index;
			break;
		}
	std::int64_t which = 0;
	if (skip >= 0)
	{
		which = Engine::Math::UniformInt(random, 0, count - 2);
		if (which == skip)
			which = count - 1;
	}
	else
		which = Engine::Math::UniformInt(random, 0, count - 1);
	return links[static_cast<std::size_t>(which)];
}
}
