export module engine.gameplay.rts.navigation.algorithms.unit_movement;
import std;

export import engine.gameplay.rts.navigation.resources.unit_cells;
export import engine.gameplay.rts.navigation.resources.goal_cells;
export import engine.gameplay.common.identity.resources.relationships;

// The units a ground route search weighs (Pathfinder::checkForMovement, AIPathfind.cpp, EA's Zero Hour source): over the
// cells the mover's footprint would cover about a cell,
//   a unit passing through (not standing in its own goal cell) that the mover counts an ally is noted (allyMoving: the
//   search adds to the cost of cells near the mover's start); passing units do not block (considerTransient is off for
//   routes);
//   a unit standing in its own goal cell (UNIT_PRESENT_FIXED) that the mover counts an ally is counted (allyFixedCount:
//   costlier, and the route is blocked by allies, so they are asked to make way); one that is not an ally blocks the cell
//   (enemyFixed) unless the mover crushes or squishes it;
//   the mover itself and the obstacle it ignores are not counted.
export namespace engine::gameplay
{
// The mover, as the unit rules see it.
struct RouteUnits
{
	const UnitCells *cells{nullptr};
	const GoalCells *goals{nullptr};
	const Relationships *relationships{nullptr};
	ecs::Entity self;
	ecs::Entity ignored;
	std::uint32_t player{0};
	std::uint32_t team{Relationships::NoTeam};
	std::uint32_t crusherLevel{0};
	bool unmanned{false};
	bool centered{true};
	bool throughUnits{false}; // canPathThroughUnits: allies standing in the way cost more but do not block the route
};

struct MovementCheck
{
	std::uint8_t allyFixedCount{0};
	bool allyMoving{false};
	bool enemyFixed{false};
};

inline bool CrushesOrSquishes(const RouteUnits &units, const UnitOccupant &other, bool allied) noexcept
{
	if (units.unmanned || allied || units.crusherLevel == 0)
		return false;
	return (other.flags & unit_cell_flag::Squishable) != 0 || units.crusherLevel > other.crushableLevel;
}

// The units about cell (x, y) for a mover whose footprint reaches `radius` cells.
inline MovementCheck CheckForMovement(const RouteUnits &units, std::int32_t x, std::int32_t y, std::uint8_t radius) noexcept
{
	MovementCheck check;
	const std::int32_t above = radius + (units.centered ? 1 : 0);
	const std::int32_t x0 = x - radius, y0 = y - radius, x1 = x + above - 1, y1 = y + above - 1;
	if (!units.cells->AnyNear(x0, y0, x1, y1))
		return check;
	constexpr std::size_t MaxAllies = 5;
	std::array<ecs::Entity, MaxAllies> allies{};
	std::size_t counted = 0;
	for (std::int32_t cx = x0; cx <= x1; ++cx)
		for (std::int32_t cy = y0; cy <= y1; ++cy)
		{
			const UnitOccupant *other = units.cells->At(cx, cy);
			if (other == nullptr || other->entity == units.self || other->entity == units.ignored)
				continue;
			const bool allied = units.relationships->Allies(units.team, units.player, other->team, other->player);
			const bool fixed = units.goals->At(cx, cy) == other->entity;
			if (!fixed)
			{
				check.allyMoving = check.allyMoving || allied;
				continue;
			}
			if (allied)
			{
				const bool seen = std::find(allies.begin(), allies.begin() + static_cast<std::ptrdiff_t>(counted), other->entity) != allies.begin() + static_cast<std::ptrdiff_t>(counted);
				if (!seen)
				{
					++check.allyFixedCount;
					if (counted < MaxAllies)
						allies[counted++] = other->entity;
				}
			}
			else if (!CrushesOrSquishes(units, *other, allied))
				check.enemyFixed = true;
		}
	return check;
}
}
