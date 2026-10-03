export module games.generalszh.gameplay.orders.algorithms.group_columns;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
import engine.gameplay.rts.navigation.algorithms.route_search;
import engine.gameplay.rts.navigation.algorithms.view_blocked;
import engine.gameplay.rts.navigation.resources.goal_cells;
import engine.gameplay.rts.navigation.components.pathfind_goal;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.algorithms.move_paths;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.containment.components.transport;

// A group's ground move as columns (EA's AIGroup.cpp: friend_computeGroundPath, friend_moveInfantryToPos,
// friend_moveVehicleToPos), free functions over the group's members in their order:
// - ComputeGroundPath: every member's goal let go (removeGoal); from the infantry or ground vehicle nearest the group's
//   centre (min/max/centre over the members with an AI that are not held) to the destination, a ground path
//   (findGroundPath, PATH_DIAMETER_IN_CELLS 6) when the group is to move far enough: the nearest of them to the
//   destination (or the group's own span, past DistanceRequiresGroup) at least MinDistanceForGroup off, and moving past
//   DistanceRequiresGroup, more than six infantry or four vehicles, or every infantry with a straight passable line to
//   that centre.
// - MoveInfantryColumns / MoveVehicleColumns: with at least MinInfantryForGroup infantry (no mob nexus among them) or
//   MinVehiclesForGroup ground vehicles, each takes a column (three of infantry, five at the end for sixteen or more; two
//   of vehicles, three at the end for five or more), evened out by GroupMovementPriority (infantry), offset along the
//   path's corners and, at the destination, across the path's last leg and back along it a place per member of its
//   column (and a cell per priority step behind MOVES_FRONT, infantry); its goal adjusted and claimed at once
//   (adjustDestination, updateGoal) and followed as a path (aiFollowPath).
// The ground path is the modern route search's (ground surfaces, any width; its corners on cell corners as the
// original's adjustCoordToCell without centring), not the original's diameter-weighted A*: on open ground both are the
// straight line from the start to the goal cell.
export namespace generalszh::gameplay
{
namespace group_column_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline constexpr std::int32_t PathDiameterInCells = 6;
inline constexpr std::uint8_t MovesBack = 0, MovesMiddle = 1, MovesFront = 2;

inline Fixed Cell() noexcept { return Fixed::FromInt(gp::PathfindCellSize); }

// SimpleObjectIterator: insert puts a member in front; sort is a stable merge sort (far to near: the greater first).
struct Clumps
{
	struct Clump
	{
		ecs::Entity unit;
		Fixed numeric;
		// AIUpdateInterface::setTmpValue: its wide (five or three) and narrow (three or two) column.
		std::int32_t wide{0};
		std::int32_t narrow{0};
	};
	std::deque<Clump> list;
	void Insert(ecs::Entity unit, Fixed numeric, std::int32_t wide = 0, std::int32_t narrow = 0) { list.push_front({unit, numeric, wide, narrow}); }
	void SortFarToNear()
	{
		std::stable_sort(list.begin(), list.end(), [](const Clump &a, const Clump &b) { return a.numeric > b.numeric; });
	}
};

// Coord2D::normalize (zero stays zero).
inline FixedVector2 Unit(FixedVector2 v) { return Engine::Math::Normalize(v); }

struct Member
{
	const content::ObjectDefinition *object{nullptr};
	bool held{false};
	bool ai{false};
};

inline Member Describe(GameWorld &game, ecs::Entity unit)
{
	auto &world = game.world;
	Member member;
	if (!world.IsAlive(unit))
		return member;
	if (const auto *ref = world.Get<gp::DefinitionRef>(unit))
		member.object = &game.templates.DefinitionAt(ref->index);
	const auto *off = world.Get<gp::Disabled>(unit);
	member.held = world.Has<gp::Passenger>(unit) || (off != nullptr && (off->mask & gp::disabled_type::Held) != 0);
	member.ai = world.Get<gp::Transform>(unit) != nullptr && (world.Get<gp::MoveOrder>(unit) != nullptr || world.Get<gp::Locomotion>(unit) != nullptr);
	return member;
}

inline bool Is(const Member &member, std::string_view kind) { return member.object != nullptr && member.object->Is(kind); }

inline FixedVector2 At(GameWorld &game, ecs::Entity unit) { return game.world.Get<gp::Transform>(unit)->position.XY(); }

// AIUpdateInterface::isDoingGroundMovement.
inline bool GroundMover(GameWorld &game, ecs::Entity unit)
{
	const auto *motion = game.world.Get<gp::Locomotion>(unit);
	return motion != nullptr && !gp::IsAirborne(motion->locomotor);
}

// Locomotor::getMovePriority of its current locomotor (`fallback` without one).
inline std::uint8_t Priority(GameWorld &game, ecs::Entity unit, std::uint8_t fallback)
{
	const auto *motion = game.world.Get<gp::Locomotion>(unit);
	return motion != nullptr ? motion->locomotor.groupPriority : fallback;
}

// AIGroup::getCenter: the members with an AI that are not held (none: all those not held).
inline std::optional<FixedVector2> Centre(GameWorld &game, std::span<const ecs::Entity> group)
{
	FixedVector2 sum;
	std::int64_t count = 0;
	for (const bool needAi : {true, false})
	{
		for (const ecs::Entity unit : group)
		{
			const Member member = Describe(game, unit);
			if (member.held || game.world.Get<gp::Transform>(unit) == nullptr || (needAi && !member.ai))
				continue;
			sum += At(game, unit);
			++count;
		}
		if (count > 0 || group.empty())
			break;
	}
	if (count == 0)
		return std::nullopt;
	return sum / Fixed::FromInt(count);
}

// clampToMap: a human player's to the visible map, a computer's to the whole pathfinding area, a cell inside.
inline FixedVector2 ClampToMap(GameWorld &game, FixedVector2 dest, bool human)
{
	FixedVector2 low, high;
	if (human)
	{
		const auto [mapLow, mapHigh] = game.ground.Extent();
		low = mapLow;
		high = mapHigh;
	}
	else
	{
		const auto &grid = game.world.Resource<gp::NavigationGrid>();
		high = {Fixed::FromInt(grid.Width()) * Cell(), Fixed::FromInt(grid.Height()) * Cell()};
	}
	low = low + FixedVector2{Cell(), Cell()};
	high = high - FixedVector2{Cell(), Cell()};
	return {std::clamp(dest.x, low.x, std::max(low.x, high.x)), std::clamp(dest.y, low.y, std::max(low.y, high.y))};
}

// Pathfinder::clearCellForDiameter (not a crusher; standing units are not looked at): the widest of `diameter`,
// `diameter` - 2, ... whose cells about the cell (its outer corners left out past a radius of one) are all clear (a
// fence an obstacle); 1 for a lone clear cell; 0 none; off the map nothing.
inline std::int32_t ClearCellForDiameter(const gp::NavigationGrid &grid, std::int32_t cellX, std::int32_t cellY, std::int32_t diameter)
{
	for (;;)
	{
		const std::int32_t radius = diameter / 2;
		const std::int32_t above = radius == 0 ? radius + 1 : radius;
		const bool cutCorners = radius > 1;
		bool clear = true;
		for (std::int32_t i = cellX - radius; i < cellX + above && clear; ++i)
		{
			const bool xEdge = i == cellX - radius || i == cellX + above - 1;
			for (std::int32_t j = cellY - radius; j < cellY + above; ++j)
			{
				const bool yEdge = j == cellY - radius || j == cellY + above - 1;
				if (xEdge && yEdge && cutCorners)
					continue;
				if (!grid.Contains(i, j))
					return 0;
				if (grid.Type(i, j) != gp::PathfindCellType::Clear)
				{
					clear = false;
					break;
				}
			}
		}
		if (clear)
			return radius == 0 ? 1 : 2 * radius;
		if (diameter < 2)
			return 0;
		diameter -= 2;
	}
}

// Pathfinder::isLinePassable for an infantry member (its footprint along the cells of the line, blocked units not
// looked at).
inline bool LinePassable(GameWorld &game, ecs::Entity unit, FixedVector2 to)
{
	const auto &grid = game.world.Resource<gp::NavigationGrid>();
	const auto *agent = game.world.Get<gp::NavigationAgent>(unit);
	if (agent == nullptr)
		return true;
	const gp::ClearancePlane *plane = grid.ClearanceFor(agent->surfaces);
	if (plane == nullptr)
		return true;
	const FixedVector2 from = At(game, unit);
	// linePassableCallback -> checkForMovement: every cell of its footprint about each cell of the line (getRadiusAndCenter:
	// from radius before it to radius after, one more when centred) one it may move on.
	const gp::GoalFootprint footprint = FootprintOf(*agent);
	const std::int32_t reach = footprint.radius, above = footprint.radius + (footprint.centered ? 1 : 0);
	return gp::VisitCellsAlongLine(grid, gp::WorldToCell(from.x), gp::WorldToCell(from.y), gp::WorldToCell(to.x), gp::WorldToCell(to.y),
			   [&](std::int32_t x, std::int32_t y) -> std::int32_t {
				   for (std::int32_t cx = x - reach; cx < x + above; ++cx)
					   for (std::int32_t cy = y - reach; cy < y + above; ++cy)
						   if (!gp::Passable(grid, *plane, cx, cy, 0))
							   return 1;
				   return 0;
			   }) == 0;
}

// Pathfinder::findGroundPath(from, to, 6, false): the goal cell moved (rings of up to 7 out) until the path's whole
// diameter fits there, none: no path; the nodes from `from` to the goal cell's corner point (adjustCoordToCell), its
// corners on cell corners.
inline std::optional<std::vector<FixedVector2>> FindGroundPath(GameWorld &game, FixedVector2 from, FixedVector2 to)
{
	if (to.x == Fixed{} && to.y == Fixed{})
		return std::nullopt;
	const auto &grid = game.world.Resource<gp::NavigationGrid>();
	const gp::ClearancePlane *plane = grid.ClearanceFor(gp::locomotor_surface::Ground);
	if (plane == nullptr)
		for (const gp::ClearancePlane &each : grid.Clearance())
			if ((each.surfaces & gp::locomotor_surface::Ground) != 0)
			{
				plane = &each;
				break;
			}
	if (plane == nullptr)
		return std::nullopt;
	std::int32_t cellX = gp::WorldToCell(to.x), cellY = gp::WorldToCell(to.y);
	if (ClearCellForDiameter(grid, cellX, cellY, PathDiameterInCells) != PathDiameterInCells)
	{
		constexpr std::int32_t MaxOffset = 8;
		std::int32_t offset = 1;
		const auto fits = [&] { return ClearCellForDiameter(grid, cellX, cellY, PathDiameterInCells) == PathDiameterInCells; };
		while (offset < MaxOffset)
		{
			const std::int32_t baseX = cellX, baseY = cellY;
			cellX += offset;
			if (fits())
				break;
			cellY += offset;
			if (fits())
				break;
			cellX -= offset;
			if (fits())
				break;
			cellX -= offset;
			if (fits())
				break;
			cellY -= offset;
			if (fits())
				break;
			cellY -= offset;
			if (fits())
				break;
			cellX += offset;
			if (fits())
				break;
			cellX += offset;
			if (fits())
				break;
			++offset;
			cellX = baseX;
			cellY = baseY;
		}
		if (offset >= MaxOffset)
			return std::nullopt;
	}
	if (!grid.Contains(cellX, cellY))
		return std::nullopt;
	// adjustCoordToCell(centerInCell false): (cell + 0.05) * PATHFIND_CELL_SIZE.
	const auto corner = [](std::int32_t x, std::int32_t y) {
		return FixedVector2{Fixed::FromInt(x) * Cell() + Fixed::FromRatio(1, 2), Fixed::FromInt(y) * Cell() + Fixed::FromRatio(1, 2)};
	};
	const FixedVector2 goal = corner(cellX, cellY);
	gp::RouteScratch scratch;
	const gp::FoundRoute route = gp::FindRoute(grid, *plane, gp::RouteMover{0, 0xFFFFFFFFu, {}}, from, goal, scratch);
	if (!route.reachedGoal)
		return std::nullopt;
	std::vector<FixedVector2> nodes;
	nodes.push_back(from);
	for (std::size_t index = 1; index + 1 < route.points.size(); ++index)
		nodes.push_back(corner(gp::WorldToCell(route.points[index].x), gp::WorldToCell(route.points[index].y)));
	if (nodes.back() != goal || nodes.size() == 1)
		nodes.push_back(goal);
	return nodes;
}

// aiFollowPath(path, NULL, cmdSource): on to its first point, then the rest (MovePath); its goal already claimed.
inline void FollowGroupPath(GameWorld &game, ecs::Entity unit, const std::vector<FixedVector2> &path, bool fromPlayer)
{
	auto &world = game.world;
	OrderMove(game, unit, path.front(), fromPlayer, true, path.size() > 1 ? gp::GoalClaim::None : gp::GoalClaim::Keep);
	if (path.size() < 2)
		return;
	// It did not take the move (asleep, locked, carried, immobile): nothing to follow.
	if (const auto *order = world.Get<gp::MoveOrder>(unit); order == nullptr || order->mode != gp::MoveMode::Point || order->destination != path.front())
		return;
	// The whole path (the goal path is as long as it needs to be); the first leg is under way.
	gp::SetMovePath(world, unit, path, 1, gp::MovePathKind::Follow);
}

// adjustDestination(obj, locomotorSet, dest, NULL) and updateGoal: claimed at once, as it is ordered.
inline FixedVector2 ClaimGoal(GameWorld &game, ecs::Entity unit, FixedVector2 dest)
{
	auto &world = game.world;
	if (const auto adjusted = AdjustDestinationFor(world, unit, dest))
		dest = *adjusted;
	auto *cells = world.FindResource<gp::GoalCells>();
	auto *claim = world.Get<gp::PathfindGoal>(unit);
	const auto *agent = world.Get<gp::NavigationAgent>(unit);
	if (cells != nullptr && claim != nullptr && agent != nullptr)
		gp::UpdateGoal(*cells, unit, *claim, FootprintOf(*agent), dest);
	return dest;
}

// The column moves' shared geometry: the path's start and end, the nodes six cells on from the start and last six
// cells short of the end, and the vectors along and across them.
struct Ends
{
	std::size_t startNode{0}, endNode{0};
	FixedVector2 startPoint, endPoint;
	FixedVector2 startVector, endVector, startNormal, endNormal;
};

inline std::optional<Ends> EndsOf(const std::vector<FixedVector2> &nodes, bool vehicles)
{
	const Fixed farEnough = Cell() * Fixed::FromInt(PathDiameterInCells);
	const Fixed farEnoughSqr = farEnough * farEnough;
	Ends ends;
	ends.startPoint = nodes.front();
	std::optional<std::size_t> start, end;
	for (std::size_t index = 0; index < nodes.size(); ++index)
		if (Engine::Math::DistanceSquared(nodes[index], ends.startPoint) > farEnoughSqr)
		{
			start = index;
			break;
		}
	ends.endPoint = nodes.back();
	for (std::size_t index = 0; index < nodes.size(); ++index)
		if (Engine::Math::DistanceSquared(nodes[index], ends.endPoint) > farEnoughSqr)
			end = index;
	if (vehicles && end && *end == 0)
		end.reset(); // friend_moveVehicleToPos: an end node that is the path's first is none
	if (!start || !end)
		return std::nullopt;
	ends.startNode = *start;
	ends.endNode = *end;
	ends.startVector = Unit(nodes[*start] - ends.startPoint);
	ends.endVector = Unit(ends.endPoint - nodes[*end]);
	ends.startNormal = Unit({Fixed{} - ends.startVector.y, ends.startVector.x});
	ends.endNormal = Unit({Fixed{} - ends.endVector.y, ends.endVector.x});
	return ends;
}

// Each unit's way along the path's corners, offset across the corner by `across` (its column, a cell apart for odd and
// even places in its column), only points that go on the way the corner goes.
inline std::vector<FixedVector2> CornerPath(GameWorld &game, const std::vector<FixedVector2> &nodes, const Ends &ends, ecs::Entity unit,
	Fixed columnOffset, std::int32_t column, std::int32_t factor, bool human)
{
	const Fixed farEnough = Cell() * Fixed::FromInt(PathDiameterInCells);
	const Fixed farEnoughSqr = farEnough * farEnough;
	std::vector<FixedVector2> path;
	std::size_t node = ends.startNode;
	std::size_t previous = 0;
	FixedVector2 prevPos = At(game, unit);
	while (node < nodes.size())
	{
		FixedVector2 dest = nodes[node];
		std::optional<std::size_t> next;
		for (std::size_t index = node + 1; index < nodes.size(); ++index)
			if (Engine::Math::DistanceSquared(nodes[index], dest) > farEnoughSqr)
			{
				next = index;
				break;
			}
		if (!next)
			break;
		const FixedVector2 cornerVector = nodes[*next] - nodes[previous];
		const FixedVector2 cornerNormal = Unit({Fixed{} - cornerVector.y, cornerVector.x});
		dest = dest + cornerNormal * (columnOffset * Fixed::FromInt(column));
		const Fixed half = Cell() / Fixed::FromInt(2);
		dest = (factor & 1) != 0 ? dest + cornerNormal * half : dest - cornerNormal * half;
		const FixedVector2 curVector = dest - prevPos;
		dest = ClampToMap(game, dest, human);
		if (Engine::Math::Dot(cornerVector, curVector) > Fixed{})
		{
			path.push_back(dest);
			prevPos = dest;
		}
		++node;
		for (std::size_t index = previous + 1; index < nodes.size() && index != node; ++index)
			if (Engine::Math::DistanceSquared(nodes[index], nodes[node < nodes.size() ? node : nodes.size() - 1]) > farEnoughSqr)
				previous = index;
	}
	return path;
}

// The tail: the points that no longer go the way the path's last leg goes are dropped, the goal claimed and added.
inline void FinishPath(GameWorld &game, ecs::Entity unit, std::vector<FixedVector2> &path, const Ends &ends, FixedVector2 dest, bool human,
	bool fromPlayer)
{
	while (!path.empty() && Engine::Math::Dot(ends.endVector, dest - path.back()) <= Fixed{})
		path.pop_back();
	dest = ClampToMap(game, dest, human);
	dest = ClaimGoal(game, unit, dest);
	path.push_back(dest);
	FollowGroupPath(game, unit, path, fromPlayer);
}

inline bool HumanOwner(GameWorld &game, ecs::Entity unit)
{
	const auto *owner = game.world.Get<gp::Owner>(unit);
	return owner != nullptr && owner->player < game.roster.PlayerCount() && game.roster.PlayerAt(owner->player).human;
}

// The evening out of the columns (both moves): each takes the least filled column nearest the one it was given.
inline std::int32_t EvenOut(std::span<std::int32_t> columns, std::int32_t wanted, std::int32_t step)
{
	std::int32_t least = 10000;
	for (std::size_t index = 0; index < columns.size(); index += static_cast<std::size_t>(step))
		least = std::min(least, columns[index]);
	std::int32_t delta = 10000, best = -1;
	for (std::size_t index = 0; index < columns.size(); index += static_cast<std::size_t>(step))
		if (columns[index] == least)
		{
			const std::int32_t dx = std::abs(wanted - static_cast<std::int32_t>(index));
			if (dx < delta)
			{
				delta = dx;
				best = static_cast<std::int32_t>(index);
			}
		}
	if (best >= 0)
		++columns[static_cast<std::size_t>(best)];
	return best;
}
}

