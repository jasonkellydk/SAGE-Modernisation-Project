export module engine.gameplay.rts.navigation.algorithms.route_search;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.navigation.algorithms.clearance;

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
//   open), so routes run straight across open ground and turn at corners.
// Points are cell centres; the first is where the mover is, the last the
// goal itself when that was reached.
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
	std::vector<std::uint32_t> cost;
	std::vector<std::int32_t> parent;
	std::vector<std::uint32_t> seen;   // generation a cell was reached in
	std::vector<std::uint32_t> closed; // generation it was expanded in
	std::uint32_t generation{0};
	struct Entry
	{
		std::uint32_t f, h;
		std::int32_t cell;
		bool operator>(const Entry &other) const noexcept
		{
			return f != other.f ? f > other.f : h != other.h ? h > other.h : cell > other.cell;
		}
	};
	std::vector<Entry> heap;
	std::vector<std::int32_t> cells;

	void Prepare(std::size_t count)
	{
		if (cost.size() != count)
		{
			cost.assign(count, 0);
			parent.assign(count, -1);
			seen.assign(count, 0);
			closed.assign(count, 0);
			generation = 0;
		}
		if (++generation == 0)
		{
			std::fill(seen.begin(), seen.end(), 0u);
			std::fill(closed.begin(), closed.end(), 0u);
			generation = 1;
		}
		heap.clear();
		cells.clear();
	}
};

struct FoundRoute
{
	std::vector<Engine::Math::FixedVector2> points;
	std::vector<std::uint8_t> layers; // each point's layer
	bool reachedGoal{false};
	bool exhausted{false}; // the budget ran out first
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

inline FoundRoute FindRoute(const NavigationGrid &grid, const ClearancePlane &plane, RouteMover mover, Engine::Math::FixedVector2 from,
	Engine::Math::FixedVector2 to, RouteScratch &scratch, std::uint8_t fromLayer = GroundLayer, std::uint8_t toLayer = GroundLayer)
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
	if (!openOn(toLayer, goalX, goalY))
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
	const std::int32_t start = index(fromLayer, startX, startY), goal = index(toLayer, goalX, goalY);
	scratch.cost[static_cast<std::size_t>(start)] = 0;
	scratch.parent[static_cast<std::size_t>(start)] = -1;
	scratch.seen[static_cast<std::size_t>(start)] = generation;
	const std::uint32_t startH = Octile(startX, startY, goalX, goalY);
	scratch.heap.push_back({startH, startH, start});
	std::int32_t best = start;
	std::uint32_t bestH = startH, bestCost = 0;
	bool found = false;
	std::uint32_t expanded = 0;
	static constexpr std::array<std::array<std::int32_t, 2>, 8> steps{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
	const auto later = std::greater<RouteScratch::Entry>{};
	const auto relax = [&](std::size_t at, std::int32_t from, std::size_t next, std::uint32_t step, std::int32_t nx, std::int32_t ny) {
		if (scratch.closed[next] == generation)
			return;
		const std::uint32_t candidate = scratch.cost[at] + step;
		if (scratch.seen[next] == generation && candidate >= scratch.cost[next])
			return;
		scratch.seen[next] = generation;
		scratch.cost[next] = candidate;
		scratch.parent[next] = from;
		const std::uint32_t h = Octile(nx, ny, goalX, goalY);
		scratch.heap.push_back({candidate + h, h, static_cast<std::int32_t>(next)});
		std::push_heap(scratch.heap.begin(), scratch.heap.end(), later);
	};
	while (!scratch.heap.empty())
	{
		std::pop_heap(scratch.heap.begin(), scratch.heap.end(), later);
		const RouteScratch::Entry entry = scratch.heap.back();
		scratch.heap.pop_back();
		const auto at = static_cast<std::size_t>(entry.cell);
		if (scratch.closed[at] == generation)
			continue;
		scratch.closed[at] = generation;
		if (++expanded > mover.budget)
		{
			route.exhausted = true;
			break;
		}
		const Place here = decode(entry.cell);
		const std::int32_t x = here.x, y = here.y;
		if (entry.h < bestH || (entry.h == bestH && scratch.cost[at] < bestCost))
		{
			best = entry.cell;
			bestH = entry.h;
			bestCost = scratch.cost[at];
		}
		if (entry.cell == goal)
		{
			found = true;
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
				relax(at, entry.cell, static_cast<std::size_t>(index(here.layer, nx, ny)), direction >= 4 ? 14u : 10u, nx, ny);
			}
			if (deck.toGround[deck.Index(x, y)] != 0 && open(x, y))
				relax(at, entry.cell, static_cast<std::size_t>(index(GroundLayer, x, y)), 0u, x, y);
			continue;
		}
		for (std::size_t direction = 0; direction < steps.size(); ++direction)
		{
			const auto &[dx, dy] = steps[direction];
			const std::int32_t nx = x + dx, ny = y + dy;
			// Tunnelling: from a blocked cell (the start, or one tunnelled into), on through a structure's cells and the
			// usable ones too tight for it beside them, until it reaches room enough.
			const bool tunnelling = !open(x, y);
			const auto structure = [&](std::int32_t cx, std::int32_t cy) {
				return cx >= 0 && cy >= 0 && cx < width && cy < height &&
					(grid.Type(cx, cy) == PathfindCellType::Obstacle || plane.room[grid.Index(cx, cy)] > 0);
			};
			if (!open(nx, ny) && !(tunnelling && structure(nx, ny)))
				continue;
			if (!tunnelling && direction >= 4 && (!open(x + dx, y) || !open(x, y + dy)))
				continue;
			relax(at, entry.cell, static_cast<std::size_t>(index(GroundLayer, nx, ny)), direction >= 4 ? 14u : 10u, nx, ny);
		}
		// Up on to a deck at the ground cell an entry links to.
		if (decks != 0)
			if (const std::uint8_t layer = grid.DeckLink(x, y); layer != GroundLayer && layer <= decks && openOn(layer, x, y))
				relax(at, entry.cell, static_cast<std::size_t>(index(layer, x, y)), 0u, x, y);
	}
	const std::int32_t end = found ? goal : best;
	for (std::int32_t cell = end; cell != -1; cell = scratch.parent[static_cast<std::size_t>(cell)])
		scratch.cells.push_back(cell);
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
			if (InSight(a.x, a.y, c.x, c.y, [&](std::int32_t sx, std::int32_t sy) { return (sx == a.x && sy == a.y) || openOn(a.layer, sx, sy); }))
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
	if (found)
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
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		if (DeckPassable(grid, plane, layer, x, y, 0))
			return layer;
	return GroundLayer;
}
}
