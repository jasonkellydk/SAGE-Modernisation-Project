export module games.generalszh.gameplay.orders.algorithms.group_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.group_columns;
export import games.generalszh.gameplay.orders.algorithms.group_formations;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.movement.algorithms.move_paths;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.slaves.components.spawner;

// AIGroup::groupMoveToPosition as EA wrote it (without a waypoint appended, or a formation made by
// groupCreateFormation): the members with an AI that are not held, as one group to `destination`.
// - A group that is a user formation (getMinMaxAndCenter: its first member's, two or more moving) moves as one
//   (friend_computeGroundPath, friend_moveFormationToPos: group_formations) once the destination is clamped, unless a
//   helicopter or an airborne aircraft is among it. Otherwise first the group's ground path (friend_computeGroundPath)
//   and, along it, its infantry and its ground vehicles as columns (friend_moveInfantryToPos / friend_moveVehicleToPos:
//   group_columns), to the destination as given.
// - The destination is then kept four pathfinding cells inside the map (clampWaypointPosition), further for
//   helicopters (their major radius) and aircraft (ten cells).
// - A player's order inside the members' area (grown by GroupMoveClickToGatherAreaFactor) tightens the group instead
//   when that area is under 2000 cells (its width squared: the original's dy repeats dx): each moves straight for the
//   point, nearest first, helicopters to their places around it (getHelicopterOffset). Not for airborne aircraft.
// - Else the members the columns did not take, each out of its formation (setFormationID(NO_FORMATION_ID)), (infantry when the infantry went as columns; ground vehicles, cliff
//   jumpers aside, when the vehicles did) go to the destination offset as they stand from the member nearest it, at
//   most six of their bounding radii (computeIndividualDestination), nearest the destination first (aiMoveToPosition;
//   equally near: the later member first, as SimpleObjectIterator's front insertion and stable sort leave them).
// Not yet: groupTightenToPosition's approach path (its units route to the point as any move), a stealth unit's
// delayed auto-acquire on a player's order.
export namespace generalszh::gameplay
{
namespace group_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// getHelicopterOffset: the idx-th helicopter's place about the point, on rings 70 apart, 70 apart along each ring.
inline FixedVector2 HelicopterOffset(FixedVector2 at, std::uint32_t index)
{
	if (index == 0)
		return at;
	const Fixed diameter = Fixed::FromInt(70);
	const Fixed circle = Fixed::FromRaw(411775); // 2 pi
	Fixed radius = diameter;
	Fixed step = diameter / (radius * circle) * circle;
	Fixed angle;
	for (std::uint32_t helicopter = 1; helicopter < index; ++helicopter)
	{
		angle += step;
		if (angle > circle)
		{
			radius += diameter;
			step = diameter / (radius * circle) * circle;
			angle -= circle;
		}
	}
	const Engine::Math::TurnAngle turn = Engine::Math::TurnFromRadians(angle);
	return {at.x + Engine::Math::Sin(turn) * radius, at.y + Engine::Math::Cos(turn) * radius};
}
}

