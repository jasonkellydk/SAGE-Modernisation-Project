module;
#include <array>
#include <cstdint>

export module engine.navigation.movement.occupancy_policy;

export namespace navigation {
struct OccupancyCell {
    std::uint32_t unit = 0;
    bool valid = true, empty = true, goal = false, moving = false, fixed = false;
};
struct OccupancyQuery {
    std::uint32_t self = 0, ignored = 0;
    int x = 0, y = 0, radius = 0, cellsAbove = 0;
    bool considerTransient = false, infantryPassThrough = false;
    bool collectMovingTraffic = true;
};
struct OccupancyResult {
    int allyFixedCount = 0;
    bool enemyFixed = false, allyMoving = false, allyGoal = false;
};

// Goal-only and empty cells do not need a present-unit identity. On the native
// grid that identity requires an extra lookup through pooled cell storage.
template<class Resolve>
OccupancyCell resolveOccupant(OccupancyCell cell, Resolve resolve, bool includeMoving=true) {
    if (cell.valid && (!cell.empty || cell.goal) && ((includeMoving && cell.moving) || cell.fixed))
        cell.unit = resolve();
    return cell;
}

// Movement policy depends on the occupancy grid and unit relationships, not on
// object ownership or game singletons. The game adapter supplies those views.
template<int Radius, int Above, class ReadCell, class Units>
bool checkOccupancyFootprint(const OccupancyQuery& query, OccupancyResult& result,
                    ReadCell readCell, const Units& units)
{
    result = {};
    std::array<std::uint32_t, 5> allies;
    int numAllies = 0;
    // A large vehicle repeats across neighboring footprint cells. Resolve its
    // identity and relationship once per contiguous run, but still examine
    // each cell's independent goal/moving/fixed flags. This cache lives only
    // for this check; ownership and object lifetime may change before the next.
    decltype(units.find(std::uint32_t{})) previousUnit{};
    std::uint32_t previousId = 0;
    bool havePrevious = false, previousAllied = false;
    const int radius=Radius<0?query.radius:Radius;
    const int above=Above<0?query.cellsAbove:Above;
    for (int x = query.x - radius; x < query.x + above; ++x) {
        for (int y = query.y - radius; y < query.y + above; ++y) {
            const auto cell = readCell(x, y);
            if (!cell.valid) return false;
            if (cell.goal) result.allyGoal = true;
            else if (cell.empty) continue;
            // Boolean clearance callers do not consume moving-traffic hints.
            // Fixed and explicitly requested transient blockers retain policy.
            if (!query.collectMovingTraffic && !query.considerTransient && !cell.fixed) continue;
            if (cell.unit == query.self || cell.unit == query.ignored) continue;
            if (!cell.moving && !cell.fixed) continue;
            if (!havePrevious || previousId != cell.unit) {
                previousId = cell.unit;
                previousUnit = units.find(cell.unit);
                previousAllied = previousUnit && units.allied(previousUnit);
                havePrevious = true;
            }
            const auto unit = previousUnit;
            if (!unit) continue;
            const bool allied = previousAllied;
            if (cell.moving && allied) result.allyMoving = true;
            if (!cell.fixed && !query.considerTransient) continue;
            if (query.infantryPassThrough && units.infantry(unit)) continue;
            if (allied) {
                if (!units.canMoveAside(unit)) return false;
                bool found = false;
                for (int i = 0; i < numAllies; ++i)
                    if (allies[i] == cell.unit) found = true;
                if (!found) {
                    ++result.allyFixedCount;
                    if (numAllies < static_cast<int>(allies.size()))
                        allies[numAllies++] = cell.unit;
                }
            } else if (!units.canCrush(unit)) {
                result.enemyFixed = true;
            }
        }
    }
    return true;
}

template<class ReadCell, class Units>
bool checkOccupancy(const OccupancyQuery& query, OccupancyResult& result,
                    ReadCell readCell, const Units& units)
{
    // Rangers use a two-by-two footprint. Expose its fixed loop bounds to the
    // compiler while sharing the complete policy with other footprint sizes.
    if (query.radius==1 && query.cellsAbove==1)
        return checkOccupancyFootprint<1,1>(query,result,readCell,units);
    return checkOccupancyFootprint<-1,-1>(query,result,readCell,units);
}
}
