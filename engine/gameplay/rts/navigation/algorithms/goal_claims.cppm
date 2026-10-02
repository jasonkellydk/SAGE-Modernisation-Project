export module engine.gameplay.rts.navigation.algorithms.goal_claims;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.resources.goal_cells;
export import engine.gameplay.rts.navigation.components.pathfind_goal;
export import engine.gameplay.rts.navigation.algorithms.clearance;

// Goal claims on the ground grid (Pathfinder, AIPathfind.cpp): where a ground mover's move really ends, and the cells it
// claims there so the next mover's goal is moved off them.
//   updateGoal / removeGoal: a mover claims the cells its footprint covers about its goal cell (the last claim on a
//     cell wins), and lets go of only those still its own.
//   checkDestination: a goal cell will do when every cell of the footprint about it is on the map, not an obstacle
//     (but the one it ignores) nor impassable, and not claimed by one it must leave be (an ally; a unit standing on its
//     own claim that it cannot crush).
//   adjustDestination: the goal's own cell, then a square spiral out from it (right, up, left, down, a step longer each
//     half turn), at most 400 cells: the first cell that is no cliff, inside the playable area for a human player's
//     unit, will do (above), and is reachable (clientSafeQuickDoesPathExist from the mover, or from the goal when the
//     goal itself is not); the goal becomes that cell's point.
//   snapClosestGoalPosition: what a failed adjustment settles for: the goal's cell, or the first of the 3 x 3 about it
//     that will do; a one-cell mover takes the first unclaimed one, or the first with no unit standing on its claim.
// A cell's point is its centre for a mover centred in its cell, else its low corner plus 0.05 of a cell
// (adjustCoordToCell). Ground cells only: a goal on a bridge's deck keeps to the ground's cells (TODO(port): the
// decks' own claims, doLayer).
export namespace engine::gameplay
{
// getRadiusAndCenter: how many cells a footprint reaches from its cell, and whether it is centred in it.
struct GoalFootprint
{
	std::int32_t radius{0};
	bool centered{true};
};

// Whose claims a mover must respect, asked for a cell's claimant (neither the mover nor what it ignores):
//   Blocks(owner, x, y): the claim keeps the mover off the cell (checkDestination's ally and fixed-unit tests);
//   Standing(owner, x, y): the claimant stands on its claim there (UNIT_PRESENT_FIXED: its position cells are its goal
//   cells).
template<typename Claims>
concept GoalClaimRules = requires(const Claims &claims, ecs::Entity owner, std::int32_t x, std::int32_t y) {
	{ claims.Blocks(owner, x, y) } -> std::convertible_to<bool>;
	{ claims.Standing(owner, x, y) } -> std::convertible_to<bool>;
};

// What an adjustment needs of the mover.
struct GoalSeeker
{
	ecs::Entity self;
	ecs::Entity ignored;      // the obstacle whose cells are open to it (getIgnoredObstacleID)
	GoalFootprint footprint;
	std::uint8_t surfaces{0}; // its locomotor set's surfaces
	bool human{true};         // a human player's (a computer's may end outside the playable area)
	Engine::Math::FixedVector2 from; // where it is
};

// The playable area in cells, inclusive (m_logicalExtent: TerrainLogic::getExtent over the cell size, its high edge
// less one).
struct LogicalExtent
{
	std::int32_t loX{0}, loY{0}, hiX{0}, hiY{0};
};

namespace goal_detail
{
using Engine::Math::Fixed;

inline std::int32_t FloorCell(Fixed value) noexcept { return static_cast<std::int32_t>((value / Fixed::FromInt(PathfindCellSize)).Floor()); }

// The first and one-past-last cells a footprint about `cell` covers on one axis.
inline std::pair<std::int32_t, std::int32_t> Span(std::int32_t cell, const GoalFootprint &footprint) noexcept
{
	return {cell - footprint.radius, cell + footprint.radius + (footprint.centered ? 1 : 0)};
}
}

// worldToCell: the cell a point is in, clamped to the grid.
inline std::array<std::int32_t, 2> WorldToCell(const NavigationGrid &grid, Engine::Math::FixedVector2 at) noexcept
{
	return {std::clamp(goal_detail::FloorCell(at.x), 0, std::max(grid.Width() - 1, 0)), std::clamp(goal_detail::FloorCell(at.y), 0, std::max(grid.Height() - 1, 0))};
}

// adjustCoordToCell.
inline Engine::Math::FixedVector2 GoalCellPoint(std::int32_t x, std::int32_t y, bool centered) noexcept
{
	using Engine::Math::Fixed;
	const Fixed size = Fixed::FromInt(PathfindCellSize);
	const Fixed offset = centered ? size / Fixed::FromInt(2) : size / Fixed::FromInt(20);
	return {Fixed::FromInt(x) * size + offset, Fixed::FromInt(y) * size + offset};
}

// The cell a claim about `at` centres on (updateGoal, updatePos): the cell it is in when centred, else the nearest
// cell corner's.
inline std::array<std::int32_t, 2> ClaimCell(Engine::Math::FixedVector2 at, bool centered) noexcept
{
	using Engine::Math::Fixed;
	if (centered)
		return {goal_detail::FloorCell(at.x), goal_detail::FloorCell(at.y)};
	const Fixed half = Fixed::FromInt(PathfindCellSize) / Fixed::FromInt(2);
	return {goal_detail::FloorCell(at.x + half), goal_detail::FloorCell(at.y + half)};
}

// Whether the footprint about (x, y) covers (cellX, cellY).
inline bool FootprintCovers(std::int32_t x, std::int32_t y, const GoalFootprint &footprint, std::int32_t cellX, std::int32_t cellY) noexcept
{
	const auto [loX, hiX] = goal_detail::Span(x, footprint);
	const auto [loY, hiY] = goal_detail::Span(y, footprint);
	return cellX >= loX && cellX < hiX && cellY >= loY && cellY < hiY;
}

// Pathfinder::removeGoal: the mover lets go of the cells still its own about its goal cell (a footprint of no reach
// clears as one of reach 1).
inline void RemoveGoal(GoalCells &cells, ecs::Entity self, PathfindGoal &goal, GoalFootprint footprint) noexcept
{
	if (!goal.Claimed())
	{
		goal = {};
		return;
	}
	if (footprint.radius == 0)
		footprint.radius = 1;
	const auto [loX, hiX] = goal_detail::Span(goal.x, footprint);
	const auto [loY, hiY] = goal_detail::Span(goal.y, footprint);
	for (std::int32_t x = loX; x < hiX; ++x)
		for (std::int32_t y = loY; y < hiY; ++y)
			if (cells.At(x, y) == self)
				cells.Set(x, y, ecs::Entity{});
	goal = {};
}

// Pathfinder::updateGoal (ground): the mover claims the footprint about the cell of `at`, letting go of its last claim
// first; the same cell again changes nothing.
inline void UpdateGoal(GoalCells &cells, ecs::Entity self, PathfindGoal &goal, GoalFootprint footprint, Engine::Math::FixedVector2 at) noexcept
{
	const auto [cellX, cellY] = ClaimCell(at, footprint.centered);
	if (goal.x == cellX && goal.y == cellY)
		return;
	RemoveGoal(cells, self, goal, footprint);
	goal = {cellX, cellY};
	const auto [loX, hiX] = goal_detail::Span(cellX, footprint);
	const auto [loY, hiY] = goal_detail::Span(cellY, footprint);
	for (std::int32_t x = loX; x < hiX; ++x)
		for (std::int32_t y = loY; y < hiY; ++y)
			cells.Set(x, y, self);
}

// Pathfinder::checkDestination (ground, a mover): whether the footprint about (cellX, cellY) may be its goal.
template<GoalClaimRules Claims>
bool CheckDestination(const NavigationGrid &grid, const GoalCells &cells, const GoalSeeker &seeker, std::int32_t cellX, std::int32_t cellY, const Claims &claims)
{
	const auto [loX, hiX] = goal_detail::Span(cellX, seeker.footprint);
	const auto [loY, hiY] = goal_detail::Span(cellY, seeker.footprint);
	for (std::int32_t x = loX; x < hiX; ++x)
		for (std::int32_t y = loY; y < hiY; ++y)
		{
			if (!grid.Contains(x, y))
				return false; // off the map
			const PathfindCellType type = grid.Type(x, y);
			if (type == PathfindCellType::Obstacle)
			{
				if (seeker.ignored != ecs::Entity{} && grid.Obstacle(x, y) == seeker.ignored)
					continue;
				return false;
			}
			if (IsImpassable(type))
				return false;
			const ecs::Entity owner = cells.At(x, y);
			if (owner == ecs::Entity{} || owner == seeker.self || owner == seeker.ignored)
				continue;
			if (claims.Blocks(owner, x, y))
				return false;
		}
	return true;
}

// Pathfinder::checkForAdjust (ground): (cellX, cellY) as the goal for `destination`, its point if it will do.
template<GoalClaimRules Claims>
std::optional<Engine::Math::FixedVector2> CheckForAdjust(NavigationGrid &grid, const GoalCells &cells, const GoalSeeker &seeker, const LogicalExtent &extent,
	std::int32_t cellX, std::int32_t cellY, Engine::Math::FixedVector2 destination, const Claims &claims, std::optional<bool> &direct)
{
	if (!grid.Contains(cellX, cellY))
		return std::nullopt;
	if (grid.Type(cellX, cellY) == PathfindCellType::Cliff)
		return std::nullopt; // no final destinations on cliffs
	if (seeker.human && (cellX < extent.loX || cellY < extent.loY || cellX > extent.hiX || cellY > extent.hiY))
		return std::nullopt;
	if (!CheckDestination(grid, cells, seeker, cellX, cellY, claims))
		return std::nullopt;
	const Engine::Math::FixedVector2 adjusted = GoalCellPoint(cellX, cellY, seeker.footprint.centered);
	// Reachable from where it is, or else (no path to the destination itself) from the destination. Whether the destination
	// is reachable is the same for every cell tried: asked once per adjustment (`direct`), when first needed.
	if (!QuickPathExists(grid, seeker.surfaces, seeker.from, adjusted))
	{
		if (!direct)
			direct = QuickPathExists(grid, seeker.surfaces, seeker.from, destination);
		if (*direct || !QuickPathExists(grid, seeker.surfaces, destination, adjusted))
			return std::nullopt;
	}
	return adjusted;
}

// Pathfinder::adjustDestination (no group destination): the goal moved to the first cell of the spiral that will do;
// none within 400 cells: nothing.
template<GoalClaimRules Claims>
std::optional<Engine::Math::FixedVector2> AdjustDestination(NavigationGrid &grid, const GoalCells &cells, const GoalSeeker &seeker, const LogicalExtent &extent,
	Engine::Math::FixedVector2 destination, const Claims &claims)
{
	using Engine::Math::Fixed;
	Engine::Math::FixedVector2 corner = destination;
	if (!seeker.footprint.centered)
	{
		const Fixed half = Fixed::FromInt(PathfindCellSize) / Fixed::FromInt(2);
		corner = {corner.x + half, corner.y + half};
	}
	auto [i, j] = WorldToCell(grid, corner);
	std::optional<bool> direct; // QuickPathExists(from, destination), once asked
	if (const auto found = CheckForAdjust(grid, cells, seeker, extent, i, j, destination, claims, direct))
		return found;
	constexpr std::int32_t MaxCellsToTry = 400;
	std::int32_t limit = MaxCellsToTry;
	std::int32_t delta = 1;
	while (limit > 0)
	{
		for (std::int32_t count = delta; count > 0; --count)
		{
			++i;
			--limit;
			if (const auto found = CheckForAdjust(grid, cells, seeker, extent, i, j, destination, claims, direct))
				return found;
		}
		for (std::int32_t count = delta; count > 0; --count)
		{
			++j;
			--limit;
			if (const auto found = CheckForAdjust(grid, cells, seeker, extent, i, j, destination, claims, direct))
				return found;
		}
		++delta;
		for (std::int32_t count = delta; count > 0; --count)
		{
			--i;
			--limit;
			if (const auto found = CheckForAdjust(grid, cells, seeker, extent, i, j, destination, claims, direct))
				return found;
		}
		for (std::int32_t count = delta; count > 0; --count)
		{
			--j;
			--limit;
			if (const auto found = CheckForAdjust(grid, cells, seeker, extent, i, j, destination, claims, direct))
				return found;
		}
		++delta;
	}
	return std::nullopt;
}

// Pathfinder::snapClosestGoalPosition (ground).
template<GoalClaimRules Claims>
Engine::Math::FixedVector2 SnapClosestGoal(const NavigationGrid &grid, const GoalCells &cells, const GoalSeeker &seeker, Engine::Math::FixedVector2 at, const Claims &claims)
{
	using Engine::Math::Fixed;
	const bool centered = seeker.footprint.centered;
	Engine::Math::FixedVector2 corner = at;
	if (!centered)
	{
		const Fixed half = Fixed::FromInt(PathfindCellSize) / Fixed::FromInt(2);
		corner = {corner.x + half, corner.y + half};
	}
	const auto [cellX, cellY] = WorldToCell(grid, corner);
	const Engine::Math::FixedVector2 snapped = GoalCellPoint(cellX, cellY, centered);
	if (CheckDestination(grid, cells, seeker, cellX, cellY, claims))
		return snapped;
	for (std::int32_t x = cellX - 1; x < cellX + 2; ++x)
		for (std::int32_t y = cellY - 1; y < cellY + 2; ++y)
			if (CheckDestination(grid, cells, seeker, x, y, claims))
				return GoalCellPoint(x, y, centered);
	if (seeker.footprint.radius == 0)
	{
		for (std::int32_t x = cellX - 1; x < cellX + 2; ++x)
			for (std::int32_t y = cellY - 1; y < cellY + 2; ++y)
				if (grid.Contains(x, y) && (cells.At(x, y) == ecs::Entity{} || cells.At(x, y) == seeker.self))
					return GoalCellPoint(x, y, centered);
		for (std::int32_t x = cellX - 1; x < cellX + 2; ++x)
			for (std::int32_t y = cellY - 1; y < cellY + 2; ++y)
				if (grid.Contains(x, y) && (cells.At(x, y) == ecs::Entity{} || !claims.Standing(cells.At(x, y), x, y)))
					return GoalCellPoint(x, y, centered);
	}
	return snapped;
}
}