// `layer`: the destination's (getLayerForDestination of the Coord3D given: a player's click at its height; a script's
// waypoint on the ground, LAYER_GROUND); each member's goal at its height on it (computeIndividualDestination's
// getLayerHeight), its own layer found from that (MemberGoalLayer).
void GroupMoveToPosition(GameWorld &game, std::span<const ecs::Entity> group, Engine::Math::FixedVector2 destination, bool fromPlayer, std::uint8_t layer = 0)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	auto &world = game.world;
	const auto definition = [&](ecs::Entity unit) -> const content::ObjectDefinition * {
		const auto *ref = world.Get<gp::DefinitionRef>(unit);
		return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
	};
	const auto held = [&](ecs::Entity unit) {
		if (world.Has<gp::Passenger>(unit))
			return true;
		const auto *off = world.Get<gp::Disabled>(unit);
		return off != nullptr && (off->mask & gp::disabled_type::Held) != 0;
	};
	const auto ground = [&](ecs::Entity unit) {
		const auto *motion = world.Get<gp::Locomotion>(unit);
		return motion == nullptr || !gp::IsAirborne(motion->locomotor);
	};
	// The members with an AI that are not held; their bounds.
	std::vector<ecs::Entity> members;
	FixedVector2 low{Fixed::FromInt(1'000'000), Fixed::FromInt(1'000'000)}, high{Fixed::FromInt(-1'000'000), Fixed::FromInt(-1'000'000)};
	for (const ecs::Entity unit : group)
	{
		if (!world.IsAlive(unit) || held(unit) || (world.Get<gp::MoveOrder>(unit) == nullptr && world.Get<gp::Locomotion>(unit) == nullptr) || world.Get<gp::Transform>(unit) == nullptr)
			continue;
		members.push_back(unit);
		const FixedVector2 at = world.Get<gp::Transform>(unit)->position.XY();
		low = {std::min(low.x, at.x), std::min(low.y, at.y)};
		high = {std::max(high.x, at.x), std::max(high.y, at.y)};
	}
	if (members.empty())
		return;
	bool isFormation = GroupFormationId(game, group) != 0;
	// friend_computeGroundPath, friend_moveInfantryToPos, friend_moveVehicleToPos (the destination as given).
	std::vector<ecs::Entity> everyone;
	for (const ecs::Entity unit : group)
		if (world.IsAlive(unit))
			everyone.push_back(unit);
	bool didInfantry = false, didVehicles = false;
	if (const auto path = !isFormation ? ComputeGroundPath(game, everyone, destination) : std::nullopt)
	{
		didInfantry = MoveInfantryColumns(game, everyone, *path, destination, fromPlayer);
		didVehicles = MoveVehicleColumns(game, everyone, *path, destination, fromPlayer);
	}
	const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
	const Fixed gather = game.templates.Content().gameData.groupMoveClickToGatherFactor;
	bool tighten = false;
	if (!isFormation && fromPlayer && gather > Fixed{})
	{
		// ScaleRect2D, then Coord3DInsideRect2D.
		const FixedVector2 grow = (high - low) * ((gather - Fixed::One()) * Fixed::FromRatio(1, 2));
		tighten = destination.x >= low.x - grow.x && destination.x <= high.x + grow.x && destination.y >= low.y - grow.y && destination.y <= high.y + grow.y;
	}
	Fixed extra;
	for (const ecs::Entity unit : members)
	{
		const content::ObjectDefinition *object = definition(unit);
		if (object == nullptr)
			continue;
		if (object->Is("PRODUCED_AT_HELIPAD"))
		{
			isFormation = false;
			extra = std::max(extra, object->geometry.majorRadius);
		}
		else if (object->Is("AIRCRAFT"))
		{
			if (!ground(unit))
			{
				tighten = false; // don't tighten aircraft
				isFormation = false; // keep the spread after the move
			}
			extra = std::max(extra, cell * 10);
		}
	}
	// clampWaypointPosition (an Int margin).
	const Fixed margin = Fixed::FromInt((cell * 4 + extra).Floor());
	const auto [mapLow, mapHigh] = game.ground.Extent();
	destination = {std::clamp(destination.x, mapLow.x + margin, std::max(mapLow.x + margin, mapHigh.x - margin)),
		std::clamp(destination.y, mapLow.y + margin, std::max(mapLow.y + margin, mapHigh.y - margin))};
	const auto nearestFirst = [&](std::vector<ecs::Entity> &units) {
		std::reverse(units.begin(), units.end()); // SimpleObjectIterator::insert puts each in front
		std::stable_sort(units.begin(), units.end(), [&](ecs::Entity a, ecs::Entity b) {
			return Engine::Math::DistanceSquared(world.Get<gp::Transform>(a)->position.XY(), destination) <
				Engine::Math::DistanceSquared(world.Get<gp::Transform>(b)->position.XY(), destination);
		});
	};
	std::vector<ecs::Entity> movers;
	for (const ecs::Entity unit : members)
		if (const content::ObjectDefinition *object = definition(unit); object == nullptr || !object->Is("IMMOBILE"))
			movers.push_back(unit);
	if (tighten)
	{
		const std::int64_t across = ((high.x - low.x) / cell).Floor();
		if (across * across < 2000)
		{
			// groupTightenToPosition.
			nearestFirst(movers);
			std::uint32_t helicopters = 0;
			for (const ecs::Entity unit : movers)
			{
				if (const content::ObjectDefinition *object = definition(unit); object != nullptr && object->Is("PRODUCED_AT_HELIPAD"))
					OrderMove(game, unit, group_detail::HelicopterOffset(destination, helicopters++), false, false);
				else
					OrderMoveTo(game, unit, destination, fromPlayer, true, MemberGoalLayer(game, destination, layer));
			}
			return;
		}
	}
	// A formation moves as one.
	if (isFormation)
	{
		MoveFormationToPos(game, everyone, ComputeGroundPath(game, everyone, destination), destination, fromPlayer);
		return;
	}
	// The rest: not those the columns took.
	std::vector<ecs::Entity> rest;
	for (const ecs::Entity unit : movers)
	{
		const content::ObjectDefinition *object = definition(unit);
		if (object != nullptr && object->Is("INFANTRY") && didInfantry)
			continue;
		if (object != nullptr && object->Is("VEHICLE") && didVehicles && ground(unit) && world.Get<gp::Locomotion>(unit) != nullptr && !object->Is("CLIFF_JUMPER"))
			continue;
		rest.push_back(unit);
	}
	nearestFirst(rest);
	FixedVector2 centre;
	bool first = true;
	for (const ecs::Entity unit : rest)
	{
		const FixedVector2 at = world.Get<gp::Transform>(unit)->position.XY();
		if (first)
		{
			centre = at; // the nearest member stands for the group's centre
			first = false;
		}
		// computeIndividualDestination: its offset from the centre, at most six bounding radii.
		const FixedVector2 offset = at - centre;
		const content::ObjectDefinition *object = definition(unit);
		const Fixed limit = object != nullptr ? content::BoundingCircleRadius(object->geometry) * 6 : Fixed{};
		const Fixed length = Engine::Math::Length(offset);
		const FixedVector2 goal = length > Fixed{} ? destination + offset / length * std::min(length, limit) : destination;
		BreakFormation(game, unit);
		OrderMoveTo(game, unit, goal, fromPlayer, true, MemberGoalLayer(game, goal, layer));
	}
}

