module;
#include <algorithm>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

export module engine.navigation.topology.footprint_reachability;
export import engine.navigation.topology.reachability_cache;

export namespace navigation {
struct AllReachabilityAnchors {
    bool operator()(int,int) const { return true; }
};
// Each footprint has its own terrain components. Occupancy stays query-local.
// Portal anchors remain optimistic even when their ground footprint is blocked.
class FootprintReachability {
    std::map<std::pair<int, bool>, ReachabilityCache> shapes_;
    ReachabilityCache* active_ = nullptr;
public:
    void reset() { shapes_.clear(); active_ = nullptr; }
    void invalidate() {
        for (auto& [shape, cache] : shapes_) cache.invalidate();
        active_ = nullptr;
    }
    void invalidate(int left, int top, int right, int bottom) {
        for (auto& [shape, cache] : shapes_) {
            const auto [radius, center] = shape;
            const int above = std::max(1, radius + int(center));
            // A changed cell affects every anchor whose footprint contains it.
            cache.invalidate(left - above + 1, top - above + 1,
                right + radius, bottom + radius);
        }
        active_ = nullptr;
    }
    // Invalidate when anchor permissions change. Anchor bounds need not limit
    // footprint cells: a unit can stand at an edge while extending beyond it.
    template<class ReadCell,class AllowAnchor=AllReachabilityAnchors>
    void prepare(int width, int height, int radius, bool center, ReadCell read,AllowAnchor allowAnchor={}) {
        if (radius < 0 || radius > std::numeric_limits<int>::max() / 4)
            throw std::invalid_argument("Invalid reachability footprint");
        auto& cache = shapes_[{radius, center}];
        const int above = std::max(1, radius + int(center));
        cache.prepare(width, height, [&](int x, int y) {
            if (!allowAnchor(x,y)) return ReachabilityCell{};
            const auto anchor = read(x, y);
            if (anchor.portal) return ReachabilityCell{true, anchor.portal};
            if (x < radius || y < radius || x > width - above || y > height - above)
                return ReachabilityCell{};
            for (int fy = y - radius; fy < y + above; ++fy)
                for (int fx = x - radius; fx < x + above; ++fx) {
                    const auto cell = read(fx, fy);
                    if (!cell.passable && !cell.portal) return ReachabilityCell{};
                }
            return ReachabilityCell{true, 0};
        });
        active_ = &cache;
    }
    bool connected(int x1, int y1, int x2, int y2) const {
        if (!active_) throw std::logic_error("Unsynchronized footprint connectivity");
        return active_->connected(x1, y1, x2, y2);
    }
};
}
