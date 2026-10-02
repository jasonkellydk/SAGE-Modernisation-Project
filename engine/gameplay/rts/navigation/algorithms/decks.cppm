export module engine.gameplay.rts.navigation.algorithms.decks;
import std;

export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.resources.deck_surfaces;

// Decks: walkable surfaces above the ground (the original's pathfinding layers, one per bridge: Pathfinder::addBridge,
// PathfindLayer::classifyCells / classifyLayerMapCell, with Bridge's isPointOnBridge, getBridgeHeight, isCellOnEnd,
// isCellOnSide and isCellEntryPoint). A deck's cells: those its surface covers corner to corner are clear, those it
// covers in part are off it (bridge impassable), those its sides pass through are bridge impassable, those its ends
// pass through clear, and those just past its ends are entry cells, clear and linked to the ground cell under them
// (which links back). A ground cell under a deck less than a cell's height (10) below it, not an entry and no
// obstacle's, is bridge impassable. A destroyed deck is bridge impassable throughout and linked to nothing.
export namespace engine::gameplay
{
namespace deck_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

inline Fixed CellSize() { return Fixed::FromInt(PathfindCellSize); }

// LAYER_Z_CLOSE_ENOUGH_F.
inline Fixed CloseEnough() { return Fixed::FromInt(10); }

inline FixedVector2 Normalized(FixedVector2 v)
{
	const Fixed length = Engine::Math::Length(v);
	return length > Fixed{} ? FixedVector2{v.x / length, v.y / length} : FixedVector2{};
}

struct Region
{
	FixedVector2 lo, hi;
};

// LineInRegion: the segment clipped to the region (Cohen-Sutherland) lies in it.
inline bool LineInRegion(FixedVector2 p1, FixedVector2 p2, const Region &clip)
{
	enum : int
	{
		Left = 1,
		Right = 2,
		Bottom = 4,
		Top = 8,
	};
	Fixed x1 = p1.x, y1 = p1.y, x2 = p2.x, y2 = p2.y;
	const auto code = [&](Fixed x, Fixed y) {
		int bits = 0;
		if (x < clip.lo.x)
			bits = Left;
		else if (x > clip.hi.x)
			bits = Right;
		if (y < clip.lo.y)
			bits |= Top;
		else if (y > clip.hi.y)
			bits |= Bottom;
		return bits;
	};
	const int code1 = code(x1, y1), code2 = code(x2, y2);
	if ((code1 | code2) == 0)
		return true;
	if ((code1 & code2) != 0)
		return false;
	Fixed diff;
	if (code1 != 0)
	{
		if ((code1 & Top) != 0)
		{
			if ((diff = y2 - y1) == Fixed{})
				return false;
			x1 += (x2 - x1) * (clip.lo.y - y1) / diff;
			y1 = clip.lo.y;
		}
		else if ((code1 & Bottom) != 0)
		{
			if ((diff = y2 - y1) == Fixed{})
				return false;
			x1 += (x2 - x1) * (clip.hi.y - y1) / diff;
			y1 = clip.hi.y;
		}
		if (x1 > clip.hi.x)
		{
			if ((diff = x2 - x1) == Fixed{})
				return false;
			y1 += (y2 - y1) * (clip.hi.x - x1) / diff;
			x1 = clip.hi.x;
		}
		else if (x1 < clip.lo.x)
		{
			if ((diff = x2 - x1) == Fixed{})
				return false;
			y1 += (y2 - y1) * (clip.lo.x - x1) / diff;
			x1 = clip.lo.x;
		}
	}
	if (code2 != 0)
	{
		if ((code2 & Top) != 0)
		{
			if ((diff = y2 - y1) == Fixed{})
				return false;
			x2 += (x2 - x1) * (clip.lo.y - y2) / diff;
			y2 = clip.lo.y;
		}
		else if ((code2 & Bottom) != 0)
		{
			if ((diff = y2 - y1) == Fixed{})
				return false;
			x2 += (x2 - x1) * (clip.hi.y - y2) / diff;
			y2 = clip.hi.y;
		}
		if (x2 > clip.hi.x)
		{
			if ((diff = x2 - x1) == Fixed{})
				return false;
			y2 += (y2 - y1) * (clip.hi.x - x2) / diff;
			x2 = clip.hi.x;
		}
		else if (x2 < clip.lo.x)
		{
			if ((diff = x2 - x1) == Fixed{})
				return false;
			y2 += (y2 - y1) * (clip.lo.x - x2) / diff;
			x2 = clip.lo.x;
		}
	}
	return x1 >= clip.lo.x && x1 <= clip.hi.x && y1 >= clip.lo.y && y1 <= clip.hi.y && x2 >= clip.lo.x && x2 <= clip.hi.x && y2 >= clip.lo.y &&
		y2 <= clip.hi.y;
}

// The unit vector across the deck (fromLeft to fromRight) and along it (from to to).
inline FixedVector2 Across(const DeckGeometry &deck) { return Normalized(deck.fromRight.XY() - deck.fromLeft.XY()); }
inline FixedVector2 Along(const DeckGeometry &deck) { return Normalized(deck.to.XY() - deck.from.XY()); }

// Bridge::isCellOnEnd: its ends, pulled a cell in from each side, cross the cell.
inline bool OnEnd(const DeckGeometry &deck, const Region &cell)
{
	const FixedVector2 end = Across(deck) * CellSize();
	return LineInRegion(deck.fromLeft.XY() + end, deck.fromRight.XY() - end, cell) || LineInRegion(deck.toLeft.XY() + end, deck.toRight.XY() - end, cell);
}

// Bridge::isCellOnSide: its sides, pushed out half a cell (and then a whole cell), cross the cell.
inline bool OnSide(const DeckGeometry &deck, const Region &cell)
{
	const FixedVector2 end = Across(deck) * (CellSize() * Fixed::FromRatio(51, 100));
	FixedVector2 fromLeft = deck.fromLeft.XY() - end, fromRight = deck.fromRight.XY() + end, toLeft = deck.toLeft.XY() - end, toRight = deck.toRight.XY() + end;
	if (LineInRegion(fromLeft, toLeft, cell) || LineInRegion(fromRight, toRight, cell))
		return true;
	fromLeft = fromLeft - end;
	fromRight = fromRight + end;
	toLeft = toLeft - end;
	toRight = toRight + end;
	return LineInRegion(fromLeft, toLeft, cell) || LineInRegion(fromRight, toRight, cell);
}

// Bridge::isCellEntryPoint: its ends, half a cell out past them and a cell in from each side, cross the cell.
inline bool EntryPoint(const DeckGeometry &deck, const Region &cell)
{
	const FixedVector2 end = Across(deck) * CellSize();
	const FixedVector2 along = Along(deck) * (CellSize() / Fixed::FromInt(2));
	return LineInRegion(deck.fromLeft.XY() - along + end, deck.fromRight.XY() - along - end, cell) ||
		LineInRegion(deck.toLeft.XY() + along + end, deck.toRight.XY() + along - end, cell);
}
}