// AIGroup::friend_computeGroundPath (the group's ground path, or none: no column moves).
std::optional<std::vector<Engine::Math::FixedVector2>> ComputeGroundPath(GameWorld &game, std::span<const ecs::Entity> group, Engine::Math::FixedVector2 pos)
{
	using namespace group_column_detail;
	auto &world = game.world;
	const auto &data = game.templates.Content().aiData;
	// getMinMaxAndCenter.
	FixedVector2 low{Fixed::FromInt(1'000'000), Fixed::FromInt(1'000'000)}, high{Fixed::FromInt(-1'000'000), Fixed::FromInt(-1'000'000)};
	FixedVector2 centre;
	std::int64_t counted = 0;
	for (const ecs::Entity unit : group)
	{
		const Member member = Describe(game, unit);
		if (member.held || !member.ai)
			continue;
		const FixedVector2 at = At(game, unit);
		centre += at;
		low = {std::min(low.x, at.x), std::min(low.y, at.y)};
		high = {std::max(high.x, at.x), std::max(high.y, at.y)};
		++counted;
	}
	if (counted > 0)
		centre = centre / Fixed::FromInt(counted);
	const Fixed groupDistance = data.distanceRequiresGroup;
	Fixed distSqr = Fixed::FromInt(4) * groupDistance * groupDistance;
	std::int32_t infantry = 0, vehicles = 0;
	std::optional<ecs::Entity> centreUnit;
	Fixed centreDistSqr = distSqr * Fixed::FromInt(10);
	auto *cells = world.FindResource<gp::GoalCells>();
	for (const ecs::Entity unit : group)
	{
		const Member member = Describe(game, unit);
		if (!world.IsAlive(unit))
			continue;
		// removeGoal.
		if (auto *claim = world.Get<gp::PathfindGoal>(unit); claim != nullptr && cells != nullptr)
			if (const auto *agent = world.Get<gp::NavigationAgent>(unit))
				gp::RemoveGoal(*cells, unit, *claim, FootprintOf(*agent));
		if (member.held || !member.ai)
			continue;
		if (Is(member, "INFANTRY"))
			++infantry;
		else if (Is(member, "VEHICLE"))
		{
			if (Is(member, "AIRCRAFT"))
				continue;
			++vehicles;
		}
		else
			continue;
		const FixedVector2 at = At(game, unit);
		distSqr = std::min(distSqr, Engine::Math::DistanceSquared(at, pos));
		const Fixed fromCentre = Engine::Math::DistanceSquared(at, centre);
		if (!centreUnit || fromCentre < centreDistSqr)
		{
			centreUnit = unit;
			centreDistSqr = fromCentre;
		}
	}
	if (!centreUnit)
		return std::nullopt;
	const FixedVector2 from = At(game, *centreUnit);
	const FixedVector2 span = high - low;
	if (Engine::Math::Dot(span, span) > groupDistance * groupDistance)
		distSqr = Engine::Math::Dot(span, span);
	if (distSqr < data.minDistanceForGroup * data.minDistanceForGroup)
		return std::nullopt;
	bool closeEnough = distSqr > groupDistance * groupDistance || infantry > 6 || vehicles > 4;
	if (!closeEnough)
	{
		bool passable = true;
		for (const ecs::Entity unit : group)
		{
			const Member member = Describe(game, unit);
			if (!Is(member, "INFANTRY") || !member.ai)
				continue;
			if (!LinePassable(game, unit, from))
				passable = false;
		}
		closeEnough = passable;
	}
	if (!closeEnough)
		return std::nullopt;
	return FindGroundPath(game, from, pos);
}

