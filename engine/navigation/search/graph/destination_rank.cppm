module;
#include <optional>
#include <algorithm>
#include <cstdint>
#include <limits>

export module engine.navigation.search.graph.destination_rank;

export namespace navigation {
// Copy this after resolving the requested destination and obstacle exceptions.
// Keep the two cost factors separate to preserve native floating-point order.
struct DestinationRankQuery {
    int x=0,y=0;
    unsigned layer=1;
    bool allowReservedExact=false;
    double costToDistanceSquared=0;
    float pathCostMultiplier=0;
};

inline double remainingDestinationRankBound(const DestinationRankQuery& query,unsigned cost,unsigned heuristic) {
    const double alpha=query.costToDistanceSquared*query.pathCostMultiplier;
    if (!(alpha>0)) return 0;
    // Octile h = 10*max(dx,dy)+4*min(dx,dy) <= sqrt(116)*distance.
    // Consistent A* pops nondecreasing f=g+h. Cauchy therefore gives
    // distance^2 + alpha*g^2 >= alpha*f^2/(1+116*alpha) for every
    // unsettled endpoint. Match the search heap's saturated priority.
    const double f=double(std::min<std::uint64_t>(std::uint64_t(cost)+heuristic,
        std::numeric_limits<std::uint32_t>::max()));
    const double bound=alpha*f*f/(1+116*alpha);
    return bound*(1-16*std::numeric_limits<double>::epsilon());
}

template<class Allowed>
std::optional<double> rankDestination(const DestinationRankQuery& query,
    int x,int y,unsigned layer,unsigned cost,Allowed allowed) {
    const bool exact=x==query.x && y==query.y && layer==query.layer;
    if (!(exact && query.allowReservedExact) && !allowed(x,y,layer)) return {};
    const double dx=double(query.x)-x,dy=double(query.y)-y;
    return dx*dx+dy*dy+double(cost)*cost*query.costToDistanceSquared*query.pathCostMultiplier;
}
}
