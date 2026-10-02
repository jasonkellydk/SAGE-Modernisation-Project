export module games.generalszh.gameplay.orders.algorithms.wander_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.content.locomotors.locomotor_catalog;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.components.wander_anchor;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import Engine.Core.Math.FixedRandom;

// Locomotor sets and wandering:
// - ChooseLocomotorSet (AIUpdateInterface::chooseLocomotorSet): the unit moves on its template's set from now on (its
//   normal one: the upgraded normal one once it has that upgrade); already on it: nothing; its template has none: no
//   change (false). The set's first locomotor starts afresh (a new Locomotor: its weave rolled, its donut timer 2.5 s
//   on); how fast it is going stays.
// - OrderWander / OrderPanic (aiWander / aiPanic, CMD_FROM_SCRIPT): a mobile unit's orders are cleared and it goes along
//   the path from `way` (MoveMode::Wander / Panic), its first goal the waypoint off by a random offset (onEnter).
// - OrderWanderInPlace (aiWanderInPlace): about where it stands (its WanderAnchor), the first goal off it at random.
// - TeamWander / TeamPanic (ScriptActions::doTeamWander / doTeamPanic): each member with an AI, in order, from the path's
//   waypoint closest to it, on its wander (panic) set; a member with no such waypoint ends the whole action there.
// - TeamWanderInPlace (doTeamWanderInPlace): each member with an AI on its wander set, wandering where it stands.
// - NormalLocomotors: the script actions that put a unit back on its normal set first (doNamedMoveToWaypoint,
//   doNamedAttack, doNamedAttackArea, doCreateReinforcements).
export namespace generalszh::gameplay
{
namespace locomotor_set
{
inline constexpr std::uint8_t Normal = 0;
inline constexpr std::uint8_t Wander = 1;
inline constexpr std::uint8_t Panic = 2;
inline constexpr std::uint8_t Sluggish = 3;
inline constexpr std::uint8_t Taxiing = 4;
inline constexpr std::uint8_t Supersonic = 5;
inline constexpr std::uint8_t Freefall = 6;
inline constexpr const auto &Names = content::LocomotorSetNames;
}

inline bool ChooseLocomotorSet(GameWorld &game, ecs::Entity unit, std::uint8_t set)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Locomotion *motion = world.IsAlive(unit) ? world.Get<gp::Locomotion>(unit) : nullptr;
	const gp::DefinitionRef *ref = motion != nullptr ? world.Get<gp::DefinitionRef>(unit) : nullptr;
	if (ref == nullptr || set >= locomotor_set::Names.size())
		return false;
	if (set == motion->set)
		return true;
	const std::string_view name = set == locomotor_set::Normal && motion->upgraded != 0 ? "SET_NORMAL_UPGRADED" : locomotor_set::Names[set];
	const gp::LocomotorDefinition *definition = content::ObjectLocomotor(game.templates.DefinitionAt(ref->index), game.templates.Content().locomotors, name);
	if (definition == nullptr)
		return false;
	gp::Locomotion fresh = gp::MakeLocomotion(*definition);
	fresh.speed = motion->speed;
	fresh.vertical = motion->vertical;
	fresh.majorRadius = motion->majorRadius;
	fresh.boundingRadius = motion->boundingRadius;
	// The same body flies on (a new locomotor, its physics as they were).
	fresh.forced = motion->forced;
	fresh.flownSpeed = motion->flownSpeed;
	fresh.upgraded = motion->upgraded;
	fresh.set = set;
	fresh.donutTimer = game.tick + game.step.TicksPerSecond() * 5 / 2;
	if (definition->wanderWidth != Engine::Math::Fixed{})
		gp::StartWander(fresh, [&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(game.random, low, high); });
	*motion = fresh;
	return true;
}