// PathfindLayer::classifyCells: the deck's cells classified afresh (its links to the ground made or cut).
inline void ClassifyDeck(NavigationGrid &grid, const GroundHeight &ground, std::uint8_t layer)
{
	using namespace deck_detail;
	DeckLayer &deck = grid.Decks()[layer - 1];
	const Fixed cell = CellSize();
	for (std::int32_t y = deck.y0; y < deck.y0 + deck.height; ++y)
		for (std::int32_t x = deck.x0; x < deck.x0 + deck.width; ++x)
		{
			const std::size_t index = deck.Index(x, y);
			if (deck.toGround[index] != 0 && grid.Contains(x, y) && grid.DeckLink(x, y) == layer)
				grid.SetDeckLink(x, y, 0);
			deck.toGround[index] = 0;
			const FixedVector2 lo{Fixed::FromInt(x) * cell, Fixed::FromInt(y) * cell};
			const FixedVector2 hi{lo.x + cell, lo.y + cell};
			int corners = 0;
			for (const FixedVector2 corner : {lo, FixedVector2{lo.x, hi.y}, hi, FixedVector2{hi.x, lo.y}})
				corners += PointOnDeck(deck.geometry, corner) ? 1 : 0;
			PathfindCellType type = PathfindCellType::Impassable;
			if (corners == 4)
				type = PathfindCellType::Clear;
			else
			{
				if (corners != 0)
					type = PathfindCellType::BridgeImpassable;
				const Region bounds{lo, hi};
				if (OnSide(deck.geometry, bounds))
					type = PathfindCellType::BridgeImpassable;
				else
				{
					if (OnEnd(deck.geometry, bounds))
						type = PathfindCellType::Clear;
					if (EntryPoint(deck.geometry, bounds) && grid.Contains(x, y))
					{
						type = PathfindCellType::Clear;
						deck.toGround[index] = 1;
						grid.SetDeckLink(x, y, layer);
					}
				}
			}
			deck.type[index] = type;
			// Too little room under the deck: the ground there is bridge impassable (an obstacle's cell stays its).
			if (type != PathfindCellType::Impassable && deck.toGround[index] == 0 && grid.Contains(x, y))
			{
				const FixedVector2 centre{lo.x + cell / Fixed::FromInt(2), lo.y + cell / Fixed::FromInt(2)};
				if (ground.At(centre) + CloseEnough() > DeckHeight(deck.geometry, centre) && grid.Type(x, y) != PathfindCellType::Obstacle)
					grid.SetType(x, y, PathfindCellType::BridgeImpassable);
			}
		}
	if (!deck.destroyed)
		return;
	for (std::size_t index = 0; index < deck.type.size(); ++index)
	{
		if (deck.toGround[index] != 0)
		{
			const std::int32_t x = deck.x0 + static_cast<std::int32_t>(index) % deck.width, y = deck.y0 + static_cast<std::int32_t>(index) / deck.width;
			if (grid.Contains(x, y) && grid.DeckLink(x, y) == layer)
				grid.SetDeckLink(x, y, 0);
			deck.toGround[index] = 0;
		}
		deck.type[index] = PathfindCellType::BridgeImpassable;
	}
}

