export module games.generalszh.gameplay.world.algorithms.position_search;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.rts.navigation.resources.navigation_grid;

// PartitionManager::findPositionAround's spot test, shared by everything that searches for a place to put something.
export namespace generalszh::gameplay
{
// PartitionManager::tryPosition with no options: not a cliff, open pathfinding ground, no water over it, and nothing
// within 5 of it.
inline auto SpotLegal(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const auto &spatial = world.Resource<gp::SpatialIndex>();
	const auto &grid = world.Resource<gp::NavigationGrid>();
	return [&game, &spatial, &grid](Engine::Math::FixedVector2 point) {
		const auto cellX = static_cast<std::int32_t>((point.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		const auto cellY = static_cast<std::int32_t>((point.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		if (grid.Width() > 0)
		{
			if (!grid.Contains(cellX, cellY))
				return false;
			const gp::PathfindCellType type = grid.Type(cellX, cellY);
			if (type == gp::PathfindCellType::Cliff || type == gp::PathfindCellType::Impassable)
				return false;
		}
		Fixed water;
		if (game.ground.Water(point, water) && water > game.ground.At(point))
			return false;
		bool free = true;
		const Fixed reach = Fixed::FromInt(5);
		spatial.ForEachWithin(point, reach, [&](const gp::SpatialEntry &entry) {
			if (!free)
				return;
			const Fixed apart = reach + entry.radius;
			if (Engine::Math::DistanceSquared(point, entry.position.XY()) < apart * apart)
				free = false;
		});
		return free;
	};
}

// Whether a point is inside the map's pathfinding extent (TerrainLogic::getMaximumPathfindExtent): findPositionAround
// answers its centre at once for one outside it (a scripted setup).
inline bool InPathfindExtent(GameWorld &game, Engine::Math::FixedVector2 point)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto &grid = game.world.Resource<gp::NavigationGrid>();
	if (grid.Width() == 0)
		return true;
	const auto cellX = static_cast<std::int32_t>((point.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
	const auto cellY = static_cast<std::int32_t>((point.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
	return grid.Contains(cellX, cellY);
}
}
