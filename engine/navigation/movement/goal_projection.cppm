module;
#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>

export module engine.navigation.movement.goal_projection;

export namespace navigation {
struct DestinationCell {
    int x=0,y=0;
    bool operator==(const DestinationCell&) const = default;
};

// Gameplay destination adjustment uses a counter-clockwise spiral rather than
// nearest-cell ranking. Keep that deterministic order in the modern module so
// all callers share one implementation.
template<class Allowed>
std::optional<DestinationCell> projectGroundSpiral(DestinationCell goal,
    unsigned maximumCandidates, Allowed allowed) {
    if (allowed(goal.x,goal.y)) return goal;
    int x=goal.x,y=goal.y;
    unsigned remaining=maximumCandidates;
    unsigned segmentLength=1;
    constexpr DestinationCell directions[]{{1,0},{0,1},{-1,0},{0,-1}};
    while (remaining>0) {
        for (unsigned direction=0; direction<4 && remaining>0; ++direction) {
            for (unsigned step=0; step<segmentLength && remaining>0; ++step) {
                x+=directions[direction].x;
                y+=directions[direction].y;
                --remaining;
                if (allowed(x,y)) return DestinationCell{x,y};
            }
            if (direction&1u) ++segmentLength;
        }
    }
    return {};
}

// Grid-local coordinates. Expand until no unvisited cell can improve the
// squared distance; equal distances use row-major order, independent of rings.
template<class Allowed>
std::optional<DestinationCell> projectDestinationInGrid(DestinationCell goal,int width,int height,Allowed allowed) {
    if (width<=0 || height<=0) return {};
    goal.x=std::clamp(goal.x,0,width-1);
    goal.y=std::clamp(goal.y,0,height-1);
    std::optional<DestinationCell> best;
    std::int64_t distance=std::numeric_limits<std::int64_t>::max();
    const auto consider=[&](std::int64_t x,std::int64_t y) {
        if (x<0 || y<0 || x>=width || y>=height) return;
        const auto dx=x-goal.x,dy=y-goal.y,score=dx*dx+dy*dy;
        if (score>distance || (score==distance && best &&
            (y>best->y || (y==best->y && x>=best->x)))) return;
        if (allowed(static_cast<int>(x),static_cast<int>(y))) {
            best=DestinationCell{static_cast<int>(x),static_cast<int>(y)};
            distance=score;
        }
    };
    const std::int64_t limit=std::max({goal.x,goal.y,width-1-goal.x,height-1-goal.y});
    consider(goal.x,goal.y);
    for (std::int64_t radius=1;radius<=limit && radius*radius<=distance;++radius) {
        const std::int64_t left=std::max<std::int64_t>(0,goal.x-radius);
        const std::int64_t right=std::min<std::int64_t>(width-1,goal.x+radius);
        for (auto x=left;x<=right;++x) {
            consider(x,goal.y-radius);
            consider(x,goal.y+radius);
        }
        const std::int64_t top=std::max<std::int64_t>(0,goal.y-radius+1);
        const std::int64_t bottom=std::min<std::int64_t>(height-1,goal.y+radius-1);
        for (auto y=top;y<=bottom;++y) {
            consider(goal.x-radius,y);
            consider(goal.x+radius,y);
        }
    }
    return best;
}

// Choose a legal ground cell using the deterministic eight-probe expansion. This is
// deliberately not a nearest-cell search: the original ground pathfinder
// returned the first valid probe in its deterministic counter-clockwise
// walk, and callers can observe that choice when several cells are open.
template<class Allowed>
std::optional<DestinationCell> projectGroundDestination(DestinationCell goal, Allowed allowed, int radius=7) {
    if (radius<0 || radius>64) return std::nullopt;
    if (allowed(goal.x,goal.y)) return goal;
    const auto candidate=[&](std::int64_t x,std::int64_t y)
        -> std::optional<DestinationCell> {
        if (x<std::numeric_limits<int>::min() || x>std::numeric_limits<int>::max() ||
            y<std::numeric_limits<int>::min() || y>std::numeric_limits<int>::max()) return {};
        if (allowed(static_cast<int>(x),static_cast<int>(y)))
            return DestinationCell{static_cast<int>(x),static_cast<int>(y)};
        return {};
    };
    for (int offset=1; offset<=radius; ++offset) {
        std::int64_t x=goal.x,y=goal.y;
        if (auto result=candidate(x+offset,y)) return result;
        if (auto result=candidate(x+offset,y+offset)) return result;
        if (auto result=candidate(x,y+offset)) return result;
        if (auto result=candidate(x-offset,y+offset)) return result;
        if (auto result=candidate(x-offset,y)) return result;
        if (auto result=candidate(x-offset,y-offset)) return result;
        if (auto result=candidate(x,y-offset)) return result;
        if (auto result=candidate(x+offset,y-offset)) return result;
    }
    return {};
}
}
