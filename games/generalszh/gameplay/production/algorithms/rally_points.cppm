export module games.generalszh.gameplay.production.algorithms.rally_points;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.production.resources.rally_notices;
export import engine.gameplay.rts.production.components.rally_point;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.navigation.algorithms.clearance;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import games.generalszh.content.production.production_content;

// GameLogic::onSetRallyPoint / doSetRallyPoint (Core/GameEngine/Source/GameLogic/System/GameLogicDispatch.cpp): a
// player's rally point for a factory of theirs takes only where a BasicHumanLocomotor (GROUND, RUBBLE) could path from
// the factory (clientSafeQuickDoesPathExist); either way the setter hears of it (RallyNotices). Set, a factory with a
// production exit (getObjectExitInterface) keeps it (setRallyPoint), and what it makes goes on there.
export namespace generalszh::gameplay
{
inline void SetRallyPoint(GameWorld &game, std::uint32_t player, ecs::Entity factory, Engine::Math::FixedVector2 at)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *from = world.IsAlive(factory) ? world.Get<gp::Transform>(factory) : nullptr;
	if (from == nullptr)
		return;
	auto *notices = world.FindResource<RallyNotices>();
	auto *grid = world.FindResource<gp::NavigationGrid>();
	// From the factory's own place, inside its obstacle: the terrain zones (clientSafeQuickDoesPathExist's doingTerrainZone).
	const bool reachable = grid == nullptr ||
		gp::QuickPathExists(*grid, gp::locomotor_surface::Ground | gp::locomotor_surface::Rubble, from->position.XY(), at);
	if (notices != nullptr)
		notices->list.push_back({factory, player, at, reachable});
	if (!reachable)
		return;
	const auto *ref = world.Get<gp::DefinitionRef>(factory);
	if (ref == nullptr)
		return;
	const auto production = content::ReadObjectProduction(game.templates.DefinitionAt(ref->index), game.step);
	if (!production || !production->hasExit)
		return;
	if (!world.Has<gp::RallyPoint>(factory))
		world.Add<gp::RallyPoint>(factory);
	world.Get<gp::RallyPoint>(factory)->at = at;
}
}