// AIGroup::friend_moveInfantryToPos: whether its infantry went as columns.
bool MoveInfantryColumns(GameWorld &game, std::span<const ecs::Entity> group, const std::vector<Engine::Math::FixedVector2> &nodes,
	Engine::Math::FixedVector2 pos, bool fromPlayer)
{
	using namespace group_column_detail;
	const auto centre = Centre(game, group);
	if (!centre)
		return false;
	auto ends = EndsOf(nodes, false);
	if (!ends)
		return false;
	constexpr std::int32_t numColumns = 3, halfNumColumns = numColumns / 2;
	Clumps iter, iter2;
	std::int32_t unitsToPath = 0;
	bool useEndVector = false;
	bool human = false;
	for (const ecs::Entity unit : group)
	{
		const Member member = Describe(game, unit);
		if (member.held || !Is(member, "INFANTRY") || !member.ai)
			continue;
		if (Is(member, "MOB_NEXUS"))
			return false; // no column: the nexus keeps a far goal for its mob to aim at
		human = HumanOwner(game, unit);
		const FixedVector2 at = At(game, unit);
		iter.Insert(unit, Engine::Math::Dot(at - *centre, ends->startNormal));
		++unitsToPath;
		if (Engine::Math::DistanceSquared(at, ends->startPoint) > Engine::Math::DistanceSquared(at, ends->endPoint))
			useEndVector = true;
	}
	if (unitsToPath < game.templates.Content().aiData.minInfantryForGroup)
		return false;
	if (useEndVector)
	{
		ends->startVector = ends->endVector;
		ends->startNormal = ends->endNormal;
		for (const auto &clump : iter.list)
			iter2.Insert(clump.unit, {});
		iter.list.clear();
		for (const auto &clump : iter2.list)
			iter.Insert(clump.unit, Engine::Math::Dot(At(game, clump.unit) - *centre, ends->startNormal));
		iter2.list.clear();
	}
	iter.SortFarToNear();
	std::int32_t curIndex = 0;
	for (const auto &clump : iter.list)
	{
		std::int32_t divisor = std::max((unitsToPath + 1) / numColumns, 1);
		const std::int32_t columnDelta = std::max(1 - curIndex / divisor, -halfNumColumns);
		divisor = std::max((unitsToPath + 3) / 5, 1);
		std::int32_t fiveColumnDelta = std::max(2 - curIndex / divisor, -2);
		if (unitsToPath < 16)
			fiveColumnDelta = columnDelta;
		const std::uint8_t priority = Priority(game, clump.unit, MovesFront);
		const Fixed adjust = priority == MovesMiddle ? Fixed{} - Cell() * Fixed::FromInt(100)
			: priority == MovesBack                  ? Fixed{} - Cell() * Fixed::FromInt(200)
													 : Fixed{};
		iter2.Insert(clump.unit, adjust + Engine::Math::Dot(At(game, clump.unit) - *centre, ends->startVector), fiveColumnDelta, columnDelta);
		++curIndex;
	}
	iter2.SortFarToNear();
	// Even out the columns, front movers first.
	std::array<std::int32_t, 3> column3{};
	std::array<std::int32_t, 5> column5{};
	for (std::int32_t group_ = MovesFront; group_ >= MovesBack; --group_)
		for (auto &clump : iter2.list)
		{
			if (Priority(game, clump.unit, MovesMiddle) != group_)
				continue;
			std::int32_t &fiveColumnDelta = clump.wide;
			std::int32_t &columnDelta = clump.narrow;
			if (const std::int32_t best = EvenOut(column3, 1 + columnDelta, 1); best >= 0)
				columnDelta = best - 1;
			if (const std::int32_t best = EvenOut(column5, 2 + fiveColumnDelta, 1); best >= 0)
				fiveColumnDelta = best - 2;
			if (unitsToPath < 16)
				fiveColumnDelta = columnDelta;
		}
	std::array<std::int32_t, 5> columnFactor{};
	for (const auto &clump : iter2.list)
	{
		const ecs::Entity unit = clump.unit;
		std::int32_t fiveColumnDelta = clump.wide;
		const std::int32_t columnDelta = clump.narrow;
		const std::int32_t factor = columnFactor[static_cast<std::size_t>(fiveColumnDelta + 2)]++;
		std::vector<FixedVector2> path = CornerPath(game, nodes, *ends, unit, Cell() * Fixed::FromRatio(21, 10) / Fixed::FromInt(halfNumColumns), columnDelta,
			factor, human);
		fiveColumnDelta = std::clamp(fiveColumnDelta, -2, 2);
		const Fixed offset = Cell() * Fixed::FromRatio(22, 10);
		FixedVector2 dest = pos + ends->endNormal * (offset * Fixed::FromInt(fiveColumnDelta));
		if ((factor & 1) != 0)
			dest = dest + ends->endNormal * Cell();
		const std::int32_t delta = static_cast<std::int32_t>(Priority(game, unit, MovesMiddle)) - MovesFront;
		dest = dest + ends->endVector * (Cell() * Fixed::FromInt(delta));
		dest = dest - ends->endVector * (offset * Fixed::FromInt(factor));
		FinishPath(game, unit, path, *ends, dest, human, fromPlayer);
	}
	return true;
}

