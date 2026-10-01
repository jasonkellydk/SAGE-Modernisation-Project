export module engine.gameplay.rts.vision.algorithms.footprint_cells;
import std;

export import engine.gameplay.rts.vision.components.partition_footprint;
export import engine.gameplay.rts.vision.resources.shroud_map;
export import Engine.Core.Math.FixedVector;

// The partition cells an object's geometry touches (PartitionData::updateCellsTouched), in the order the original
// records them, each once, at most as many as it allocates (calcMaxCoiForShape: retail sizes a circle's as a box's, so
// by its radius and its minor radius; never under 4): a small geometry's cells under its bounding square (radius at most
// half a cell: doSmallFill), a circle's midpoint-circle scanlines (doCircleFill, retail's loop bound), a box sampled on a
// half-cell grid along its axes (doRectFill). Cells off the map are none. The box's samples use fixed-point trig where
// the original used floats: a sample landing within rounding of a cell edge may fall in the neighbour.
export namespace engine::gameplay
{
namespace footprint_cells_detail
{
using Engine::Math::Fixed;

inline std::int32_t CellsFor(const ShroudMap &map, Fixed distance) noexcept
{
	return static_cast<std::int32_t>((distance / map.CellSize()).Ceil());
}

// calcMaxCoiForShape (RETAIL_COMPATIBLE_CRC: spheres and cylinders fall through to the box case).
inline std::size_t Allowance(const ShroudMap &map, const Footprint &shape)
{
	const Fixed diagonal = Engine::Math::Sqrt(shape.major * shape.major + shape.minor * shape.minor);
	const std::int32_t cells = CellsFor(map, diagonal * Fixed::FromInt(2)) + 1;
	return static_cast<std::size_t>(std::max(cells * cells, 4));
}
}

// doRectFill's cells (as VisitFootprintCells takes them, below) when its samples' cells fit a box of at most 64 cells:
// the samples lie on a lattice (each an exact sum of fixed steps), so its corner samples bound every sample's cell
// (CellOf is monotone). The cells taken are marked in a 64-bit map of that box, not a hash set. The allowance (a square
// root) is needed only when the box holds more cells than a bound under it (4, or with max(major, minor) less one unit
// for the diagonal: the rounded root of the rounded squares is no less). False (nothing taken) when the box is larger
// or the lattice's sums could saturate.
template<typename Take>
inline bool VisitRectCellsInBox(const ShroudMap &map, const Footprint &shape, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing, Take &take)
{
	using Engine::Math::Fixed;
	using namespace footprint_cells_detail;
	const Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	const Fixed step = map.CellSize() / Fixed::FromInt(2);
	const Fixed ydx = s * step, ydy = -c * step, xdx = c * step, xdy = s * step;
	const std::int64_t stepsX = (shape.major * Fixed::FromInt(2) / step).Ceil();
	const std::int64_t stepsY = (shape.minor * Fixed::FromInt(2) / step).Ceil();
	if (stepsX <= 0 || stepsY <= 0)
		return true; // no samples: no cells
	const Fixed rowX = at.x - shape.major * c - shape.minor * s;
	const Fixed rowY = at.y + shape.minor * c - shape.major * s;
	constexpr std::int64_t safe = std::int64_t{1} << 40;
	const auto small = [&](Fixed value) { return value.Raw() > -safe && value.Raw() < safe; };
	if (stepsX > 64 || stepsY > 64 || !small(rowX) || !small(rowY) || !small(step))
		return false;
	// The lattice's extreme samples, as the loop's sums reach them (no sum can saturate at these magnitudes).
	const Fixed lastX = Fixed::FromRaw(xdx.Raw() * (stepsX - 1)), lastXy = Fixed::FromRaw(xdy.Raw() * (stepsX - 1));
	const Fixed lastYx = Fixed::FromRaw(ydx.Raw() * (stepsY - 1)), lastY = Fixed::FromRaw(ydy.Raw() * (stepsY - 1));
	std::int32_t lowX = std::numeric_limits<std::int32_t>::max(), lowY = lowX, highX = std::numeric_limits<std::int32_t>::min(), highY = highX;
	for (const bool alongX : {false, true})
		for (const bool alongY : {false, true})
		{
			const Fixed x = rowX + (alongY ? lastYx : Fixed{}) + (alongX ? lastX : Fixed{});
			const Fixed y = rowY + (alongY ? lastY : Fixed{}) + (alongX ? lastXy : Fixed{});
			const auto cell = map.CellOf(x, y);
			lowX = std::min(lowX, cell[0]);
			highX = std::max(highX, cell[0]);
			lowY = std::min(lowY, cell[1]);
			highY = std::max(highY, cell[1]);
		}
	const std::int64_t boxWidth = std::int64_t{highX} - lowX + 1, boxCells = boxWidth * (std::int64_t{highY} - lowY + 1);
	if (boxCells > 64)
		return false;
	// (For a side of a unit or more: the rounded square loses under half a unit, the root then under one.)
	const Fixed longest = std::max(shape.major, shape.minor);
	const std::int32_t side = CellsFor(map, (longest - Fixed::Epsilon()) * Fixed::FromInt(2)) + 1;
	const bool bounded = boxCells <= 4 || (longest >= Fixed::One() && boxCells <= std::int64_t{side} * side);
	const std::size_t allowance = bounded ? static_cast<std::size_t>(boxCells) : Allowance(map, shape);
	std::uint64_t marked = 0;
	std::size_t taken = 0;
	Fixed sampleRowX = rowX, sampleRowY = rowY;
	for (std::int64_t iy = 0; iy < stepsY; ++iy, sampleRowX += ydx, sampleRowY += ydy)
	{
		Fixed x = sampleRowX, y = sampleRowY;
		for (std::int64_t ix = 0; ix < stepsX; ++ix, x += xdx, y += xdy)
		{
			const auto cell = map.CellOf(x, y);
			if (cell[0] < 0 || cell[1] < 0 || cell[0] >= map.CellsX() || cell[1] >= map.CellsY() || taken >= allowance)
				continue;
			const std::uint64_t bit = std::uint64_t{1} << ((cell[1] - lowY) * boxWidth + (cell[0] - lowX));
			if ((marked & bit) != 0)
				continue;
			marked |= bit;
			++taken;
			take(cell[0], cell[1]);
		}
	}
	return true;
}

// Each cell `take(x, y)`, once, in that order; `seen` is working space for a footprint too large for the set kept on the
// stack (a hash set of the cells taken, reused).
template<typename Take>
inline void VisitFootprintCells(const ShroudMap &map, const PartitionFootprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	Take &&take, std::vector<std::uint32_t> &seen)
{
	using Engine::Math::Fixed;
	using namespace footprint_cells_detail;
	const Footprint shape = footprint.Shape();
	if (footprint.smallGeometry != 0)
	{
		// doSmallFill with at most 2 x 2 cells (the usual case): distinct cells, never more than the allowance (at
		// least 4), so neither the set nor the allowance is needed; taken in the same order as below.
		const Fixed radius = std::min(shape.major, map.CellSize() / Fixed::FromInt(2));
		const auto low = map.CellOf(at.x - radius, at.y - radius);
		const auto high = map.CellOf(at.x + radius, at.y + radius);
		if (high[0] - low[0] <= 1 && high[1] - low[1] <= 1)
		{
			for (std::int32_t x = low[0]; x <= high[0]; ++x)
				for (std::int32_t y = low[1]; y <= high[1]; ++y)
					if (x >= 0 && y >= 0 && x < map.CellsX() && y < map.CellsY())
						take(x, y);
			return;
		}
	}
	else if (shape.shape != FootprintShape::Circle && VisitRectCellsInBox(map, shape, at, facing, take))
		return;
	const std::size_t allowance = Allowance(map, shape);
	// The cells taken, as a set (open addressing): a cell is looked up in one probe or two, not against every cell
	// taken before it. On the stack up to 256 slots, else in `seen`.
	const std::size_t capacity = std::bit_ceil(std::max<std::size_t>(16, allowance * 2));
	std::array<std::uint32_t, 256> stackSet;
	std::uint32_t *set = stackSet.data();
	if (capacity <= stackSet.size())
		std::fill_n(set, capacity, 0u);
	else
	{
		seen.assign(capacity, 0u);
		set = seen.data();
	}
	const std::size_t mask = capacity - 1;
	std::size_t taken = 0;
	// addSubPixToCoverage: a cell on the map, once, while there is room (once full, nothing more is taken).
	const auto add = [&](std::int32_t x, std::int32_t y) {
		if (x < 0 || y < 0 || x >= map.CellsX() || y >= map.CellsY() || taken >= allowance)
			return;
		const std::uint32_t key = ((static_cast<std::uint32_t>(y) << 16) | static_cast<std::uint32_t>(x)) + 1u;
		for (std::size_t slot = (key * 0x9E3779B1u) & mask;; slot = (slot + 1) & mask)
		{
			if (set[slot] == key)
				return;
			if (set[slot] == 0)
			{
				set[slot] = key;
				break;
			}
		}
		++taken;
		take(x, y);
	};

	if (footprint.smallGeometry != 0)
	{
		// doSmallFill: truncated to half a cell.
		const Fixed radius = std::min(shape.major, map.CellSize() / Fixed::FromInt(2));
		const auto low = map.CellOf(at.x - radius, at.y - radius);
		const auto high = map.CellOf(at.x + radius, at.y + radius);
		for (std::int32_t x = low[0]; x <= high[0]; ++x)
			for (std::int32_t y = low[1]; y <= high[1]; ++y)
				add(x, y);
		return;
	}

	if (shape.shape == FootprintShape::Circle)
	{
		// doCircleFill.
		const auto centre = map.CellOf(at.x, at.y);
		const std::int32_t radius = std::max(CellsFor(map, shape.major), 1);
		std::int32_t y = radius - 1;
		std::int32_t dec = 3 - 2 * radius;
		const std::int32_t end = radius - 1;
		const auto line = [&](std::int32_t x1, std::int32_t x2, std::int32_t row) {
			for (std::int32_t x = x1; x <= x2; ++x)
				add(x, row);
		};
		// Retail's bound: y as it shrinks below 240 cells, else the radius less one.
		for (std::int32_t x = 0; x <= (radius < 240 ? y : end); ++x)
		{
			line(centre[0] - x, centre[0] + x, centre[1] + y);
			line(centre[0] - x, centre[0] + x, centre[1] - y);
			line(centre[0] - y, centre[0] + y, centre[1] + x);
			line(centre[0] - y, centre[0] + y, centre[1] - x);
			if (dec >= 0)
			{
				dec += (1 - y) << 2;
				--y;
			}
			dec += (x << 2) + 6;
		}
		return;
	}

	// doRectFill: from the top-left corner, half-cell steps along both axes.
	const Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	const Fixed step = map.CellSize() / Fixed::FromInt(2);
	const Fixed ydx = s * step, ydy = -c * step, xdx = c * step, xdy = s * step;
	// ceil(half size x 2 / step): the float product lands on the whole number when the quotient is one.
	const std::int64_t stepsX = (shape.major * Fixed::FromInt(2) / step).Ceil();
	const std::int64_t stepsY = (shape.minor * Fixed::FromInt(2) / step).Ceil();
	Fixed rowX = at.x - shape.major * c - shape.minor * s;
	Fixed rowY = at.y + shape.minor * c - shape.major * s;
	for (std::int64_t iy = 0; iy < stepsY; ++iy, rowX += ydx, rowY += ydy)
	{
		Fixed x = rowX, y = rowY;
		for (std::int64_t ix = 0; ix < stepsX; ++ix, x += xdx, y += xdy)
		{
			const auto cell = map.CellOf(x, y);
			add(cell[0], cell[1]);
		}
	}
}

// `out` receives the cells as (x, y) pairs; `seen` is working space (VisitFootprintCells).
inline void FootprintCells(const ShroudMap &map, const PartitionFootprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	std::vector<std::array<std::int32_t, 2>> &out, std::vector<std::uint32_t> &seen)
{
	out.clear();
	VisitFootprintCells(map, footprint, at, facing, [&](std::int32_t x, std::int32_t y) { out.push_back({x, y}); }, seen);
}

inline void FootprintCells(const ShroudMap &map, const PartitionFootprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	std::vector<std::array<std::int32_t, 2>> &out)
{
	std::vector<std::uint32_t> seen;
	FootprintCells(map, footprint, at, facing, out, seen);
}

// PartitionData::getShroudedStatus from its cells (before the client's ghost-object refinements): none (off the map)
// or all shrouded: shrouded; all fogged or shrouded: fogged; all clear: clear; else partly clear.
enum class ObjectShroudLevel : std::uint8_t
{
	Clear,
	PartialClear,
	Fogged,
	Shrouded,
};

inline ObjectShroudLevel ShroudLevelOf(const ShroudMap &map, std::uint32_t player, std::span<const std::array<std::int32_t, 2>> cells) noexcept
{
	std::size_t shrouded = 0, fogged = 0;
	for (const auto &cell : cells)
	{
		const CellShroud status = map.Status(player, cell[0], cell[1]);
		shrouded += status == CellShroud::Shrouded ? 1u : 0u;
		fogged += status == CellShroud::Fogged ? 1u : 0u;
	}
	if (cells.empty() || shrouded == cells.size())
		return ObjectShroudLevel::Shrouded;
	if (shrouded + fogged == cells.size())
		return ObjectShroudLevel::Fogged;
	if (shrouded == 0 && fogged == 0)
		return ObjectShroudLevel::Clear;
	return ObjectShroudLevel::PartialClear;
}
}
