export module games.generalszh.gameplay.orders.algorithms.group_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.rts.movement.algorithms.formation_layout;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.containment.components.transport;

// AIGroup::groupMoveToPosition (without a waypoint appended, or a formation made by groupCreateFormation): the members
// with an AI that are not held, as one group to `destination`.
// - The destination is kept four pathfinding cells inside the map (clampWaypointPosition), further for helicopters
//   (their major radius) and aircraft (ten cells).
// - A player's order inside the members' area (grown by GroupMoveClickToGatherAreaFactor) tightens the group instead
//   when that area is under 2000 cells (its width squared: the original's dy repeats dx): each moves straight for the
//   point, nearest first, helicopters to their places around it (getHelicopterOffset). Not for airborne aircraft.
// - Else the mobile ground infantry and vehicles with a speed, when more than one, take slots in a formation facing
//   the move (navigation::planFormation, 22 apart); the rest go to the destination offset as they stand from the
//   member nearest it, at most six of their bounding radii (computeIndividualDestination).
// Each is ordered, nearest the destination first (aiMoveToPosition). Not yet: groupTightenToPosition's approach path
// (its units route to the point as any move), the columns of infantry and vehicles a lone group member or a failed
// formation falls back to (friend_moveInfantryToPos / friend_moveVehicleToPos: they go as the rest), a stealth unit's
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

void GroupMoveToPosition(GameWorld &game, std::span<const ecs::Entity> group, Engine::Math::FixedVector2 destination, bool fromPlayer)
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
	const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
	const Fixed gather = game.templates.Content().gameData.groupMoveClickToGatherFactor;
	bool tighten = false;
	if (fromPlayer && gather > Fixed{})
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
			extra = std::max(extra, object->geometry.majorRadius);
		else if (object->Is("AIRCRAFT"))
		{
			if (!ground(unit))
				tighten = false; // don't tighten aircraft
			extra = std::max(extra, cell * 10);
		}
	}
	// clampWaypointPosition (an Int margin).
	const Fixed margin = Fixed::FromInt((cell * 4 + extra).Floor());
	const auto [mapLow, mapHigh] = game.ground.Extent();
	destination = {std::clamp(destination.x, mapLow.x + margin, std::max(mapLow.x + margin, mapHigh.x - margin)),
		std::clamp(destination.y, mapLow.y + margin, std::max(mapLow.y + margin, mapHigh.y - margin))};
	const auto nearestFirst = [&](std::vector<ecs::Entity> &units) {
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
					OrderMove(game, unit, destination, fromPlayer);
			}
			return;
		}
	}
	// The formation: mobile ground infantry and vehicles with a speed.
	std::vector<gp::FormationUnit> formed;
	for (const ecs::Entity unit : members)
	{
		const content::ObjectDefinition *object = definition(unit);
		const auto *motion = world.Get<gp::Locomotion>(unit);
		if (object == nullptr || motion == nullptr || !detail::Mobile(game, unit) || !ground(unit) || object->Is("CLIFF_JUMPER") ||
			!(object->Is("INFANTRY") || object->Is("VEHICLE")) || motion->locomotor.maxSpeed <= Fixed{})
			continue;
		formed.push_back({unit.index, world.Get<gp::Transform>(unit)->position.XY(), content::BoundingCircleRadius(object->geometry),
			world.Get<gp::DefinitionRef>(unit)->index, static_cast<std::uint32_t>(std::max<std::int32_t>(0, object->buildCost))});
	}
	std::vector<std::pair<ecs::Entity, FixedVector2>> slots;
	if (formed.size() > 1)
	{
		const gp::FormationPlan plan = gp::PlanFormation(formed, destination, mapLow + FixedVector2{margin, margin}, mapHigh - FixedVector2{margin, margin});
		for (const gp::FormationSlot &slot : plan.slots)
			for (const ecs::Entity unit : members)
				if (unit.index == slot.id)
					slots.emplace_back(unit, slot.position);
	}
	// In the formation: by id; the rest as they stand.
	if (!slots.empty())
		std::stable_sort(movers.begin(), movers.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	nearestFirst(movers);
	FixedVector2 centre;
	bool first = true;
	for (const ecs::Entity unit : movers)
	{
		const FixedVector2 at = world.Get<gp::Transform>(unit)->position.XY();
		if (first)
		{
			centre = at; // the nearest member stands for the group's centre
			first = false;
		}
		FixedVector2 goal;
		const auto slot = std::find_if(slots.begin(), slots.end(), [&](const auto &entry) { return entry.first == unit; });
		if (slot != slots.end())
			goal = slot->second;
		else
		{
			// computeIndividualDestination: its offset from the centre, at most six bounding radii.
			const FixedVector2 offset = at - centre;
			const content::ObjectDefinition *object = definition(unit);
			const Fixed limit = object != nullptr ? content::BoundingCircleRadius(object->geometry) * 6 : Fixed{};
			const Fixed length = Engine::Math::Length(offset);
			goal = length > Fixed{} ? destination + offset / length * std::min(length, limit) : destination;
		}
		OrderMove(game, unit, goal, fromPlayer);
	}
}
}
