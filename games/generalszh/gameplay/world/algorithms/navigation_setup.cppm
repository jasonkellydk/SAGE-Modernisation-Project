export module games.generalszh.gameplay.world.algorithms.navigation_setup;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.rts.navigation.algorithms.grid_classification;
export import engine.gameplay.rts.navigation.algorithms.clearance;

// The level's pathfinding grid: one cell per heightmap cell over the largest
// playable area, cliffs where the map's cliff bits say, water where the ground
// is under water, and a clearance plane for each set of surfaces the game's
// ground locomotors move over (air movers fly over the grid). Obstacles are
// stamped in as their objects appear (the obstacle system).
export namespace generalszh::gameplay
{
inline void BuildNavigationGrid(GameWorld &game, engine::gameplay::NavigationGrid &grid)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto &terrain = game.level.terrain;
	std::int32_t width = 0, height = 0;
	for (const auto &boundary : terrain.playableExtents)
	{
		width = std::max(width, boundary[0]);
		height = std::max(height, boundary[1]);
	}
	grid.Resize(width, height);
	const auto &surface = game.level.surface;
	const std::int32_t border = static_cast<std::int32_t>(terrain.border);
	const auto cliff = [&](std::int32_t x, std::int32_t y) {
		if (surface.cliffFlags.empty() || surface.cliffFlagBytesPerRow == 0)
			return false;
		const std::int32_t mapWidth = static_cast<std::int32_t>(terrain.width), mapHeight = static_cast<std::int32_t>(terrain.height);
		const std::int32_t cx = std::clamp(x + border, 0, mapWidth - 2), cy = std::clamp(y + border, 0, mapHeight - 2);
		const std::size_t at = static_cast<std::size_t>(cy) * surface.cliffFlagBytesPerRow + static_cast<std::size_t>(cx >> 3);
		return at < surface.cliffFlags.size() && (surface.cliffFlags[at] & (1u << (cx & 7))) != 0;
	};
	const auto underwater = [&](Fixed x, Fixed y) {
		Fixed water;
		return game.ground.Water({x, y}, water) && game.ground.At({x, y}) < water;
	};
	gp::ClassifyTerrain(grid, cliff, underwater);
	std::vector<std::uint8_t> sets;
	for (const auto &[name, locomotor] : game.templates.Content().locomotors)
		if ((locomotor.surfaces & gp::locomotor_surface::Air) == 0 && locomotor.surfaces != 0 &&
			std::find(sets.begin(), sets.end(), locomotor.surfaces) == sets.end())
			sets.push_back(locomotor.surfaces);
	std::sort(sets.begin(), sets.end());
	grid.Clearance().clear();
	for (const std::uint8_t surfaces : sets)
	{
		grid.Clearance().push_back({surfaces, {}});
		gp::BuildClearance(grid, grid.Clearance().back());
	}
}
}