inline void OrderWander(GameWorld &game, ecs::Entity unit, std::uint32_t way, bool panic = false)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit) || way == gp::WaypointGraph::None)
		return;
	Commanded(game, unit);
	auto *order = detail::Mobile(game, unit) ? world.Get<gp::MoveOrder>(unit) : nullptr;
	if (order == nullptr)
		return;
	Engine::Math::FixedVector2 goal = game.waypoints.Position(way).XY();
	if (const std::int64_t cells = gp::PathWanderCells(world.Get<gp::Locomotion>(unit)->locomotor.wanderWidth); cells > 0)
		goal = goal + gp::WanderOffset(cells, gp::PathfindCellSize, [&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(game.random, low, high); });
	const auto *decks = world.FindResource<gp::DeckSurfaces>();
	if (decks != nullptr)
		goal = gp::WaypointGoalOnWall(*decks, game.waypoints.Position(way).XY(), goal);
	*order = gp::MoveOrder{goal, way, panic ? gp::MoveMode::Panic : gp::MoveMode::Wander};
	// computeGoal's goal layer; each leg routed afresh.
	order->goalLayer = decks != nullptr ? gp::WaypointGoalLayer(*decks, game.waypoints.Position(way).XY()) : gp::GroundLayer;
	if (auto *route = world.Get<gp::Route>(unit))
		route->planned = false;
	if (auto *attack = world.Get<gp::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

inline void OrderWanderInPlace(GameWorld &game, ecs::Entity unit, bool commanded = true)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit))
		return;
	if (commanded)
		Commanded(game, unit);
	if (!detail::Mobile(game, unit) || !world.Has<gp::MoveOrder>(unit))
		return;
	const Engine::Math::FixedVector2 origin = world.Get<gp::Transform>(unit)->position.XY();
	// (Adding the anchor moves the entity: its other components are looked up after.)
	if (!world.Has<gp::WanderAnchor>(unit))
		world.Add<gp::WanderAnchor>(unit);
	world.Get<gp::WanderAnchor>(unit)->origin = origin;
	auto *order = world.Get<gp::MoveOrder>(unit);
	const std::int64_t cells = gp::PointWanderCells(world.Get<gp::Locomotion>(unit)->locomotor.wanderAboutPointRadius, gp::PathfindCellSize);
	const Engine::Math::FixedVector2 goal =
		origin + gp::WanderOffset(cells, gp::PathfindCellSize, [&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(game.random, low, high); });
	*order = gp::MoveOrder{goal, gp::WaypointGraph::None, gp::MoveMode::WanderInPlace};
	order->goalLayer = gp::GroundLayer;
	if (auto *route = world.Get<gp::Route>(unit))
		route->planned = false;
	if (auto *attack = world.Get<gp::AttackTarget>(unit))
		*attack = {};
	detail::EndStance(game, unit);
}

inline void TeamWanderAlong(GameWorld &game, const std::string &team, const std::string &label, bool panic)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return;
	const std::vector<ecs::Entity> members = game.roster.TeamAt(*index).members;
	for (const ecs::Entity unit : members)
	{
		if (!game.world.IsAlive(unit) || !HasAi(game, unit))
			continue;
		const std::uint32_t way = game.waypoints.ClosestOnPath(game.world.Get<engine::gameplay::Transform>(unit)->position.XY(), label);
		if (way == engine::gameplay::WaypointGraph::None)
			return;
		ChooseLocomotorSet(game, unit, panic ? locomotor_set::Panic : locomotor_set::Wander);
		OrderWander(game, unit, way, panic);
	}
}

inline void TeamWander(GameWorld &game, const std::string &team, const std::string &label) { TeamWanderAlong(game, team, label, false); }
inline void TeamPanic(GameWorld &game, const std::string &team, const std::string &label) { TeamWanderAlong(game, team, label, true); }

inline void TeamWanderInPlace(GameWorld &game, const std::string &team)
{
	const auto index = ResolveTeam(game, team);
	if (!index)
		return;
	const std::vector<ecs::Entity> members = game.roster.TeamAt(*index).members;
	for (const ecs::Entity unit : members)
	{
		if (!game.world.IsAlive(unit) || !HasAi(game, unit))
			continue;
		ChooseLocomotorSet(game, unit, locomotor_set::Wander);
		OrderWanderInPlace(game, unit);
	}
}

inline void NormalLocomotors(GameWorld &game, ecs::Entity unit) { ChooseLocomotorSet(game, unit, locomotor_set::Normal); }
}
