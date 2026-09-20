module;
#include <cstdint>
#include <limits>

export module engine.navigation.movement.corridor.clearance;
import engine.navigation.movement.terrain_policy;

export namespace navigation {
struct CorridorCell {
    TerrainKind terrain=TerrainKind::ground;
    std::uint32_t occupant=0;
    bool present=false,fence=false,stationary=false;
    bool valid() const { return present; }
    TerrainKind kind() const { return terrain; }
    bool isFence() const { return fence; }
    bool fixed() const { return stationary; }
    std::uint32_t unit() const { return occupant; }
};

// Stable readers can refer to native cells or captured values. Oversized
// footprints touching missing cells retain the native zero-clearance result.
// Blocked footprints shrink by two cells without recursive native calls.
template<class ReadCell,class Units>
int corridorClearance(int x,int y,int diameter,bool crusher,ReadCell readCell,const Units& units) {
    if (diameter<0) return 0;
    for (;;) {
        const int radius=diameter/2,above=radius==0?1:radius;
        const auto left=std::int64_t(x)-radius,top=std::int64_t(y)-radius;
        const auto right=std::int64_t(x)+above,bottom=std::int64_t(y)+above;
        if (left<std::numeric_limits<int>::min() || top<std::numeric_limits<int>::min() ||
            right>std::int64_t(std::numeric_limits<int>::max())+1 ||
            bottom>std::int64_t(std::numeric_limits<int>::max())+1) return 0;
        bool clear=true;
        for (auto xx=left;xx<right;++xx) {
            const bool edgeX=xx==left || xx==right-1;
            for (auto yy=top;yy<bottom;++yy) {
                if (radius>1 && edgeX && (yy==top || yy==bottom-1)) continue;
                const auto cell=readCell(int(xx),int(yy));
                if (!cell.valid()) return 0;
                if (cell.kind()!=TerrainKind::ground &&
                    !(cell.kind()==TerrainKind::obstacle && cell.isFence() && crusher)) clear=false;
                if (cell.fixed() && diameter>=2) {
                    const auto unit=units.find(cell.unit());
                    if (unit && units.crushableLevel(unit)>(crusher?1u:0u)) clear=false;
                }
                // Preserve the native column traversal, including missing-cell
                // rejection in subsequent columns after a blocker was found.
                if (!clear) break;
            }
        }
        if (clear) return radius==0?1:2*radius;
        if (diameter<2) return 0;
        diameter-=2;
    }
}
}
