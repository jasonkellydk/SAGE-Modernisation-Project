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

// `out` receives the cells as (x, y) pairs.
inline void FootprintCells(const ShroudMap &map, const PartitionFootprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
	std::vector<std::array<std::int32_t, 2>> &out)
{
	using Engine::Math::Fixed;
	using namespace footprint_cells_detail;
	out.clear();
	const Footprint shape = footprint.Shape();
	const std::size_t allowance = Allowance(map, shape);
	// addSubPixToCoverage: a cell on the map, once, while there is room.
	const auto add = [&](std::int32_t x, std::int32_t y) {
		if (x < 0 || y < 0 || x >= map.CellsX() || y >= map.CellsY())
			return;
		for (const auto &cell : out)
			if (cell[0] == x && cell[1] == y)
				return;
		if (out.size() < allowance)
			out.push_back({x, y});
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
