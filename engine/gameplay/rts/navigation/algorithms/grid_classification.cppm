export module engine.gameplay.rts.navigation.algorithms.grid_classification;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;

// Classifying the pathfinding grid from the map and the obstacles on it:
//   terrain: a cell is CLIFF when the map marks it (its cliff bit), WATER when
//   any corner is under water (water wins); obstacle cells stay obstacles;
//   footprints: a box takes the cells whose centres lie inside its rotated
//   rectangle, a cylinder or sphere the cells whose centres lie within its
//   radius, a fence the cells along its line (FenceWidth from -FenceXOffset);
//   the first obstacle to cover a cell owns it, rubble belongs to no one.
// Stamping returns the cells it touched, so clearance can be rebuilt there.
// All in fixed point, deterministic on every machine.
export namespace engine::gameplay
{
using GridRegion = std::array<std::int32_t, 4>; // lo x, lo y, hi x, hi y (inclusive)

// Terrain. `cliff(x, y)`: the map's cliff bit for the cell; `underwater(wx, wy)`: water above the ground there.
template<typename Cliff, typename Underwater>
void ClassifyTerrain(NavigationGrid &grid, Cliff &&cliff, Underwater &&underwater)
{
	using Engine::Math::Fixed;
	for (std::int32_t y = 0; y < grid.Height(); ++y)
		for (std::int32_t x = 0; x < grid.Width(); ++x)
		{
			PathfindCellType type = cliff(x, y) ? PathfindCellType::Cliff : PathfindCellType::Clear;
			const Fixed left = Fixed::FromInt(x * PathfindCellSize), top = Fixed::FromInt(y * PathfindCellSize);
			const Fixed right = left + Fixed::FromInt(PathfindCellSize), bottom = top + Fixed::FromInt(PathfindCellSize);
			if (underwater(left, top) || underwater(left, bottom) || underwater(right, bottom) || underwater(right, top))
				type = PathfindCellType::Water;
			grid.SetType(x, y, type); // obstacle cells stay obstacles
		}
}

// Puts an obstacle's footprint in (or takes it out). Returns the cells it covered, if any.
inline std::optional<GridRegion> StampFootprint(NavigationGrid &grid, ecs::Entity entity, const ObstacleFootprint &footprint,
	Engine::Math::FixedVector2 position, Engine::Math::TurnAngle orientation, bool insert)
{
	using Engine::Math::Fixed;
	const Fixed cell = Fixed::FromInt(PathfindCellSize), half = cell / Fixed::FromInt(2);
	GridRegion touched{std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max(), -1, -1};
	const auto visit = [&](std::int32_t x, std::int32_t y) {
		if (!grid.Contains(x, y))
			return;
		if (insert)
			grid.SetObstacle(x, y, entity, footprint, footprint.shape == ObstacleShape::Fence);
		else
			grid.RemoveObstacle(x, y, entity);
		touched = {std::min(touched[0], x), std::min(touched[1], y), std::max(touched[2], x), std::max(touched[3], y)};
	};
	const auto cellOf = [&](Fixed value) { return static_cast<std::int32_t>((value / cell).Floor()); };
	const Fixed c = Engine::Math::Cos(orientation), s = Engine::Math::Sin(orientation);
	// The footprint in its own frame: an x range and a y half height, around the origin it is turned about.
	Fixed minX, maxX, halfY, radius;
	bool round = false;
	switch (footprint.shape)
	{
	case ObstacleShape::Box:
		minX = Fixed{} - footprint.majorRadius;
		maxX = footprint.majorRadius;
		halfY = footprint.minorRadius;
		break;
	case ObstacleShape::Fence:
		minX = Fixed{} - footprint.minorRadius; // -FenceXOffset
		maxX = minX + footprint.majorRadius;    // along FenceWidth
		halfY = half;                           // a cell thick
		break;
	case ObstacleShape::Cylinder:
		radius = footprint.majorRadius;
		round = true;
		break;
	}
	const Fixed reach = round ? radius : std::max({Fixed{} - minX, maxX, halfY}) + halfY;
	const std::int32_t loX = cellOf(position.x - reach), hiX = cellOf(position.x + reach);
	const std::int32_t loY = cellOf(position.y - reach), hiY = cellOf(position.y + reach);
	for (std::int32_t y = loY; y <= hiY; ++y)
		for (std::int32_t x = loX; x <= hiX; ++x)
		{
			const Fixed dx = Fixed::FromInt(x) * cell + half - position.x, dy = Fixed::FromInt(y) * cell + half - position.y;
			if (round)
			{
				if (dx * dx + dy * dy <= radius * radius)
					visit(x, y);
				continue;
			}
			// Into the footprint's frame.
			const Fixed along = dx * c + dy * s, across = dy * c - dx * s;
			if (along >= minX && along <= maxX && across >= Fixed{} - halfY && across <= halfY)
				visit(x, y);
		}
	if (touched[2] < 0)
		return std::nullopt;
	return touched;
}
}