// Pathfinder::addBridge / PathfindLayer::init and allocateCells: a deck's layer, its cells its bounds (rounded out with
// no slack: the original's PATHFIND_CELL_SIZE/100 is an integer 0; padded by one), within the grid up to its last cell; none when the layers are all used (the original's bridges past
// the last layer stay on the ground) or it covers no cell.
inline std::optional<std::uint8_t> AddDeck(NavigationGrid &grid, const GroundHeight &ground, ecs::Entity owner, const DeckGeometry &geometry, bool destroyed = false)
{
	using namespace deck_detail;
	if (grid.Decks().size() >= MaxDecks)
		return std::nullopt;
	const FixedVector2 corners[] = {geometry.fromLeft.XY(), geometry.fromRight.XY(), geometry.toLeft.XY(), geometry.toRight.XY()};
	FixedVector2 lo = corners[0], hi = corners[0];
	for (const FixedVector2 &corner : corners)
	{
		lo = {std::min(lo.x, corner.x), std::min(lo.y, corner.y)};
		hi = {std::max(hi.x, corner.x), std::max(hi.y, corner.y)};
	}
	// PATHFIND_CELL_SIZE/100 is integer division in the original: no slack.
	std::int32_t x0 = static_cast<std::int32_t>((lo.x / CellSize()).Floor()) - 1;
	std::int32_t y0 = static_cast<std::int32_t>((lo.y / CellSize()).Floor()) - 1;
	std::int32_t maxX = static_cast<std::int32_t>((hi.x / CellSize()).Ceil()) + 1;
	std::int32_t maxY = static_cast<std::int32_t>((hi.y / CellSize()).Ceil()) + 1;
	// Within m_extent: from its first cell up to its last (m_extent.hi, not included).
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	maxX = std::min(maxX, grid.HiX());
	maxY = std::min(maxY, grid.HiY());
	if (maxX <= x0 || maxY <= y0)
		return std::nullopt;
	DeckLayer deck;
	deck.owner = owner;
	deck.geometry = geometry;
	deck.x0 = x0;
	deck.y0 = y0;
	deck.width = maxX - x0;
	deck.height = maxY - y0;
	deck.type.assign(static_cast<std::size_t>(deck.width) * static_cast<std::size_t>(deck.height), PathfindCellType::Impassable);
	deck.toGround.assign(deck.type.size(), 0);
	deck.destroyed = destroyed;
	grid.Decks().push_back(std::move(deck));
	const auto layer = static_cast<std::uint8_t>(grid.Decks().size());
	ClassifyDeck(grid, ground, layer);
	return layer;
}