// AIUpdateInterface::aiFollowPathAppend -> privateFollowPathAppend (a dynamic waypoint): following a path and under way
// (isMoving || isWaitingForPath), the point goes on its end; under way to a point, the path is that point (its goal) and
// then this one; else (idle, or doing something that needs no move) the path is this point alone (privateFollowPath:
// a new move, its legs before the last claiming no goal, the last adjusted and claimed).
// `goalLayer`: the point's layer, for a move to it alone (NoGoalLayer: found by the route).
void AppendWaypoint(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector2 point, bool fromPlayer, std::uint8_t goalLayer = engine::gameplay::NoGoalLayer)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::MoveOrder *order = world.IsAlive(unit) ? world.Get<gp::MoveOrder>(unit) : nullptr;
	if (order == nullptr)
		return;
	const bool moving = order->mode == gp::MoveMode::Point || order->mode == gp::MoveMode::Path || order->mode == gp::MoveMode::Direct ||
		order->mode == gp::MoveMode::PathExact || gp::Wandering(order->mode) || order->mode == gp::MoveMode::WanderInPlace;
	// AI_FOLLOW_PATH with a goal path: its path still the one under way (its current move the path's point; not a way out
	// of a factory: AI_FOLLOW_EXITPRODUCTION_PATH is another state).
	const auto *points = world.FindResource<gp::PathPoints>();
	if (const gp::MovePath *path = world.Get<gp::MovePath>(unit); moving && points != nullptr && path != nullptr &&
		path->kind == gp::MovePathKind::Follow && path->next > 0 && path->next <= path->count &&
		order->destination == gp::MovePathPoint(*points, *path, path->next - 1))
	{
		// addToGoalPath: on its end, as long as the path gets.
		gp::AppendMovePathPoint(world, unit, point);
		return;
	}
	if (moving)
	{
		group_column_detail::FollowGroupPath(game, unit, {order->destination, point}, fromPlayer);
		return;
	}
	OrderMove(game, unit, point, fromPlayer, true, gp::GoalClaim::Adjust, false, goalLayer);
}

