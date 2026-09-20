module;
#include <cmath>
#include <span>
#include <stdexcept>
#include <vector>

export module engine.navigation.movement.corridor.yield_paths;

export namespace navigation {
struct YieldSegment { float x1,y1,x2,y2,width; };
struct YieldDestinationQuery {
    int startX=0,startY=0;
    unsigned startLayer=1;
    bool centered=true;
    float cellSize=10;
};

// Own optimized path segments: neither source Path nor its units survive here.
class YieldPaths {
    std::vector<YieldSegment> segments_;
public:
    explicit YieldPaths(std::span<const YieldSegment> segments) : segments_(segments.begin(),segments.end()) {
        for (const auto& s:segments_) {
            if (!std::isfinite(s.x1) || !std::isfinite(s.y1) || !std::isfinite(s.x2) ||
                !std::isfinite(s.y2) || !std::isfinite(s.width) || s.width<0)
                throw std::invalid_argument("Invalid yield corridor");
        }
    }
    // Intersect is a stateless geometry adapter, never a live world reader.
    template<class Intersect>
    bool clear(float x,float y,Intersect intersect) const {
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        for (const auto& segment:segments_)
            if (intersect(segment,x-segment.width,y-segment.width,x+segment.width,y+segment.width))
                return false;
        return true;
    }
    std::size_t size() const { return segments_.size(); }
    template<class Allowed,class Intersect>
    bool destination(const YieldDestinationQuery& query,int x,int y,unsigned layer,
        Allowed allowed,Intersect intersect) const {
        if (x==query.startX && y==query.startY && layer==query.startLayer) return false;
        if (!allowed(x,y,layer)) return false;
        // Match native coordinate placement, including the double literal for
        // uncentered footprints. Height does not affect a planar corridor.
        const float px=query.centered?(float(x)+0.5f)*query.cellSize:float((float(x)+0.05)*query.cellSize);
        const float py=query.centered?(float(y)+0.5f)*query.cellSize:float((float(y)+0.05)*query.cellSize);
        return clear(px,py,intersect);
    }
};
}