// PathfindLayer::setDestroyed (Pathfinder::changeBridgeState): whether anything changed.
inline bool SetDeckDestroyed(NavigationGrid &grid, const GroundHeight &ground, std::uint8_t layer, bool destroyed)
{
	if (layer == GroundLayer || layer > grid.Decks().size() || grid.Decks()[layer - 1].destroyed == destroyed)
		return false;
	grid.Decks()[layer - 1].destroyed = destroyed;
	ClassifyDeck(grid, ground, layer);
	return true;
}


// PathfindLayer::classifyWallCells / classifyWallMapCell over the wall's own cells: a cell all four of whose corners lie
// on a standing wall piece is clear, one with some of them on one bridge impassable, others impassable (no cell of the
// wall); no cell links to the ground. Then tightened by a cell: a clear cell not on the wall's border with any cell of
// the 3 x 3 around it not clear is a cliff (only climbers walk the wall's edge). The wall's clear cells go to its surface
// (Pathfinder::isPointOnWall).
inline void ClassifyWall(NavigationGrid &grid, std::uint8_t layer, WallSurface &wall)
{
	using namespace deck_detail;
	DeckLayer &deck = grid.Decks()[layer - 1];
	const Fixed cell = CellSize();
	for (std::int32_t y = deck.y0; y < deck.y0 + deck.height; ++y)
		for (std::int32_t x = deck.x0; x < deck.x0 + deck.width; ++x)
		{
			const std::size_t index = deck.Index(x, y);
			deck.toGround[index] = 0;
			const FixedVector2 lo{Fixed::FromInt(x) * cell, Fixed::FromInt(y) * cell};
			const FixedVector2 hi{lo.x + cell, lo.y + cell};
			int corners = 0;
			for (const FixedVector2 corner : {lo, FixedVector2{lo.x, hi.y}, hi, FixedVector2{hi.x, lo.y}})
				corners += PointOnWallPieces(wall, corner) ? 1 : 0;
			deck.type[index] = corners == 4 ? PathfindCellType::Clear : corners != 0 ? PathfindCellType::BridgeImpassable : PathfindCellType::Impassable;
		}
	// Tighten up 1 cell.
	const auto at = [&](std::int32_t i, std::int32_t j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(deck.width) + static_cast<std::size_t>(i); };
	std::vector<std::uint8_t> pinched(deck.type.size(), 0);
	for (std::int32_t i = 1; i < deck.width - 1; ++i)
		for (std::int32_t j = 1; j < deck.height - 1; ++j)
			for (std::int32_t k = i - 1; k < i + 2; ++k)
				for (std::int32_t l = j - 1; l < j + 2; ++l)
					if (deck.type[at(k, l)] != PathfindCellType::Clear)
						pinched[at(i, j)] = 1;
	for (std::size_t index = 0; index < deck.type.size(); ++index)
		if (pinched[index] != 0 && deck.type[index] == PathfindCellType::Clear)
			deck.type[index] = PathfindCellType::Cliff;
	wall.x0 = deck.x0;
	wall.y0 = deck.y0;
	wall.columns = deck.width;
	wall.rows = deck.height;
	wall.clear.resize(deck.type.size());
	for (std::size_t index = 0; index < deck.type.size(); ++index)
		wall.clear[index] = deck.type[index] == PathfindCellType::Clear ? 1u : 0u;
	wall.layer = layer;
}