// GameLogic::onAddWaypoint -> AIGroup::groupMoveToPosition(addWaypoint TRUE, CMD_FROM_PLAYER) as EA wrote it: never as
// a formation nor as columns nor tightened; the members with an AI that are not held nor IMMOBILE, the destination kept
// inside the map as for a move (clampWaypointPosition: four cells, further for helicopters and aircraft), each out of its
// formation, nearest the destination first, to the destination offset as it stands from the member nearest it (at most
// six of its bounding radii: computeIndividualDestination), appended to its path (aiFollowPathAppend).
void GroupAddWaypoint(GameWorld &game, std::span<const ecs::Entity> group, Engine::Math::FixedVector2 destination, bool fromPlayer, std::uint8_t layer = 0)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	auto &world = game.world;
	const auto definition = [&](ecs::Entity unit) -> const content::ObjectDefinition * {
		const auto *ref = world.Get<gp::DefinitionRef>(unit);
		return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
	};
	std::vector<ecs::Entity> movers;
	Fixed extra;
	const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
	for (const ecs::Entity unit : group)
	{
		if (!world.IsAlive(unit) || world.Get<gp::Transform>(unit) == nullptr)
			continue;
		const content::ObjectDefinition *object = definition(unit);
		if (object != nullptr && object->Is("PRODUCED_AT_HELIPAD"))
			extra = (std::max)(extra, object->geometry.majorRadius);
		else if (object != nullptr && object->Is("AIRCRAFT"))
			extra = (std::max)(extra, cell * 10);
		if (world.Has<gp::Passenger>(unit))
			continue;
		if (const auto *off = world.Get<gp::Disabled>(unit); off != nullptr && (off->mask & gp::disabled_type::Held) != 0)
			continue;
		if (object != nullptr && object->Is("IMMOBILE"))
			continue;
		if (world.Get<gp::MoveOrder>(unit) == nullptr)
			continue;
		movers.push_back(unit);
	}
	const Fixed margin = Fixed::FromInt((cell * 4 + extra).Floor());
	const auto [mapLow, mapHigh] = game.ground.Extent();
	destination = {std::clamp(destination.x, mapLow.x + margin, (std::max)(mapLow.x + margin, mapHigh.x - margin)),
		std::clamp(destination.y, mapLow.y + margin, (std::max)(mapLow.y + margin, mapHigh.y - margin))};
	// SimpleObjectIterator: each put in front, then sorted near to far (stable).
	std::reverse(movers.begin(), movers.end());
	std::stable_sort(movers.begin(), movers.end(), [&](ecs::Entity a, ecs::Entity b) {
		return Engine::Math::DistanceSquared(world.Get<gp::Transform>(a)->position.XY(), destination) <
			Engine::Math::DistanceSquared(world.Get<gp::Transform>(b)->position.XY(), destination);
	});
	FixedVector2 centre;
	bool first = true;
	for (const ecs::Entity unit : movers)
	{
		const FixedVector2 at = world.Get<gp::Transform>(unit)->position.XY();
		if (first)
		{
			centre = at;
			first = false;
		}
		const FixedVector2 offset = at - centre;
		const content::ObjectDefinition *object = definition(unit);
		const Fixed limit = object != nullptr ? content::BoundingCircleRadius(object->geometry) * 6 : Fixed{};
		const Fixed length = Engine::Math::Length(offset);
		const FixedVector2 goal = length > Fixed{} ? destination + offset / length * (std::min)(length, limit) : destination;
		BreakFormation(game, unit);
		AppendWaypoint(game, unit, goal, fromPlayer, MemberGoalLayer(game, goal, layer));
	}
}

// AIGroup::groupAttackObject (groupAttackObjectPrivate, not forced): the members not held, nearest the victim first
// (SimpleObjectIterator: equally near, the later member first); for each, the armed riders of a container that lets them
// fire attack it (getAbleToAttackSpecificObject: those with a weapon), then a spawner's slaves without free will
// (SpawnBehavior::orderSlavesToAttackTarget), then the member itself unless it is the victim (aiAttackObject).
void GroupAttackObject(GameWorld &game, std::span<const ecs::Entity> group, ecs::Entity victim, std::uint32_t maxShots,
	engine::gameplay::CommandSource source)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *victimAt = world.IsAlive(victim) ? world.Get<gp::Transform>(victim) : nullptr;
	if (victimAt == nullptr)
		return;
	const FixedVector2 target = victimAt->position.XY();
	std::vector<ecs::Entity> units;
	for (const ecs::Entity unit : group)
	{
		if (!world.IsAlive(unit) || world.Get<gp::Transform>(unit) == nullptr)
			continue;
		if (const auto *off = world.Get<gp::Disabled>(unit); off != nullptr && (off->mask & gp::disabled_type::Held) != 0)
			continue;
		units.push_back(unit);
	}
	std::reverse(units.begin(), units.end());
	std::stable_sort(units.begin(), units.end(), [&](ecs::Entity a, ecs::Entity b) {
		return Engine::Math::DistanceSquared(world.Get<gp::Transform>(a)->position.XY(), target) <
			Engine::Math::DistanceSquared(world.Get<gp::Transform>(b)->position.XY(), target);
	});
	for (const ecs::Entity unit : units)
	{
		const std::vector<ecs::Entity> riders(game.manifest.Aboard(unit).begin(), game.manifest.Aboard(unit).end());
		for (const ecs::Entity rider : riders)
		{
			const auto *ride = world.IsAlive(rider) ? world.Get<gp::OffMap>(rider) : nullptr;
			const auto *armament = ride != nullptr && ride->armed ? world.Get<gp::Armament>(rider) : nullptr;
			if (armament != nullptr && armament->weapon != gp::WeaponCatalog::None)
				OrderAttack(game, rider, victim, maxShots, source);
		}
		if (const auto *spawner = world.Get<gp::Spawner>(unit); spawner != nullptr && !spawner->freeWill)
			for (std::uint8_t index = 0; index < spawner->spawnedCount; ++index)
				if (world.IsAlive(spawner->spawned[index]))
					OrderAttack(game, spawner->spawned[index], victim, maxShots, source);
		if (unit != victim)
			OrderAttack(game, unit, victim, maxShots, source);
	}
}
}
