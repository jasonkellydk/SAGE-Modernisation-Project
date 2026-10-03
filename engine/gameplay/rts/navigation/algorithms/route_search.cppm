export module engine.gameplay.rts.navigation.algorithms.route_search;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.algorithms.clearance;
export import engine.gameplay.rts.navigation.algorithms.unit_movement;

// Ground routes over a clearance plane, deterministic and allocation free
// once warm (the scratch is reused, stamped by generation):
//   A* on the 8-neighbour grid (straight 10, diagonal 14; a diagonal only
//   when both cells beside it are open, so no corners are cut), with the
//   octile heuristic (admissible and consistent: the route is a shortest
//   one) and a closed set; ties go to the lower estimate, then the lower
//   cell index. A cell is open for a mover whose footprint reaches `radius`
//   cells when its room exceeds that radius. The start cell is always left,
//   and a mover caught inside a structure (a new building stamped over it)
//   tunnels out: from a blocked cell it may step into the structure's cells
//   and usable cells too tight for it (Pathfinder's tunneling from an invalid
//   start), until it reaches room enough.
//   A goal nothing reaches: the route goes to the reached cell nearest it
//   (then the cheapest to get to).
//   Smoothing: from each kept point, on to the farthest later point in
//   straight sight (every cell the segment passes through, corners included,
//   open, and with the units no unit standing fixed in the mover's footprint
//   there), so routes run straight across open ground and turn at corners.
// Points are cell centres; the first is where the mover is, the last the
// goal itself when that was reached.
// With the units (RouteUnits: Pathfinder::examineNeighboringCells over checkForMovement): a cell a unit that is no ally
// stands fixed in (and the mover does not crush) is closed; one an ally stands fixed in costs 3 diagonal steps (42) more
// and marks the route blocked by allies; one an ally passes through within 10 cells of the start costs 42 more. A mover
// starting among units it may not pass tunnels (as from a blocked start) until it reaches a cell it may.
export namespace engine::gameplay
{
struct RouteMover
{
	std::uint8_t radius{0}; // cells its footprint reaches from its own
	// Cells a search may expand before it settles for the reached cell nearest the goal (the route is then
	// partial: walked, and planned on from its end).
	std::uint32_t budget{5000};
	// An obstacle whose cells are open to it (none: invalid): the factory it walks out of.
	ecs::Entity ignored;
};

struct RouteScratch
{
	// A cell's search state in one record (one cache line holds four cells' worth, not four arrays' worth of misses).
	struct Node
	{
		std::uint32_t cost;
		std::int32_t parent;
		std::uint32_t seen;   // generation it was reached in
		std::uint32_t closed; // generation it was expanded in
		std::int32_t x, y;    // where the cell is (set when it is reached)
		std::uint8_t layer;
		std::uint8_t allyBlocked; // an ally stands fixed in its footprint (setBlockedByAlly)
	};
	std::vector<Node> nodes;
	std::uint32_t generation{0};
	struct Entry
	{
		std::uint32_t f, h;
		std::int32_t cell;
		// Where it is (carried, not decoded from the index): layer and cell coordinates.
		std::int32_t x, y;
		std::uint8_t layer;
		// Keys are unique (a cell is pushed again only with a lower cost), so the order is total.
		bool operator>(const Entry &other) const noexcept
		{
			return f != other.f ? f > other.f : h != other.h ? h > other.h : cell > other.cell;
		}
	};
	std::vector<Entry> heap;
	// The open set as packed keys (f, then h, then cell, in one integer) in a 4-ary heap, when they fit 64 bits: the
	// same order as `heap`, the cheapest compares and moves.
	std::vector<std::uint64_t> keys;
	std::vector<std::int32_t> cells;

	void PushKey(std::uint64_t key)
	{
		keys.push_back(key);
		std::size_t at = keys.size() - 1;
		while (at > 0)
		{
			const std::size_t parent = (at - 1) / 4;
			if (keys[parent] <= key)
				break;
			keys[at] = keys[parent];
			at = parent;
		}
		keys[at] = key;
	}
	std::uint64_t PopKey()
	{
		const std::uint64_t top = keys.front();
		const std::uint64_t last = keys.back();
		keys.pop_back();
		const std::size_t count = keys.size();
		if (count == 0)
			return top;
		std::size_t at = 0;
		for (;;)
		{
			const std::size_t first = at * 4 + 1;
			if (first >= count)
				break;
			std::size_t least = first;
			const std::size_t end = std::min(first + 4, count);
			for (std::size_t child = first + 1; child < end; ++child)
				least = keys[child] < keys[least] ? child : least;
			if (keys[least] >= last)
				break;
			keys[at] = keys[least];
			at = least;
		}
		keys[at] = last;
		return top;
	}

