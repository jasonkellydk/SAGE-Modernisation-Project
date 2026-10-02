export module engine.gameplay.rts.navigation.algorithms.move_away_search;
import std;

export import engine.gameplay.rts.navigation.algorithms.route_search;
export import engine.gameplay.rts.navigation.algorithms.goal_claims;
export import engine.gameplay.rts.navigation.components.navigation;

// A route out of another unit's way (Pathfinder::getMoveAwayFromPath, AIPathfind.cpp, EA's Zero Hour source): out from
// where the mover stands, cheapest first over the cells it may pass (the units weighed as for any route), to the first
// cell other than its own whose box clears the other unit's way and the way of the unit it made way for before, and that
// may be its goal (checkDestination). A cell's box: about its point (adjustCoordToCell) as wide as both footprints
// (each footprint's radius in cells, less a quarter cell, half a cell more when centred). A unit's way: its position, then
// the points left on its route (the original's path also held the points it had passed).
export namespace engine::gameplay
{
struct AvoidedWay
{
	std::array<Engine::Math::FixedVector2, RoutePoints + 1> points{};
	std::size_t count{0};
};

// The way a unit is going: where it is, then its route's points still ahead.
inline AvoidedWay WayOf(Engine::Math::FixedVector2 position, const Route &route) noexcept
{
	AvoidedWay way;
	way.points[way.count++] = position;
	for (std::uint32_t point = route.next; point < route.count && way.count < way.points.size(); ++point)
		way.points[way.count++] = route.points[point];
	return way;
}

namespace move_away_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// LineInRegion: whether the segment from `a` to `b` meets the box (Liang-Barsky clipping, exact on fixed point).
inline bool SegmentMeetsBox(FixedVector2 a, FixedVector2 b, FixedVector2 lo, FixedVector2 hi) noexcept
{
	Fixed enter{}, leave = Fixed::One();
	const auto clip = [&](Fixed p, Fixed q) {
		if (p == Fixed{})
			return q >= Fixed{};
		const Fixed r = q / p;
		if (p < Fixed{})
		{
			if (r > leave)
				return false;
			if (r > enter)
				enter = r;
		}
		else
		{
			if (r < enter)
				return false;
			if (r < leave)
				leave = r;
		}
		return true;
	};
	const Fixed dx = b.x - a.x, dy = b.y - a.y;
	return clip(Fixed{} - dx, a.x - lo.x) && clip(dx, hi.x - a.x) && clip(Fixed{} - dy, a.y - lo.y) && clip(dy, hi.y - a.y);
}

inline bool WayMeetsBox(const AvoidedWay &way, FixedVector2 lo, FixedVector2 hi) noexcept
{
	for (std::size_t point = 0; point + 1 < way.count; ++point)
		if (SegmentMeetsBox(way.points[point], way.points[point + 1], lo, hi))
			return true;
	return false;
}
}

template<GoalClaimRules Claims>
FoundRoute FindMoveAway(const NavigationGrid &grid, const ClearancePlane &plane, RouteMover mover, Engine::Math::FixedVector2 from, RouteScratch &scratch,
	const RouteUnits *units, const GoalCells &goals, const GoalSeeker &seeker, GoalFootprint other, const AvoidedWay &avoid, const AvoidedWay *avoid2,
	const Claims &claims)
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	const Fixed cell = Fixed::FromInt(PathfindCellSize);
	Fixed half = Fixed::FromInt(seeker.footprint.radius) * cell - cell / Fixed::FromInt(4);
	if (seeker.footprint.centered)
		half += cell / Fixed::FromInt(2);
	half += Fixed::FromInt(other.radius) * cell;
	if (other.centered)
		half += cell / Fixed::FromInt(2);
	const auto accept = [&](std::int32_t x, std::int32_t y) {
		const FixedVector2 at = GoalCellPoint(x, y, seeker.footprint.centered);
		const FixedVector2 lo{at.x - half, at.y - half}, hi{at.x + half, at.y + half};
		if (move_away_detail::WayMeetsBox(avoid, lo, hi) || (avoid2 != nullptr && move_away_detail::WayMeetsBox(*avoid2, lo, hi)))
			return false;
		return CheckDestination(grid, goals, seeker, x, y, claims);
	};
	return SearchRoute(grid, plane, mover, from, from, scratch, GroundLayer, GroundLayer, units, false, accept);
}
}
