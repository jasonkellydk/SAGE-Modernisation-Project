module;
#include <algorithm>
#include <cstdint>

export module engine.navigation.movement.destination.flee_goal;

export namespace navigation {
struct FleeQuery {
    float firstX=0,firstY=0,secondX=0,secondY=0,radius=0;
    unsigned searchLimit=2000;
};
class FleeGoal {
    FleeQuery query_;
    std::uint64_t examined_=0;
    float farthest_=0;
public:
    explicit FleeGoal(FleeQuery query):query_(query) {}
    static float distanceSquared(const FleeQuery& query,float x,float y) {
        const float ax=x-query.firstX,ay=y-query.firstY;
        const float bx=x-query.secondX,by=y-query.secondY;
        return std::min(ax*ax+ay*ay,bx*bx+by*by);
    }
    // The caller still checks occupancy/reservations. Beyond the fixed search
    // limit, accept a new distance improvement instead of scanning the whole map.
    bool consider(float x,float y) {
        ++examined_;
        const float distance=distanceSquared(query_,x,y);
        const bool farther=distance>farthest_;
        farthest_=std::max(farthest_,distance);
        return distance>query_.radius*query_.radius || (examined_>query_.searchLimit && farther);
    }
};
}
