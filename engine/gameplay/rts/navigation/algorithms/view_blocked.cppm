export module engine.gameplay.rts.navigation.algorithms.view_blocked;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// Whether obstacles in the pathfinding grid block a line of sight (the original's Pathfinder::isAttackViewBlockedByObstacle
// obstacle walk: iterateCellsAlongLine with attackBlockedByObstacleCallback, ground layer). A free function over the grid:
// callers decide whether the check applies (AttackUsesLineOfSight, KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT) and do the terrain
// part themselves.
export namespace engine::gameplay
{
// Pathfinder::iterateCellsAlongLine(ICoord2D...) as EA wrote it (Bresenham from www.gamedev.net): each step visits its
// cell; when the minor axis steps, the cell it steps into (minor moved, major not yet) is visited too, before the major
// step. `visit(x, y)` returns non-zero to stop with that value; a cell off the grid (getCell null) ends the walk with 0.
template<class Visit>
std::int32_t VisitCellsAlongLine(const NavigationGrid &grid, std::int32_t startX, std::int32_t startY, std::int32_t endX, std::int32_t endY, Visit &&visit)
{
	const std::int32_t deltaX = std::abs(endX - startX);
	const std::int32_t deltaY = std::abs(endY - startY);
	std::int32_t x = startX;
	std::int32_t y = startY;
	std::int32_t xinc1 = endX >= startX ? 1 : -1;
	std::int32_t xinc2 = xinc1;
	std::int32_t yinc1 = endY >= startY ? 1 : -1;
	std::int32_t yinc2 = yinc1;
	std::int32_t den, num, numadd, numpixels;
	if (deltaX >= deltaY)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = deltaX;
		num = deltaX / 2;
		numadd = deltaY;
		numpixels = deltaX;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = deltaY;
		num = deltaY / 2;
		numadd = deltaX;
		numpixels = deltaY;
	}
	for (std::int32_t pixel = 0; pixel <= numpixels; ++pixel)
	{
		if (!grid.Contains(x, y))
			return 0;
		if (const std::int32_t result = visit(x, y); result != 0)
			return result;
		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
			if (!grid.Contains(x, y))
				return 0;
			if (const std::int32_t result = visit(x, y); result != 0)
				return result;
		}
		x += xinc2;
		y += yinc2;
	}
	return 0;
}

// Pathfinder::worldToCell: REAL_TO_INT_FLOOR(pos / PATHFIND_CELL_SIZE).
inline std::int32_t WorldToCell(Engine::Math::Fixed coordinate) noexcept
{
	return static_cast<std::int32_t>((coordinate / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor());
}

// The obstacles that never block the view (attackBlockedByObstacleCallback): the looker's own, the victim's, the
// victim's slaver's, the looker's container's and its slaver's (none: an invalid entity).
struct ViewIgnores
{
	ecs::Entity self;
	ecs::Entity victim;
	ecs::Entity victimSlaver;
	ecs::Entity container;
	ecs::Entity slaver;
};

// isAttackViewBlockedByObstacle's walk from `from` to `to` (world positions): blocked at the first obstacle cell not
// ignored, not see-through (KINDOF_CAN_SEE_THROUGH_STRUCTURE) and not the obstacle the victim's own cell belongs to
// (victimCell: a victim inside another's bounds is not hidden by it); `skipCount` cells skipped first (3 for a looker
// on a bridge or rooftop).
inline bool AttackViewBlockedByObstacle(const NavigationGrid &grid, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to,
	const ViewIgnores &ignore, std::int32_t skipCount = 0)
{
	const std::int32_t victimX = WorldToCell(to.x), victimY = WorldToCell(to.y);
	const ecs::Entity victimCell = grid.Contains(victimX, victimY) ? grid.Obstacle(victimX, victimY) : ecs::Entity{};
	const auto present = [](ecs::Entity owner, ecs::Entity id) { return id != ecs::Entity{} && owner == id; };
	return VisitCellsAlongLine(grid, WorldToCell(from.x), WorldToCell(from.y), victimX, victimY, [&](std::int32_t x, std::int32_t y) -> std::int32_t {
		if (skipCount > 0)
		{
			--skipCount;
			return 0;
		}
		if (grid.Type(x, y) != PathfindCellType::Obstacle)
			return 0;
		const ecs::Entity owner = grid.Obstacle(x, y);
		if (present(owner, ignore.self) || present(owner, ignore.victim) || present(owner, ignore.victimSlaver) || present(owner, ignore.container) ||
			present(owner, ignore.slaver))
			return 0;
		if (grid.ObstacleIsSeeThrough(x, y))
			return 0;
		if (present(owner, victimCell))
			return 0;
		return 1;
	}) != 0;
}
}
