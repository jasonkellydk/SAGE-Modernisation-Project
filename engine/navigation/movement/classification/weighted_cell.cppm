module;
#include <cstdint>
#include <limits>

export module engine.navigation.movement.classification.weighted_cell;
import engine.navigation.movement.terrain_policy;
import engine.navigation.movement.occupancy_policy;

export namespace navigation {
struct WeightedCellQuery {
    TerrainQuery terrain;
    int radius=0, cellsAbove=1;
    int left=0, top=0, right=0, bottom=0;
    bool restrictToBounds=false, corridor=false, dozer=false;
};
struct WeightedTerrainCell {
    TerrainCell terrain;
    bool pinched=false, dozerPassage=false;
};

// ReadTerrain and CheckTraffic may refer to live data during a synchronous
// query, or exclusively to captured values for a query paused between updates.
// Logical bounds apply to the anchor, not to the whole physical footprint.
template<class ReadTerrain,class CheckTraffic>
bool classifyWeightedCell(const WeightedCellQuery& query,int x,int y,
    OccupancyResult& traffic,ReadTerrain readTerrain,CheckTraffic checkTraffic) {
    traffic={};
    if (query.restrictToBounds &&
        (x<query.left || x>query.right || y<query.top || y>query.bottom)) return false;
    const auto permitted=[&](const WeightedTerrainCell& cell) {
        return permitsTerrain(query.terrain,cell.terrain) ||
            (query.dozer && cell.dozerPassage && cell.terrain.valid && cell.terrain.kind==TerrainKind::obstacle);
    };
    const auto anchor=readTerrain(x,y);
    if ((query.corridor && anchor.pinched) || !permitted(anchor)) return false;
    // DX9 validates terrain at the anchor. Its map classification already
    // expands cliffs; the physical footprint belongs to the traffic check.
    // Rejecting neighboring terrain here changes ordinary detours and access.
    // A group corridor performs its own width/narrowing check downstream.
    if (query.corridor) return true;
    if (query.radius<0 || query.cellsAbove<1) return false;
    const auto left=std::int64_t(x)-query.radius,top=std::int64_t(y)-query.radius;
    const auto right=std::int64_t(x)+query.cellsAbove,bottom=std::int64_t(y)+query.cellsAbove;
    // The downstream occupancy policy also uses representable half-open ends.
    if (left<std::numeric_limits<int>::min() || top<std::numeric_limits<int>::min() ||
        right>std::numeric_limits<int>::max() || bottom>std::numeric_limits<int>::max()) return false;
    return checkTraffic(traffic) && !traffic.enemyFixed;
}
}