	void Prepare(std::size_t count)
	{
		if (nodes.size() != count)
		{
			nodes.assign(count, Node{0, -1, 0, 0, 0, 0, 0, 0});
			generation = 0;
		}
		if (++generation == 0)
		{
			for (Node &node : nodes)
				node.seen = node.closed = 0;
			generation = 1;
		}
		heap.clear();
		keys.clear();
		cells.clear();
	}
};

struct FoundRoute
{
	std::vector<Engine::Math::FixedVector2> points;
	std::vector<std::uint8_t> layers; // each point's layer
	bool reachedGoal{false};
	bool exhausted{false}; // the budget ran out first
	bool blockedByAlly{false}; // a cell along it has an ally standing fixed in the mover's footprint (Path::getBlockedByAlly)
};

namespace route_detail
{
using Engine::Math::Fixed;

inline std::int32_t CellOf(Fixed value) noexcept { return static_cast<std::int32_t>((value / Fixed::FromInt(PathfindCellSize)).Floor()); }

inline Engine::Math::FixedVector2 Centre(std::int32_t x, std::int32_t y) noexcept
{
	const Fixed cell = Fixed::FromInt(PathfindCellSize);
	return {Fixed::FromInt(x) * cell + cell / Fixed::FromInt(2), Fixed::FromInt(y) * cell + cell / Fixed::FromInt(2)};
}

inline std::uint32_t Octile(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by) noexcept
{
	const std::uint32_t dx = static_cast<std::uint32_t>(std::abs(ax - bx)), dy = static_cast<std::uint32_t>(std::abs(ay - by));
	return 10u * std::max(dx, dy) + 4u * std::min(dx, dy);
}

// Every cell the segment between two cell centres passes through, corners included, open.
template<typename Open>
bool InSight(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Open &&open)
{
	const std::int32_t dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
	const std::int32_t sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
	std::int32_t x = x0, y = y0;
	// Supercover walk: compare the crossings of vertical and horizontal cell edges (scaled by 2 * dx * dy).
	std::int64_t error = static_cast<std::int64_t>(dx) - dy;
	for (std::int32_t steps = dx + dy; steps > 0;)
	{
		if (!open(x, y))
			return false;
		if (error > 0)
		{
			x += sx;
			error -= 2 * dy;
			--steps;
		}
		else if (error < 0)
		{
			y += sy;
			error += 2 * dx;
			--steps;
		}
		else
		{
			// Through a corner: both cells beside it must be open.
			if (!open(x + sx, y) || !open(x, y + sy))
				return false;
			x += sx;
			y += sy;
			error += 2 * (dx - dy);
			steps -= 2;
		}
	}
	return open(x1, y1);
}
}

// The search both kinds of route share: toward a goal (`towardGoal`: A* to `to`, as above, ending early at a ground cell
// other than the start that `accept(x, y)` takes), or out from the start until such a cell (no goal: cost alone orders
// the search, Dijkstra; nothing accepted within the budget: no route).
template<typename Accept>
inline FoundRoute SearchRoute(const NavigationGrid &grid, const ClearancePlane &plane, RouteMover mover, Engine::Math::FixedVector2 from,
	Engine::Math::FixedVector2 to, RouteScratch &scratch, std::uint8_t fromLayer, std::uint8_t toLayer, const RouteUnits *units, bool towardGoal,
	Accept &&accept)
{
	using namespace route_detail;
	FoundRoute route;
	const std::int32_t width = grid.Width(), height = grid.Height();
	if (width <= 0 || height <= 0)
		return route;
	const std::uint8_t decks = static_cast<std::uint8_t>(grid.Decks().size());
	if (fromLayer > decks)
		fromLayer = GroundLayer;
	if (toLayer > decks)
		toLayer = GroundLayer;
	const auto clampX = [&](std::int32_t x) { return std::clamp(x, 0, width - 1); };
	const auto clampY = [&](std::int32_t y) { return std::clamp(y, 0, height - 1); };
	const std::int32_t startX = clampX(CellOf(from.x)), startY = clampY(CellOf(from.y));
	std::int32_t goalX = clampX(CellOf(to.x)), goalY = clampY(CellOf(to.y));
	const bool ignoring = mover.ignored != ecs::Entity{};
	bool unitTunnel = false; // among units it may not pass (m_isTunneling from checkForMovement)
	// The ignored obstacle's own cells, and those it alone leaves too tight for the mover (within its reach of one of
	// them, owned by no other obstacle), are open: it walks out of it, or into it.
	const auto nearIgnored = [&](std::int32_t x, std::int32_t y) {
		const ecs::Entity owner = grid.Obstacle(x, y);
		if (owner == mover.ignored)
			return true;
		if (owner != ecs::Entity{} || grid.Type(x, y) == PathfindCellType::Obstacle)
			return false;
		const std::int32_t reach = mover.radius;
		for (std::int32_t dy = -reach; dy <= reach; ++dy)
			for (std::int32_t dx = -reach; dx <= reach; ++dx)
				if (x + dx >= 0 && y + dy >= 0 && x + dx < width && y + dy < height && grid.Obstacle(x + dx, y + dy) == mover.ignored)
					return true;
		return false;
	};
	const auto open = [&](std::int32_t x, std::int32_t y) {
		return x >= 0 && y >= 0 && x < width && y < height &&
			((plane.room[grid.Index(x, y)] > mover.radius && grid.Type(x, y) != PathfindCellType::BridgeImpassable) || (ignoring && nearIgnored(x, y)));
	};
	// A cell of a layer: the ground's as above, a deck's when clear with room for the mover.
	const auto openOn = [&](std::uint8_t layer, std::int32_t x, std::int32_t y) {
		return layer == GroundLayer ? open(x, y) : DeckPassable(grid, plane, layer, x, y, mover.radius);
	};
	// Pathfinder::adjustDestination: a goal where the mover cannot stand (inside a structure's cells) becomes the open
	// cell nearest it, ring by ring out to MaxGoalAdjust cells (nearest the goal within a ring; lowest index on ties),
	// and the route ends at that cell's centre. On a deck: its own cells first, then the ground's.
	const auto adjust = [&](std::uint8_t layer) {
		constexpr std::int32_t MaxGoalAdjust = 20;
		for (std::int32_t ring = 1; ring <= MaxGoalAdjust; ++ring)
		{
			std::optional<std::pair<std::int32_t, std::int32_t>> nearest;
			Fixed nearestDistance;
			for (std::int32_t y = goalY - ring; y <= goalY + ring; ++y)
				for (std::int32_t x = goalX - ring; x <= goalX + ring; ++x)
				{
					if ((std::abs(x - goalX) != ring && std::abs(y - goalY) != ring) || !openOn(layer, x, y))
						continue;
					const Fixed distance = Engine::Math::DistanceSquared(Centre(x, y), to);
					if (!nearest || distance < nearestDistance)
					{
						nearest = std::pair{x, y};
						nearestDistance = distance;
					}
				}
			if (nearest)
			{
				goalX = nearest->first;
				goalY = nearest->second;
				to = Centre(goalX, goalY);
				return true;
			}
		}
		return false;
	};
	// A goal on terrain the mover cannot use (water, a cliff) is not adjusted: findPath fails and computePath falls back to
	// findClosestPath (pathCostFactor 0), whose route ends at the reached cell nearest the goal on any layer (`best`).
	const bool terrainGoal = toLayer == GroundLayer && grid.Contains(goalX, goalY) &&
		(grid.Type(goalX, goalY) == PathfindCellType::Water || grid.Type(goalX, goalY) == PathfindCellType::Cliff);
	if (towardGoal && !terrainGoal && !openOn(toLayer, goalX, goalY))
		if (!adjust(toLayer) && toLayer != GroundLayer)
		{
			toLayer = GroundLayer;
			if (!open(goalX, goalY))
				adjust(GroundLayer);
		}
	// The index space: the ground's cells, then each deck's (NavigationGrid::CellCount).
	const std::size_t groundCells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	std::array<std::size_t, MaxDecks + 1> offsets{};
	for (std::uint8_t layer = 1; layer <= decks; ++layer)
		offsets[layer] = grid.DeckOffset(layer);
	scratch.Prepare(grid.CellCount());
	const std::uint32_t generation = scratch.generation;
	const auto index = [&](std::uint8_t layer, std::int32_t x, std::int32_t y) {
		if (layer == GroundLayer)
			return static_cast<std::int32_t>(grid.Index(x, y));
		return static_cast<std::int32_t>(offsets[layer] + grid.Decks()[layer - 1].Index(x, y));
	};
	struct Place
	{
		std::uint8_t layer;
		std::int32_t x, y;
	};
	const auto decode = [&](std::int32_t cell) -> Place {
		const auto at = static_cast<std::size_t>(cell);
		if (at < groundCells)
			return {GroundLayer, cell % width, cell / width};
		std::uint8_t layer = decks;
		while (layer > 1 && at < offsets[layer])
			--layer;
		const DeckLayer &deck = grid.Decks()[layer - 1];
		const auto local = static_cast<std::int32_t>(at - offsets[layer]);
		return {layer, deck.x0 + local % deck.width, deck.y0 + local / deck.width};
	};
	// A start on a deck outside its cells stands on the ground.
	if (fromLayer != GroundLayer && !grid.Decks()[fromLayer - 1].Contains(startX, startY))
		fromLayer = GroundLayer;
	const std::int32_t start = index(fromLayer, startX, startY), goal = towardGoal ? index(toLayer, goalX, goalY) : -1;
	RouteScratch::Node *const nodes = scratch.nodes.data();
	nodes[start].cost = 0;
	nodes[start].parent = -1;
	nodes[start].seen = generation;
	const std::uint32_t startH = towardGoal ? Octile(startX, startY, goalX, goalY) : 0u;
	// Packed keys: f below 14 per step of the deepest path the budget allows plus the largest octile distance, h below
	// that distance, the cell below the index space.
	const std::uint64_t hLimit = 14u * (static_cast<std::uint64_t>(width) + static_cast<std::uint64_t>(height) + 2u);
	const std::uint64_t fLimit = hLimit + 14u * (static_cast<std::uint64_t>(mover.budget) + 2u) + 14u;
	const unsigned cellBits = static_cast<unsigned>(std::bit_width(static_cast<std::uint64_t>(grid.CellCount())));
	const unsigned hBits = static_cast<unsigned>(std::bit_width(hLimit));
	const unsigned fBits = static_cast<unsigned>(std::bit_width(fLimit));
	const bool packed = cellBits + hBits + fBits <= 64 && Octile(0, 0, width, height) < hLimit;
	const std::uint64_t cellMask = (std::uint64_t{1} << cellBits) - 1u, hMask = (std::uint64_t{1} << hBits) - 1u;
	const auto pushOpen = [&](std::uint32_t f, std::uint32_t h, std::int32_t cell, std::int32_t cx, std::int32_t cy, std::uint8_t layer) {
		if (packed)
		{
			RouteScratch::Node &node = scratch.nodes[static_cast<std::size_t>(cell)];
			node.x = cx;
			node.y = cy;
			node.layer = layer;
			scratch.PushKey((static_cast<std::uint64_t>(f) << (hBits + cellBits)) | (static_cast<std::uint64_t>(h) << cellBits) | static_cast<std::uint64_t>(cell));
		}
		else
		{
			scratch.heap.push_back({f, h, cell, cx, cy, layer});
			std::push_heap(scratch.heap.begin(), scratch.heap.end(), std::greater<RouteScratch::Entry>{});
		}
	};
	const auto openEmpty = [&] { return packed ? scratch.keys.empty() : scratch.heap.empty(); };
	const auto popOpen = [&]() -> RouteScratch::Entry {
		if (packed)
		{
			const std::uint64_t key = scratch.PopKey();
			const auto cell = static_cast<std::int32_t>(key & cellMask);
			const RouteScratch::Node &node = scratch.nodes[static_cast<std::size_t>(cell)];
			return {static_cast<std::uint32_t>(key >> (hBits + cellBits)), static_cast<std::uint32_t>((key >> cellBits) & hMask), cell, node.x, node.y, node.layer};
		}
		std::pop_heap(scratch.heap.begin(), scratch.heap.end(), std::greater<RouteScratch::Entry>{});
		const RouteScratch::Entry entry = scratch.heap.back();
		scratch.heap.pop_back();
		return entry;
	};
	pushOpen(startH, startH, start, startX, startY, fromLayer);
	nodes[start].allyBlocked = 0;
	// A start among units it may not pass: it tunnels out (checkForMovement on the start cell).
	if (units != nullptr && fromLayer == GroundLayer && CheckForMovement(*units, startX, startY, mover.radius).enemyFixed)
		unitTunnel = true;
	std::int32_t best = start;
	std::uint32_t bestH = startH, bestCost = 0;
	bool found = false;
	std::int32_t accepted = -1;
	std::uint32_t expanded = 0;
	static constexpr std::array<std::array<std::int32_t, 2>, 8> steps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
	// The units about a ground cell the search steps to: closed (nullopt) to a mover not tunnelling, else what they add to
	// its cost (and whether allies stand fixed there).
	struct UnitStep
	{
		std::uint32_t extra{0};
		std::uint8_t allyBlocked{0};
	};
	const auto unitStep = [&](std::int32_t nx, std::int32_t ny, bool tunnelling) -> std::optional<UnitStep> {
		if (units == nullptr)
			return UnitStep{};
		const MovementCheck check = CheckForMovement(*units, nx, ny, mover.radius);
		if (check.enemyFixed)
		{
			if (!tunnelling && !unitTunnel)
				return std::nullopt;
		}
		else
			unitTunnel = false;
		UnitStep step;
		if (check.allyMoving && std::abs(nx - startX) < 10 && std::abs(ny - startY) < 10)
			step.extra += 42u;
		if (check.allyFixedCount > 0)
		{
			step.extra += 42u;
			step.allyBlocked = units->throughUnits ? 0 : 1;
		}
		return step;
	};
	const auto relax = [&](std::uint32_t atCost, std::int32_t from, std::size_t next, std::uint32_t step, std::int32_t nx, std::int32_t ny, std::uint8_t layer,
						   UnitStep unit = {}) {
		RouteScratch::Node &node = nodes[next];
		if (node.closed == generation)
			return;
		node.allyBlocked = unit.allyBlocked;
		const std::uint32_t candidate = atCost + step + unit.extra;
		if (node.seen == generation && candidate >= node.cost)
			return;
		node.seen = generation;
		node.cost = candidate;
		node.parent = from;
		const std::uint32_t h = towardGoal ? Octile(nx, ny, goalX, goalY) : 0u;
		pushOpen(candidate + h, h, static_cast<std::int32_t>(next), nx, ny, layer);
	};
	// A ground cell open for the mover: the open test above, with the room and type read straight from their arrays.
	const std::uint8_t *const room = plane.room.data();
	const auto groundOpen = [&](std::int32_t x, std::int32_t y) {
		if (x < 0 || y < 0 || x >= width || y >= height)
			return false;
		return (room[grid.Index(x, y)] > mover.radius && grid.Type(x, y) != PathfindCellType::BridgeImpassable) || (ignoring && nearIgnored(x, y));
	};
	// Interior ground cells (a step from every edge), for a mover ignoring nothing: the neighbours' index offsets, in
	// `steps` order, and their openness straight from the room and type arrays (no bounds tests, no branches).
	const PathfindCellType *const types = grid.Types().data();
	const std::array<std::ptrdiff_t, 8> stepOffsets{1, width, -1, -width, width + 1, width - 1, -width - 1, -width + 1};
	const std::uint8_t radius = mover.radius;
	const auto interiorOpen = [&](std::size_t cell) {
		return (room[cell] > radius) & (types[cell] != PathfindCellType::BridgeImpassable);
	};
	while (!openEmpty())
	{
		const RouteScratch::Entry entry = popOpen();
		const auto at = static_cast<std::size_t>(entry.cell);
		if (nodes[at].closed == generation)
			continue;
		nodes[at].closed = generation;
		if (++expanded > mover.budget)
		{
			route.exhausted = true;
			break;
		}
		const Place here{entry.layer, entry.x, entry.y};
		const std::int32_t x = here.x, y = here.y;
		const std::uint32_t atCost = nodes[at].cost;
		if (entry.h < bestH || (entry.h == bestH && atCost < bestCost))
		{
			best = entry.cell;
			bestH = entry.h;
			bestCost = atCost;
		}
		// (Toward a goal, a ground cell `accept` takes ends the search too: Pathfinder::findAttackPath's goal test.)
		if (entry.cell == goal || (here.layer == GroundLayer && entry.cell != start && accept(x, y)))
		{
			found = true;
			accepted = entry.cell;
			break;
		}
		if (here.layer != GroundLayer)
		{
			// On a deck: its own cells (no tunnelling, no corners cut), and down to the ground at an entry cell.
			const DeckLayer &deck = grid.Decks()[here.layer - 1];
			for (std::size_t direction = 0; direction < steps.size(); ++direction)
			{
				const auto &[dx, dy] = steps[direction];
				const std::int32_t nx = x + dx, ny = y + dy;
				if (!openOn(here.layer, nx, ny))
					continue;
				if (direction >= 4 && (!openOn(here.layer, x + dx, y) || !openOn(here.layer, x, y + dy)))
					continue;
				relax(atCost, entry.cell, static_cast<std::size_t>(index(here.layer, nx, ny)), direction >= 4 ? 14u : 10u, nx, ny, here.layer);
			}
			if (deck.toGround[deck.Index(x, y)] != 0 && open(x, y))
				relax(atCost, entry.cell, static_cast<std::size_t>(index(GroundLayer, x, y)), 0u, x, y, GroundLayer);
			continue;
		}
		if (!ignoring && !unitTunnel && x > 0 && y > 0 && x < width - 1 && y < height - 1 && interiorOpen(at))
		{
			// Not tunnelling (its own cell is open): each open neighbour, a diagonal only past two open straight ones.
			std::array<bool, 8> neighbourOpen;
			for (std::size_t direction = 0; direction < 8; ++direction)
				neighbourOpen[direction] = interiorOpen(static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at) + stepOffsets[direction]));
			for (std::size_t direction = 0; direction < 4; ++direction)
				if (neighbourOpen[direction])
					if (const auto unit = unitStep(x + steps[direction][0], y + steps[direction][1], false))
						relax(atCost, entry.cell, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at) + stepOffsets[direction]), 10u, x + steps[direction][0],
							y + steps[direction][1], GroundLayer, *unit);
			for (std::size_t direction = 4; direction < 8; ++direction)
			{
				const auto &[dx, dy] = steps[direction];
				if (neighbourOpen[direction] & neighbourOpen[dx > 0 ? 0 : 2] & neighbourOpen[dy > 0 ? 1 : 3])
					if (const auto unit = unitStep(x + dx, y + dy, false))
						relax(atCost, entry.cell, static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at) + stepOffsets[direction]), 14u, x + dx, y + dy, GroundLayer, *unit);
			}
			if (decks != 0)
				if (const std::uint8_t layer = grid.DeckLink(x, y); layer != GroundLayer && layer <= decks && openOn(layer, x, y))
					relax(atCost, entry.cell, static_cast<std::size_t>(index(layer, x, y)), 0u, x, y, layer);
			continue;
		}
		// Tunnelling: from a blocked cell (the start, or one tunnelled into), on through a structure's cells and the
		// usable ones too tight for it beside them, until it reaches room enough.
		const bool tunnelling = !groundOpen(x, y);
		const auto structure = [&](std::int32_t cx, std::int32_t cy) {
			return cx >= 0 && cy >= 0 && cx < width && cy < height && (grid.Type(cx, cy) == PathfindCellType::Obstacle || room[grid.Index(cx, cy)] > 0);
		};
		// The eight neighbours' openness, each looked up once (a diagonal's corner test reuses its two straight ones).
		std::array<bool, 8> neighbourOpen;
		for (std::size_t direction = 0; direction < steps.size(); ++direction)
			neighbourOpen[direction] = groundOpen(x + steps[direction][0], y + steps[direction][1]);
		for (std::size_t direction = 0; direction < steps.size(); ++direction)
		{
			const auto &[dx, dy] = steps[direction];
			const std::int32_t nx = x + dx, ny = y + dy;
			if (!neighbourOpen[direction] && !(tunnelling && structure(nx, ny)))
				continue;
			// Beside a diagonal: (x + dx, y) is straight neighbour 0 or 2, (x, y + dy) is 1 or 3.
			if (!tunnelling && direction >= 4 && (!neighbourOpen[dx > 0 ? 0 : 2] || !neighbourOpen[dy > 0 ? 1 : 3]))
				continue;
			if (const auto unit = unitStep(nx, ny, tunnelling))
				relax(atCost, entry.cell, static_cast<std::size_t>(index(GroundLayer, nx, ny)), direction >= 4 ? 14u : 10u, nx, ny, GroundLayer, *unit);
		}
		// Up on to a deck at the ground cell an entry links to.
		if (decks != 0)
			if (const std::uint8_t layer = grid.DeckLink(x, y); layer != GroundLayer && layer <= decks && openOn(layer, x, y))
				relax(atCost, entry.cell, static_cast<std::size_t>(index(layer, x, y)), 0u, x, y, layer);
	}
	if (!towardGoal && !found)
		return route;
	const std::int32_t end = found ? accepted : best;
	for (std::int32_t cell = end; cell != -1; cell = nodes[static_cast<std::size_t>(cell)].parent)
	{
		scratch.cells.push_back(cell);
		route.blockedByAlly = route.blockedByAlly || nodes[static_cast<std::size_t>(cell)].allyBlocked != 0;
	}
	std::reverse(scratch.cells.begin(), scratch.cells.end());
	route.reachedGoal = found;
	// Smoothing: from each kept cell, on to the farthest later cell in sight on the same layer (a change of layer is kept
	// as a point on each).
	route.points.push_back(from);
	route.layers.push_back(fromLayer);
	std::size_t anchor = 0;
	while (anchor + 1 < scratch.cells.size())
	{
		const Place a = decode(scratch.cells[anchor]);
		std::size_t runEnd = anchor;
		while (runEnd + 1 < scratch.cells.size() && decode(scratch.cells[runEnd + 1]).layer == a.layer)
			++runEnd;
		std::size_t next = anchor + 1;
		for (std::size_t candidate = runEnd; candidate > anchor + 1; --candidate)
		{
			const Place c = decode(scratch.cells[candidate]);
			// The anchor itself may be blocked (a mover walking out): judge the sight from the next cell on.
			// Pathfinder::isLinePassable (linePassableCallback): a unit standing fixed across the line (an ally or not) breaks it.
			const auto passable = [&](std::int32_t sx, std::int32_t sy) {
				if (sx == a.x && sy == a.y)
					return true;
				if (!openOn(a.layer, sx, sy))
					return false;
				if (units == nullptr || a.layer != GroundLayer)
					return true;
				const MovementCheck check = CheckForMovement(*units, sx, sy, mover.radius);
				return check.allyFixedCount == 0 && !check.enemyFixed;
			};
			if (InSight(a.x, a.y, c.x, c.y, passable))
			{
				next = candidate;
				break;
			}
		}
		const Place n = decode(scratch.cells[next]);
		route.points.push_back(Centre(n.x, n.y));
		route.layers.push_back(n.layer);
		anchor = next;
	}
	// The goal itself reached: the route ends on it (an accepted cell: on that cell's centre).
	if (found && towardGoal && accepted == goal)
	{
		if (route.points.size() == 1)
		{
			route.points.push_back(to);
			route.layers.push_back(toLayer);
		}
		else
			route.points.back() = to;
	}
	return route;
}