// The wall layer's deck, its cells from x0, y0 on, width by height, classified (`wall`'s layer then this one).
inline std::uint8_t PushWallLayer(NavigationGrid &grid, WallSurface &wall, std::int32_t x0, std::int32_t y0, std::int32_t width, std::int32_t height)
{
	DeckLayer deck;
	deck.x0 = x0;
	deck.y0 = y0;
	deck.width = std::max(width, 0);
	deck.height = std::max(height, 0);
	deck.type.assign(static_cast<std::size_t>(deck.width) * static_cast<std::size_t>(deck.height), PathfindCellType::Impassable);
	deck.toGround.assign(deck.type.size(), 0);
	deck.wall = true;
	grid.Decks().push_back(std::move(deck));
	const auto layer = static_cast<std::uint8_t>(grid.Decks().size());
	ClassifyWall(grid, layer, wall);
	return layer;
}

// Pathfinder::newMap with wall pieces (PathfindLayer::allocateCellsForWallLayer): the wall's layer, its cells the pieces'
// bounds (GeometryInfo::get2DBounds: a box's turned corners, `boxes` 1; a sphere or cylinder its major radius each way)
// rounded out (no slack: the original's PATHFIND_CELL_SIZE/100 is an integer 0), padded by one, within the grid (up to
// its last cell, not including it: m_extent.hi); none without pieces, or covering no cell. Classified at once.
inline std::optional<std::uint8_t> AddWallLayer(NavigationGrid &grid, WallSurface &wall, std::span<const std::uint8_t> boxes)
{
	using namespace deck_detail;
	if (wall.Pieces() == 0)
		return std::nullopt;
	FixedVector2 lo{}, hi{};
	for (std::size_t piece = 0; piece < wall.Pieces(); ++piece)
	{
		const FixedVector2 centre = wall.position[piece];
		FixedVector2 low, high;
		if (piece < boxes.size() && boxes[piece] != 0)
		{
			const Fixed c = Engine::Math::Cos(wall.facing[piece]), s = Engine::Math::Sin(wall.facing[piece]);
			const Fixed exc = wall.major[piece] * c, eyc = wall.minor[piece] * c, exs = wall.major[piece] * s, eys = wall.minor[piece] * s;
			const FixedVector2 corners[] = {{centre.x - exc - eys, centre.y + eyc - exs}, {centre.x + exc - eys, centre.y + eyc + exs},
				{centre.x + exc + eys, centre.y - eyc + exs}, {centre.x - exc + eys, centre.y - eyc - exs}};
			low = high = corners[0];
			for (const FixedVector2 &corner : corners)
			{
				low = {std::min(low.x, corner.x), std::min(low.y, corner.y)};
				high = {std::max(high.x, corner.x), std::max(high.y, corner.y)};
			}
		}
		else
		{
			low = {centre.x - wall.major[piece], centre.y - wall.major[piece]};
			high = {centre.x + wall.major[piece], centre.y + wall.major[piece]};
		}
		if (piece == 0)
		{
			lo = low;
			hi = high;
			continue;
		}
		lo = {std::min(lo.x, low.x), std::min(lo.y, low.y)};
		hi = {std::max(hi.x, high.x), std::max(hi.y, high.y)};
	}
	std::int32_t x0 = static_cast<std::int32_t>((lo.x / CellSize()).Floor()) - 1;
	std::int32_t y0 = static_cast<std::int32_t>((lo.y / CellSize()).Floor()) - 1;
	std::int32_t maxX = static_cast<std::int32_t>((hi.x / CellSize()).Ceil()) + 1;
	std::int32_t maxY = static_cast<std::int32_t>((hi.y / CellSize()).Ceil()) + 1;
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	maxX = std::min(maxX, grid.HiX());
	maxY = std::min(maxY, grid.HiY());
	if (maxX <= x0 || maxY <= y0)
		return std::nullopt;
	return PushWallLayer(grid, wall, x0, y0, maxX - x0, maxY - y0);
}
}
