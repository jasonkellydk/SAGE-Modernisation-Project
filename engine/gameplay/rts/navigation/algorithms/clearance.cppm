export module engine.gameplay.rts.navigation.algorithms.clearance;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;

// Clearance planes: per cell, the Chebyshev distance in cells to the nearest
// cell the plane's surfaces cannot use (or beyond the map's edge), capped at
// MaxClearance; unusable cells have none. A mover whose footprint reaches r
// cells from its cell needs room > r. Two sweeps (forward then backward, the
// 8-neighbour chamfer) over a window: the whole grid, or a changed region
// grown by the cap (nothing further can change).
// Decks: the ground a deck leaves too little room under (bridge impassable)
// takes no room from its neighbours (a mover's footprint may reach under the
// deck's ends as it goes on to it): it keeps a distance as if usable, and is
// passable to none (Passable). A deck's own room is the same within its
// cells: its sides (bridge impassable) take room, its cells off it (past its
// ends) and beyond its bounds take none, and only its clear cells are
// passable.
export namespace engine::gameplay
{
inline void BuildClearance(const NavigationGrid &grid, ClearancePlane &plane, std::int32_t loX, std::int32_t loY, std::int32_t hiX, std::int32_t hiY)
{
	const std::int32_t width = grid.Width(), height = grid.Height();
	if (plane.room.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
	{
		plane.room.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
		loX = loY = 0;
		hiX = width - 1;
		hiY = height - 1;
	}
	loX = std::max(loX - MaxClearance, 0);
	loY = std::max(loY - MaxClearance, 0);
	hiX = std::min(hiX + MaxClearance, width - 1);
	hiY = std::min(hiY + MaxClearance, height - 1);
	// Off the grid is no room; cells outside the window keep their values (nothing that changed reaches them).
	const auto room = [&](std::int32_t x, std::int32_t y) -> std::int32_t { return grid.Contains(x, y) ? plane.room[grid.Index(x, y)] : 0; };
	for (std::int32_t y = loY; y <= hiY; ++y)
		for (std::int32_t x = loX; x <= hiX; ++x)
			plane.room[grid.Index(x, y)] =
				grid.Usable(x, y, plane.surfaces) || grid.Type(x, y) == PathfindCellType::BridgeImpassable ? MaxClearance : 0;
	const auto relax = [&](std::int32_t x, std::int32_t y, const std::array<std::array<std::int32_t, 2>, 4> &from) {
		std::uint8_t &value = plane.room[grid.Index(x, y)];
		for (const auto &[dx, dy] : from)
			if (value != 0)
				value = static_cast<std::uint8_t>(std::min<std::int32_t>(value, room(x + dx, y + dy) + 1));
	};
	for (std::int32_t y = loY; y <= hiY; ++y)
		for (std::int32_t x = loX; x <= hiX; ++x)
			relax(x, y, {{{-1, 0}, {-1, -1}, {0, -1}, {1, -1}}});
	for (std::int32_t y = hiY; y >= loY; --y)
		for (std::int32_t x = hiX; x >= loX; --x)
			relax(x, y, {{{1, 0}, {1, 1}, {0, 1}, {-1, 1}}});
	plane.zonesStale = true;
}

// Whether a deck cell of this type is one movers of these surfaces walk (Pathfinder::validLocomotorSurfacesForCellType,
// flying aside): a clear cell for ground movers, a cliff cell (the wall's edge) for cliff climbers.
inline bool DeckCellUsable(PathfindCellType type, std::uint8_t surfaces) noexcept
{
	return (SurfacesFor(type) & surfaces & ~locomotor_surface::Air) != 0;
}

// A deck's room over a plane (PathfindLayer's cells for this plane's surfaces): as the ground's, within its cells.
inline void BuildDeckClearance(const NavigationGrid &grid, ClearancePlane &plane, std::uint8_t layer)
{
	const DeckLayer &deck = grid.Decks()[layer - 1];
	if (plane.deckRoom.size() < grid.Decks().size())
		plane.deckRoom.resize(grid.Decks().size());
	std::vector<std::uint8_t> &room = plane.deckRoom[layer - 1];
	room.assign(deck.type.size(), 0);
	for (std::size_t index = 0; index < deck.type.size(); ++index)
	{
		const PathfindCellType type = deck.type[index];
		room[index] = DeckCellUsable(type, plane.surfaces) || type == PathfindCellType::Impassable ? MaxClearance : 0;
	}
	// Beyond its bounds takes no room.
	const auto at = [&](std::int32_t x, std::int32_t y) -> std::int32_t { return deck.Contains(x, y) ? room[deck.Index(x, y)] : MaxClearance; };
	const auto relax = [&](std::int32_t x, std::int32_t y, const std::array<std::array<std::int32_t, 2>, 4> &from) {
		std::uint8_t &value = room[deck.Index(x, y)];
		for (const auto &[dx, dy] : from)
			if (value != 0)
				value = static_cast<std::uint8_t>(std::min<std::int32_t>(value, at(x + dx, y + dy) + 1));
	};
	for (std::int32_t y = deck.y0; y < deck.y0 + deck.height; ++y)
		for (std::int32_t x = deck.x0; x < deck.x0 + deck.width; ++x)
			relax(x, y, {{{-1, 0}, {-1, -1}, {0, -1}, {1, -1}}});
	for (std::int32_t y = deck.y0 + deck.height - 1; y >= deck.y0; --y)
		for (std::int32_t x = deck.x0 + deck.width - 1; x >= deck.x0; --x)
			relax(x, y, {{{1, 0}, {1, 1}, {0, 1}, {-1, 1}}});
	plane.zonesStale = true;
}

// A mover whose footprint reaches `radius` cells may stand on the cell: its room exceeds the radius, and it is no
// bridge-impassable ground (whose room only spares its neighbours); on a deck, a clear cell of it.
inline bool Passable(const NavigationGrid &grid, const ClearancePlane &plane, std::int32_t x, std::int32_t y, std::uint8_t radius)
{
	return grid.Contains(x, y) && plane.room[grid.Index(x, y)] > radius && grid.Type(x, y) != PathfindCellType::BridgeImpassable;
}

inline bool DeckPassable(const NavigationGrid &grid, const ClearancePlane &plane, std::uint8_t layer, std::int32_t x, std::int32_t y, std::uint8_t radius)
{
	if (layer == GroundLayer || layer > grid.Decks().size() || layer > plane.deckRoom.size())
		return false;
	const DeckLayer &deck = grid.Decks()[layer - 1];
	if (!deck.Contains(x, y))
		return false;
	const std::size_t index = deck.Index(x, y);
	return DeckCellUsable(deck.type[index], plane.surfaces) && plane.deckRoom[layer - 1].size() == deck.type.size() && plane.deckRoom[layer - 1][index] > radius;
}

namespace clearance_detail
{
// The ground's zones from a table of its usable cells (row-major, `zones` already all 0): each row's usable cells form
// runs; a run joins the runs of the row above it overlaps (four ways: sharing a column), by union-find. The flood fill
// numbers each zone when its first cell in index order is met, so numbering the components in the order their first
// run comes (rows top to bottom, runs left to right) gives the same numbers. Returns how many zones it numbered.
inline std::uint32_t LabelRuns(const std::vector<std::uint8_t> &open, std::size_t width, std::size_t height, std::vector<std::uint32_t> &zones)
{
	struct Run
	{
		std::uint32_t start, end; // cell index range in its row, end exclusive
		std::uint32_t parent;
	};
	std::vector<Run> runs;
	const auto find = [&](std::uint32_t run) {
		while (runs[run].parent != run)
		{
			runs[run].parent = runs[runs[run].parent].parent;
			run = runs[run].parent;
		}
		return run;
	};
	std::size_t previousFirst = 0, previousEnd = 0; // the row above's runs
	for (std::size_t y = 0; y < height; ++y)
	{
		const std::size_t rowStart = y * width;
		const std::uint8_t *const row = open.data() + rowStart;
		const std::size_t first = runs.size();
		// Eight cells a step over whole stretches (all closed, or all open), cell by cell at a run's edges.
		const auto word = [&](std::size_t x) {
			std::uint64_t value;
			std::memcpy(&value, row + x, sizeof(value));
			return value;
		};
		constexpr std::uint64_t AllOpen = 0x0101010101010101ull;
		for (std::size_t x = 0; x < width;)
		{
			while (x + 8 <= width && word(x) == 0)
				x += 8;
			while (x < width && row[x] == 0)
				++x;
			if (x >= width)
				break;
			const std::size_t begin = x;
			while (x + 8 <= width && word(x) == AllOpen)
				x += 8;
			while (x < width && row[x] != 0)
				++x;
			const auto id = static_cast<std::uint32_t>(runs.size());
			runs.push_back({static_cast<std::uint32_t>(rowStart + begin), static_cast<std::uint32_t>(rowStart + x), id});
		}
		// Join the runs sharing a column with the row above (both lists run left to right).
		std::size_t above = previousFirst;
		for (std::size_t run = first; run < runs.size(); ++run)
		{
			const std::size_t begin = runs[run].start - rowStart, end = runs[run].end - rowStart;
			while (above < previousEnd && runs[above].end - (rowStart - width) <= begin)
				++above;
			for (std::size_t other = above; other < previousEnd && runs[other].start - (rowStart - width) < end; ++other)
			{
				const std::uint32_t a = find(static_cast<std::uint32_t>(run)), b = find(static_cast<std::uint32_t>(other));
				if (a != b)
					runs[std::max(a, b)].parent = std::min(a, b);
			}
		}
		previousFirst = first;
		previousEnd = runs.size();
	}
	std::vector<std::uint32_t> label(runs.size(), 0);
	std::uint32_t zone = 0;
	for (std::size_t run = 0; run < runs.size(); ++run)
	{
		std::uint32_t &number = label[find(static_cast<std::uint32_t>(run))];
		if (number == 0)
			number = ++zone;
		std::fill(zones.begin() + runs[run].start, zones.begin() + runs[run].end, number);
	}
	return zone;
}

// The zones of a grid with decks, exactly as FloodZones floods them, without a cell-by-cell flood of the ground: the
// ground's usable cells (`open`) are first labelled by runs (LabelRuns: components four ways within the ground, numbered
// by their first cell). A flood that reaches any cell of such a component fills all of it, so FloodZones' flood is
// the same over the components and the deck cells, following its links exactly: a ground cell leads to the deck its
// DeckLink names, where that deck's cell over it leads to the ground; a deck cell leads to its four neighbours on the
// deck and, where it leads to the ground, to the ground cell under it. FloodZones starts a zone at each cell not yet
// reached, in index order. A component's first cell comes before any of its other cells and every ground cell comes
// before every deck cell, so starting from the components in their order, then from the deck cells in index order,
// numbers the same zones the same.
template<typename DeckUsable>
void LinkDeckZones(const NavigationGrid &grid, const std::vector<std::uint8_t> &open, std::vector<std::uint32_t> &zones, DeckUsable &&deckUsable)
{
	const std::size_t width = static_cast<std::size_t>(grid.Width()), height = static_cast<std::size_t>(grid.Height());
	const std::size_t groundCells = width * height;
	zones.assign(grid.CellCount(), 0);
	// The ground's components first (their ids in the ground's part of `zones`, renumbered as zones at the end).
	const std::uint32_t components = LabelRuns(open, width, height, zones);
	std::vector<std::uint32_t> componentZone(static_cast<std::size_t>(components) + 1, 0);
	// The ground-to-deck links, by component: from each deck's cells leading to the ground, those whose ground cell's
	// DeckLink names that deck (a usable ground cell: in a component).
	std::vector<std::pair<std::uint32_t, std::size_t>> links; // (component, deck cell index)
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
	{
		const DeckLayer &deck = grid.Decks()[layer - 1];
		const std::size_t offset = grid.DeckOffset(layer);
		for (std::int32_t y = deck.y0; y < deck.y0 + deck.height; ++y)
			for (std::int32_t x = deck.x0; x < deck.x0 + deck.width; ++x)
			{
				const std::size_t local = deck.Index(x, y);
				if (deck.toGround[local] == 0 || !grid.Contains(x, y) || grid.DeckLink(x, y) != layer)
					continue;
				if (const std::uint32_t component = zones[grid.Index(x, y)]; component != 0)
					links.emplace_back(component, offset + local);
			}
	}
	std::sort(links.begin(), links.end());
	std::vector<std::size_t> offsets;
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		offsets.push_back(grid.DeckOffset(layer));
	// The flood's stack: a component (its id, with the top bit set) or a deck cell (its index).
	constexpr std::size_t ComponentBit = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1);
	std::vector<std::size_t> stack;
	const auto reachDeck = [&](std::size_t index, std::uint32_t zone) {
		if (index < zones.size() && zones[index] == 0 && deckUsable(index))
		{
			zones[index] = zone;
			stack.push_back(index);
		}
	};
	const auto reachGround = [&](std::size_t index, std::uint32_t zone) {
		const std::uint32_t component = zones[index];
		if (component != 0 && componentZone[component] == 0)
		{
			componentZone[component] = zone;
			stack.push_back(ComponentBit | component);
		}
	};
	const auto flood = [&](std::uint32_t zone) {
		while (!stack.empty())
		{
			const std::size_t node = stack.back();
			stack.pop_back();
			if ((node & ComponentBit) != 0)
			{
				const auto component = static_cast<std::uint32_t>(node & ~ComponentBit);
				for (auto link = std::lower_bound(links.begin(), links.end(), std::pair<std::uint32_t, std::size_t>{component, 0});
					 link != links.end() && link->first == component; ++link)
					reachDeck(link->second, zone);
				continue;
			}
			std::uint8_t layer = static_cast<std::uint8_t>(offsets.size());
			while (layer > 1 && node < offsets[layer - 1])
				--layer;
			const DeckLayer &deck = grid.Decks()[layer - 1];
			const std::size_t local = node - offsets[layer - 1];
			const std::int32_t x = deck.x0 + static_cast<std::int32_t>(local % static_cast<std::size_t>(deck.width));
			const std::int32_t y = deck.y0 + static_cast<std::int32_t>(local / static_cast<std::size_t>(deck.width));
			const std::int32_t around[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
			for (const auto &[nx, ny] : around)
				if (deck.Contains(nx, ny))
					reachDeck(offsets[layer - 1] + deck.Index(nx, ny), zone);
			if (deck.toGround[local] != 0 && grid.Contains(x, y))
				reachGround(grid.Index(x, y), zone);
		}
	};
	std::uint32_t next = 0;
	for (std::uint32_t component = 1; component <= components; ++component)
		if (componentZone[component] == 0)
		{
			const std::uint32_t zone = ++next;
			componentZone[component] = zone;
			stack.push_back(ComponentBit | component);
			flood(zone);
		}
	for (std::size_t index = groundCells; index < zones.size(); ++index)
		if (zones[index] == 0 && deckUsable(index))
		{
			const std::uint32_t zone = ++next;
			zones[index] = zone;
			stack.push_back(index);
			flood(zone);
		}
	for (std::size_t index = 0; index < groundCells; ++index)
		zones[index] = componentZone[zones[index]];
}

// Every cell the routes know (the ground's, then each deck's: NavigationGrid::CellCount) flood-filled where `usable`,
// four ways within a layer and across a deck's links to the ground, numbered from 1 in index order (0: not usable).
template<typename Usable>
void FloodZones(const NavigationGrid &grid, std::vector<std::uint32_t> &zones, Usable &&usable)
{
	const std::int32_t width = grid.Width(), height = grid.Height();
	const std::size_t groundCells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	zones.assign(grid.CellCount(), 0);
	// Without decks (the usual map): the same zones, labelled by runs over a table of the usable cells (LabelRuns).
	if (grid.Decks().empty())
	{
		std::vector<std::uint8_t> open(groundCells);
		for (std::size_t index = 0; index < groundCells; ++index)
			open[index] = usable(index) ? 1 : 0;
		LabelRuns(open, static_cast<std::size_t>(width), static_cast<std::size_t>(height), zones);
		return;
	}
	std::vector<std::size_t> offsets;
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		offsets.push_back(grid.DeckOffset(layer));
	std::vector<std::size_t> stack;
	std::uint32_t next = 0;
	const auto visit = [&](std::size_t index, std::uint32_t zone) {
		if (index < zones.size() && zones[index] == 0 && usable(index))
		{
			zones[index] = zone;
			stack.push_back(index);
		}
	};
	for (std::size_t start = 0; start < zones.size(); ++start)
	{
		if (zones[start] != 0 || !usable(start))
			continue;
		const std::uint32_t zone = ++next;
		zones[start] = zone;
		stack.push_back(start);
		while (!stack.empty())
		{
			const std::size_t cell = stack.back();
			stack.pop_back();
			if (cell < groundCells)
			{
				const std::int32_t x = static_cast<std::int32_t>(cell % static_cast<std::size_t>(width)), y = static_cast<std::int32_t>(cell / static_cast<std::size_t>(width));
				const std::int32_t around[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
				for (const auto &[nx, ny] : around)
					if (nx >= 0 && ny >= 0 && nx < width && ny < height)
						visit(static_cast<std::size_t>(ny) * static_cast<std::size_t>(width) + static_cast<std::size_t>(nx), zone);
				if (const std::uint8_t layer = grid.DeckLink(x, y); layer != GroundLayer && layer <= grid.Decks().size())
				{
					const DeckLayer &deck = grid.Decks()[layer - 1];
					if (deck.Contains(x, y) && deck.toGround[deck.Index(x, y)] != 0)
						visit(offsets[layer - 1] + deck.Index(x, y), zone);
				}
				continue;
			}
			std::uint8_t layer = static_cast<std::uint8_t>(offsets.size());
			while (layer > 1 && cell < offsets[layer - 1])
				--layer;
			const DeckLayer &deck = grid.Decks()[layer - 1];
			const std::size_t local = cell - offsets[layer - 1];
			const std::int32_t x = deck.x0 + static_cast<std::int32_t>(local % static_cast<std::size_t>(deck.width));
			const std::int32_t y = deck.y0 + static_cast<std::int32_t>(local / static_cast<std::size_t>(deck.width));
			const std::int32_t around[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
			for (const auto &[nx, ny] : around)
				if (deck.Contains(nx, ny))
					visit(offsets[layer - 1] + deck.Index(nx, ny), zone);
			if (deck.toGround[local] != 0 && grid.Contains(x, y))
				visit(grid.Index(x, y), zone);
		}
	}
}
}

// The plane's connected zones (PathfindZoneManager): its usable cells flood-filled four ways, numbered from 1 in grid
// order; and its terrain zones, the obstacles' cells counted usable (buildings are put on clear ground).
inline void BuildZones(const NavigationGrid &grid, ClearancePlane &plane)
{
	const std::int32_t width = grid.Width();
	const std::size_t groundCells = plane.room.size();
	std::vector<std::size_t> offsets;
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		offsets.push_back(grid.DeckOffset(layer));
	// A deck cell: usable when clear with room for this plane.
	const auto deckUsable = [&](std::size_t index) {
		std::uint8_t layer = static_cast<std::uint8_t>(offsets.size());
		while (layer > 1 && index < offsets[layer - 1])
			--layer;
		const std::size_t local = index - offsets[layer - 1];
		const DeckLayer &deck = grid.Decks()[layer - 1];
		return layer <= plane.deckRoom.size() && local < plane.deckRoom[layer - 1].size() && plane.deckRoom[layer - 1][local] != 0 &&
			DeckCellUsable(deck.type[local], plane.surfaces);
	};
	// Row-major, as Index: the ground's types in index order.
	const std::span<const PathfindCellType> types = grid.Types();
	const auto groundType = [&](std::size_t index) { return types[index]; };
	// Both zone maps' usable ground cells in one pass (the tests below, cell by cell, into buffers kept with the plane),
	// then labelled by runs; with decks, the decks joined on by LinkDeckZones (the same zones as FloodZones).
	if (!grid.Decks().empty() && groundCells == types.size() && groundCells == static_cast<std::size_t>(width) * static_cast<std::size_t>(grid.Height()))
	{
		std::vector<std::uint8_t> &open = plane.zoneOpen, &terrain = plane.terrainOpen;
		open.resize(groundCells);
		terrain.resize(groundCells);
		const std::uint8_t *const room = plane.room.data();
		for (std::size_t index = 0; index < groundCells; ++index)
		{
			const PathfindCellType type = types[index];
			const bool usable = room[index] != 0 && type != PathfindCellType::BridgeImpassable;
			open[index] = usable ? 1 : 0;
			terrain[index] = usable || type == PathfindCellType::Obstacle ? 1 : 0;
		}
		clearance_detail::LinkDeckZones(grid, open, plane.zones, deckUsable);
		clearance_detail::LinkDeckZones(grid, terrain, plane.terrainZones, deckUsable);
		plane.zonesStale = false;
		return;
	}
	if (grid.Decks().empty() && groundCells == types.size() && grid.CellCount() == groundCells)
	{
		std::vector<std::uint8_t> &open = plane.zoneOpen, &terrain = plane.terrainOpen;
		open.resize(groundCells);
		terrain.resize(groundCells);
		const std::uint8_t *const room = plane.room.data();
		for (std::size_t index = 0; index < groundCells; ++index)
		{
			const PathfindCellType type = types[index];
			const bool usable = room[index] != 0 && type != PathfindCellType::BridgeImpassable;
			open[index] = usable ? 1 : 0;
			terrain[index] = usable || type == PathfindCellType::Obstacle ? 1 : 0;
		}
		plane.zones.assign(groundCells, 0);
		plane.terrainZones.assign(groundCells, 0);
		clearance_detail::LabelRuns(open, static_cast<std::size_t>(width), static_cast<std::size_t>(grid.Height()), plane.zones);
		clearance_detail::LabelRuns(terrain, static_cast<std::size_t>(width), static_cast<std::size_t>(grid.Height()), plane.terrainZones);
		plane.zonesStale = false;
		return;
	}
	clearance_detail::FloodZones(grid, plane.zones, [&](std::size_t index) {
		if (index >= groundCells)
			return deckUsable(index);
		return plane.room[index] != 0 && groundType(index) != PathfindCellType::BridgeImpassable;
	});
	clearance_detail::FloodZones(grid, plane.terrainZones, [&](std::size_t index) {
		if (index >= groundCells)
			return deckUsable(index);
		const PathfindCellType type = groundType(index);
		return (plane.room[index] != 0 && type != PathfindCellType::BridgeImpassable) || type == PathfindCellType::Obstacle;
	});
	plane.zonesStale = false;
}

// Pathfinder::clientSafeQuickDoesPathExist: the destination's cell usable by these surfaces, and in the same zone as
// the start's (a start in another unusable cell: any usable neighbour's); a start inside an obstacle compares the terrain
// zones (clientSafeQuickDoesPathExist's doingTerrainZone). Zones rebuilt first if stale.
inline bool QuickPathExists(NavigationGrid &grid, std::uint8_t surfaces, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to)
{
	ClearancePlane *plane = nullptr;
	for (ClearancePlane &candidate : grid.Clearance())
		if (candidate.surfaces == surfaces)
			plane = &candidate;
	if (plane == nullptr || plane->room.empty())
		return false;
	if (plane->zonesStale || plane->zones.size() != grid.CellCount())
		BuildZones(grid, *plane);
	const auto cellOf = [](Engine::Math::Fixed value) { return static_cast<std::int32_t>((value / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor()); };
	const std::int32_t toX = cellOf(to.x), toY = cellOf(to.y), fromX = cellOf(from.x), fromY = cellOf(from.y);
	if (!grid.Contains(toX, toY) || !grid.Contains(fromX, fromY))
		return false;
	const std::uint32_t goal = plane->zones[grid.Index(toX, toY)];
	if (goal == 0)
		return false;
	// A start inside an obstacle (a unit a building was put over, a factory's own place): the terrain zones, the buildings
	// aside (doingTerrainZone); it tunnels out of the obstacle (FindRoute).
	if (grid.Type(fromX, fromY) == PathfindCellType::Obstacle)
		return plane->terrainZones[grid.Index(fromX, fromY)] == plane->terrainZones[grid.Index(toX, toY)];
	for (std::int32_t dy = -1; dy <= 1; ++dy)
		for (std::int32_t dx = -1; dx <= 1; ++dx)
		{
			if ((dx != 0 || dy != 0) && plane->zones[grid.Index(fromX, fromY)] != 0)
				continue;
			if (grid.Contains(fromX + dx, fromY + dy) && plane->zones[grid.Index(fromX + dx, fromY + dy)] == goal)
				return true;
		}
	return false;
}

inline void BuildClearance(const NavigationGrid &grid, ClearancePlane &plane)
{
	BuildClearance(grid, plane, 0, 0, grid.Width() - 1, grid.Height() - 1);
	plane.deckRoom.resize(grid.Decks().size());
	for (std::uint8_t layer = 1; layer <= grid.Decks().size(); ++layer)
		BuildDeckClearance(grid, plane, layer);
}
}