inline FoundRoute FindRoute(const NavigationGrid &grid, const ClearancePlane &plane, RouteMover mover, Engine::Math::FixedVector2 from,
	Engine::Math::FixedVector2 to, RouteScratch &scratch, std::uint8_t fromLayer = GroundLayer, std::uint8_t toLayer = GroundLayer,
	const RouteUnits *units = nullptr)
{
	return SearchRoute(grid, plane, mover, from, to, scratch, fromLayer, toLayer, units, true, [](std::int32_t, std::int32_t) { return false; });
}

// The layer a destination is on (the original's getLayerForDestination, for a point with no height of its own): the
// mover's own deck if it stays on it there, else the ground if it can stand there, else a deck over it (the lowest
// layer), else the ground.
inline std::uint8_t RouteGoalLayer(const NavigationGrid &grid, const ClearancePlane &plane, std::uint8_t radius, std::uint8_t fromLayer,
	Engine::Math::FixedVector2 to)
{
	if (grid.Decks().empty())
		return GroundLayer;
	const std::int32_t x = route_detail::CellOf(to.x), y = route_detail::CellOf(to.y);
	if (fromLayer != GroundLayer && DeckPassable(grid, plane, fromLayer, x, y, 0))
		return fromLayer;
	if (Passable(grid, plane, x, y, radius))
		return GroundLayer;
	// The wall is no destination from off it (a point on the ground is nearer the ground than the wall: more than half
	// the wall's height from it is what getLayerForDestination asks).
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		if (!grid.Decks()[layer - 1].wall && DeckPassable(grid, plane, layer, x, y, 0))
			return layer;
	return GroundLayer;
}
}
