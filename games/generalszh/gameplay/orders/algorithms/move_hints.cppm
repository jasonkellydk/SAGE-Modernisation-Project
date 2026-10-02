export module games.generalszh.gameplay.orders.algorithms.move_hints;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.navigation.algorithms.clearance;

// What the move hint asks the game (CommandTranslator::handleDefaultMoveCommand, DO_HINT): whether a unit could path to a
// point at a glance.
export namespace generalszh::gameplay
{
// AIUpdateInterface::isQuickPathAvailable (Pathfinder::clientSafeQuickDoesPathExist over its locomotor set's surfaces:
// connected zones), or, with a CLIFF locomotor, the point a cliff cell (TerrainLogic::isCliffCell: the pathfinder's
// cliff cells here). A unit without an AI (no navigation agent): no.
inline bool QuickPathAvailable(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector2 to)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *agent = world.IsAlive(unit) ? world.Get<gp::NavigationAgent>(unit) : nullptr;
	const auto *from = agent != nullptr ? world.Get<gp::Transform>(unit) : nullptr;
	auto *grid = world.FindResource<gp::NavigationGrid>();
	if (from == nullptr || grid == nullptr)
		return false;
	if (gp::QuickPathExists(*grid, agent->surfaces, from->position.XY(), to))
		return true;
	if ((agent->surfaces & gp::locomotor_surface::Cliff) == 0)
		return false;
	const auto cellOf = [](Engine::Math::Fixed value) { return static_cast<std::int32_t>((value / Engine::Math::Fixed::FromInt(gp::PathfindCellSize)).Floor()); };
	const std::int32_t x = cellOf(to.x), y = cellOf(to.y);
	return grid->Contains(x, y) && grid->Type(x, y) == gp::PathfindCellType::Cliff;
}
}
