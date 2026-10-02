export module engine.gameplay.rts.navigation.resources.unit_cells;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Which ground unit stands on each ground pathfinding cell (PathfindCell's m_posUnitID, set by Pathfinder::updatePos for
// every unit whose AI moves it on the ground: the cells its footprint covers about the cell it is in), with what a route
// search needs to know of it. A unit standing in its own goal cell is fixed there (UNIT_PRESENT_FIXED: its goal claim in
// GoalCells is its own); otherwise it is passing (UNIT_PRESENT_MOVING).
// Rebuilt at the start of every tick from where the units are (the original updated a unit's cells as it changed cell, a
// later unit's mark replacing an earlier one's, and a unit leaving cleared only its own mark, so a unit standing where
// another passed lost its mark until it moved: here every unit covers its cells every tick, the lower ObjectID holding a
// cell two share). Derived state: not checkpointed.
// Cells are grouped in blocks of 8 x 8 holding whether any unit stands in them, so a search asks about a footprint
// without reading its cells where no unit is near.
export namespace engine::gameplay
{
namespace unit_cell_flag
{
inline constexpr std::uint8_t Infantry = 1u << 0;
inline constexpr std::uint8_t Squishable = 1u << 1;
inline constexpr std::uint8_t Dozer = 1u << 2;
inline constexpr std::uint8_t Harvester = 1u << 3;
}

struct UnitOccupant
{
	ecs::Entity entity;
	std::uint32_t player{0};
	std::uint32_t team{0xFFFFFFFFu}; // its team (none: Relationships::NoTeam)
	std::uint32_t crushableLevel{255};
	std::uint8_t flags{0};
};

struct UnitCells
{
	static constexpr std::int32_t BlockShift = 3;

	std::int32_t width{0}, height{0};
	std::int32_t blocksWide{0}, blocksHigh{0};
	std::vector<std::uint32_t> occupant; // per cell: 1 + its index in `occupants`; 0: nobody
	std::vector<std::uint8_t> blocks;    // per block of 8 x 8 cells: anybody
	std::vector<UnitOccupant> occupants;
	std::vector<std::uint32_t> touched;  // the cells marked since the last Clear

	void Fit(std::int32_t gridWidth, std::int32_t gridHeight)
	{
		if (gridWidth == width && gridHeight == height && !occupant.empty())
			return;
		width = std::max(gridWidth, 0);
		height = std::max(gridHeight, 0);
		blocksWide = (width + 7) >> BlockShift;
		blocksHigh = (height + 7) >> BlockShift;
		occupant.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0u);
		blocks.assign(static_cast<std::size_t>(blocksWide) * static_cast<std::size_t>(blocksHigh), 0u);
		occupants.clear();
		touched.clear();
	}

	void Clear() noexcept
	{
		for (const std::uint32_t cell : touched)
		{
			occupant[cell] = 0;
			const auto x = static_cast<std::int32_t>(cell % static_cast<std::uint32_t>(width));
			const auto y = static_cast<std::int32_t>(cell / static_cast<std::uint32_t>(width));
			blocks[static_cast<std::size_t>((y >> BlockShift) * blocksWide + (x >> BlockShift))] = 0;
		}
		touched.clear();
		occupants.clear();
	}

	bool Contains(std::int32_t x, std::int32_t y) const noexcept { return x >= 0 && y >= 0 && x < width && y < height; }

	// The unit `index` (in `occupants`) covers cell (x, y).
	void Mark(std::int32_t x, std::int32_t y, std::uint32_t index)
	{
		if (!Contains(x, y))
			return;
		const auto cell = static_cast<std::uint32_t>(y * width + x);
		if (occupant[cell] == 0)
			touched.push_back(cell);
		occupant[cell] = index + 1u;
		blocks[static_cast<std::size_t>((y >> BlockShift) * blocksWide + (x >> BlockShift))] = 1;
	}

	// Who stands on cell (x, y) (none: nullptr).
	const UnitOccupant *At(std::int32_t x, std::int32_t y) const noexcept
	{
		if (!Contains(x, y))
			return nullptr;
		const std::uint32_t at = occupant[static_cast<std::size_t>(y * width + x)];
		return at == 0 ? nullptr : &occupants[at - 1u];
	}

	// Whether anybody may stand in the cells x0..x1, y0..y1 (inclusive): their blocks.
	bool AnyNear(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) const noexcept
	{
		if (occupants.empty())
			return false;
		const std::int32_t bx0 = std::max(x0, 0) >> BlockShift, by0 = std::max(y0, 0) >> BlockShift;
		const std::int32_t bx1 = std::min(x1, width - 1) >> BlockShift, by1 = std::min(y1, height - 1) >> BlockShift;
		for (std::int32_t by = by0; by <= by1; ++by)
			for (std::int32_t bx = bx0; bx <= bx1; ++bx)
				if (blocks[static_cast<std::size_t>(by * blocksWide + bx)] != 0)
					return true;
		return false;
	}
};

// The cell a unit is in (Pathfinder::updatePos: a footprint centred in its cell, the cell it is in; else about a cell
// corner, the nearest corner's cell) and the cells its footprint covers about it: `radius` before and `radius` after
// (one more when centred).
struct UnitFootprint
{
	std::int32_t x0, y0, x1, y1; // inclusive
};

inline std::int32_t FloorDiv10(std::int64_t raw16) noexcept
{
	// raw16: a Fixed's raw value (16 fraction bits); floor(raw / 65536 / 10).
	const std::int64_t scaled = 10 * 65536;
	return static_cast<std::int32_t>(raw16 >= 0 ? raw16 / scaled : -((-raw16 + scaled - 1) / scaled));
}

inline UnitFootprint FootprintAt(std::int64_t xRaw, std::int64_t yRaw, std::uint8_t radius, bool centered) noexcept
{
	constexpr std::int64_t HalfCell = 5 * 65536;
	const std::int32_t cx = centered ? FloorDiv10(xRaw) : FloorDiv10(xRaw + HalfCell);
	const std::int32_t cy = centered ? FloorDiv10(yRaw) : FloorDiv10(yRaw + HalfCell);
	const std::int32_t above = radius + (centered ? 1 : 0);
	return {cx - radius, cy - radius, cx + above - 1, cy + above - 1};
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::UnitCells>
{
	static constexpr std::string_view StableName = "engine.gameplay.unit_cells";
};
}
