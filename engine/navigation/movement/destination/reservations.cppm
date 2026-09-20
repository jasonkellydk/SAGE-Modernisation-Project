module;
#include <cstdint>
#include <limits>

export module engine.navigation.movement.destination.reservations;
import engine.navigation.movement.terrain_policy;

export namespace navigation {
struct DestinationQuery {
    std::uint32_t self=0, ignored=0;
    int radius=0;
    bool center=true, hasMover=false, aircraft=false, rejectImpassable=true;
};
struct ReservationCell {
    TerrainKind terrain=TerrainKind::ground;
    std::uint32_t obstacleId=0, goalId=0, aircraftId=0;
    bool present=false, noUnits=true, stationary=false, reservedForAircraft=false;
    bool valid() const { return present; }
    TerrainKind kind() const { return terrain; }
    std::uint32_t obstacle() const { return obstacleId; }
    std::uint32_t goal() const { return goalId; }
    std::uint32_t aircraftGoal() const { return aircraftId; }
    bool empty() const { return noUnits; }
    bool fixed() const { return stationary; }
    bool aircraftReserved() const { return reservedForAircraft; }
};

// Cell views expose goal identities lazily: native identities live in pooled
// storage and need not be fetched for empty cells or rejected terrain.
template<class Cell,class Units>
bool permitsDestinationCell(const DestinationQuery& query,const Cell& cell,const Units& units) {
    if (!cell.valid()) return false;
    if (query.aircraft) return !cell.aircraftReserved() || cell.aircraftGoal()==query.self;
    const auto terrain=cell.kind();
    if (terrain==TerrainKind::obstacle)
        return query.ignored!=0 && cell.obstacle()==query.ignored;
    if (query.rejectImpassable &&
        (terrain==TerrainKind::impassable || terrain==TerrainKind::blockedBridge)) return false;
    if (cell.empty()) return true;
    const auto goal=cell.goal();
    if (goal==0 || goal==query.self || goal==query.ignored) return true;
    if (!query.hasMover) return false;
    const auto unit=units.find(goal);
    if (!unit) return true;
    if (units.allied(unit)) return false;
    return !cell.fixed() || units.canCrush(unit);
}

template<class ReadCell,class Units>
bool permitsDestination(const DestinationQuery& query,int x,int y,ReadCell readCell,const Units& units) {
    if (query.radius<0) return false;
    const auto above=std::int64_t(query.radius)+(query.center?1:0);
    if (query.radius==0 && above==0) return true;
    const auto left=std::int64_t(x)-query.radius,top=std::int64_t(y)-query.radius;
    const auto right=std::int64_t(x)+above,bottom=std::int64_t(y)+above;
    if (left<std::numeric_limits<int>::min() || top<std::numeric_limits<int>::min() ||
        right>std::int64_t(std::numeric_limits<int>::max())+1 ||
        bottom>std::int64_t(std::numeric_limits<int>::max())+1) return false;
    for (auto xx=left;xx<right;++xx) for (auto yy=top;yy<bottom;++yy)
        if (!permitsDestinationCell(query,readCell(int(xx),int(yy)),units)) return false;
    return true;
}
}