// AIGroup::friend_moveVehicleToPos: whether its ground vehicles went as columns.
bool MoveVehicleColumns(GameWorld &game, std::span<const ecs::Entity> group, const std::vector<Engine::Math::FixedVector2> &nodes,
	Engine::Math::FixedVector2 pos, bool fromPlayer)
{
	using namespace group_column_detail;
	const auto centre = Centre(game, group);
	if (!centre)
		return false;
	auto ends = EndsOf(nodes, true);
	if (!ends)
		return false;
	constexpr std::int32_t numColumns = 2;
	Clumps iter, iter2;
	std::int32_t unitsToPath = 0;
	bool useEndVector = false;
	bool human = false;
	for (const ecs::Entity unit : group)
	{
		const Member member = Describe(game, unit);
		if (member.held || !Is(member, "VEHICLE") || !member.ai || !GroundMover(game, unit))
			continue;
		human = HumanOwner(game, unit);
		const FixedVector2 at = At(game, unit);
		iter.Insert(unit, Engine::Math::Dot(at - *centre, ends->startNormal));
		++unitsToPath;
		if (Engine::Math::DistanceSquared(at, ends->startPoint) > Engine::Math::DistanceSquared(at, ends->endPoint))
			useEndVector = true;
	}
	if (unitsToPath < game.templates.Content().aiData.minVehiclesForGroup)
		return false;
	if (useEndVector)
	{
		ends->startVector = ends->endVector;
		ends->startNormal = ends->endNormal;
		for (const auto &clump : iter.list)
			iter2.Insert(clump.unit, {});
		iter.list.clear();
		for (const auto &clump : iter2.list)
			iter.Insert(clump.unit, Engine::Math::Dot(At(game, clump.unit) - *centre, ends->startNormal));
		iter2.list.clear();
	}
	iter.SortFarToNear();
	std::int32_t curIndex = 0;
	for (const auto &clump : iter.list)
	{
		std::int32_t divisor = std::max((unitsToPath + 1) / numColumns, 1);
		std::int32_t columnDelta = 1 - curIndex / divisor;
		if (columnDelta == 0)
			columnDelta = -1;
		divisor = std::max((unitsToPath + 1) / 3, 1);
		std::int32_t threeColumnDelta = std::max(1 - curIndex / divisor, -1);
		if (unitsToPath < 5)
			threeColumnDelta = columnDelta;
		iter2.Insert(clump.unit, Engine::Math::Dot(At(game, clump.unit) - *centre, ends->startVector), threeColumnDelta, columnDelta);
		++curIndex;
	}
	iter2.SortFarToNear();
	// Even out the columns: three passes over all of them (the priority test is compiled out in the original).
	std::array<std::int32_t, 3> column2{}, column3{};
	for (std::int32_t pass = 0; pass < 3; ++pass)
		for (auto &clump : iter2.list)
		{
			std::int32_t &threeColumnDelta = clump.wide;
			std::int32_t &columnDelta = clump.narrow;
			if (const std::int32_t best = EvenOut(column2, 1 + columnDelta, 2); best >= 0)
				columnDelta = best - 1;
			if (const std::int32_t best = EvenOut(column3, 1 + threeColumnDelta, 1); best >= 0)
				threeColumnDelta = best - 1;
			if (unitsToPath < 5)
				threeColumnDelta = columnDelta;
		}
	std::array<std::int32_t, 5> columnFactor{};
	for (const auto &clump : iter2.list)
	{
		const ecs::Entity unit = clump.unit;
		std::int32_t threeColumnDelta = clump.wide;
		const std::int32_t columnDelta = clump.narrow;
		const std::int32_t factor = columnFactor[static_cast<std::size_t>(threeColumnDelta + 2)]++;
		std::vector<FixedVector2> path = CornerPath(game, nodes, *ends, unit, Cell() * Fixed::FromRatio(3, 2), columnDelta, factor, human);
		threeColumnDelta = std::clamp(threeColumnDelta, -3, 3);
		const Fixed offset = unitsToPath < 5 ? Cell() * Fixed::FromRatio(3, 2) : Cell() * Fixed::FromRatio(32, 10);
		FixedVector2 dest = pos + ends->endNormal * (offset * Fixed::FromInt(threeColumnDelta));
		if ((factor & 1) != 0)
			dest = dest + ends->endNormal * Cell();
		dest = dest - ends->endVector * (offset * Fixed::FromInt(factor));
		FinishPath(game, unit, path, *ends, dest, human, fromPlayer);
	}
	return true;
}
}
