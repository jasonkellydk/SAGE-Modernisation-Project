module;
#include <vector>

export module engine.navigation.movement.following.route_geometry;

export namespace navigation::following {
struct RoutePoint { float x{}, y{}, z{}; };

// Immutable between route edits. Prefix lengths preserve route-order floating
// accumulation and remove repeated length summation from following queries.
template<class Handle>
class RouteGeometry {
    struct Segment {
        RoutePoint start, end;
        float dx, dy, length, prefix;
        Handle handle;
        float minX, maxX, minY, maxY;
    };
    std::vector<Segment> segments_;
    float length_{};
public:
    struct Closest {
        Handle handle{};
        RoutePoint point{};
        float along{};
        float distanceSquared{99999999.9f};
    };

    void clear() { segments_.clear(); length_=0; }
    void append(RoutePoint start, RoutePoint end, float dx, float dy,
                float length, Handle handle) {
        segments_.push_back({start,end,dx,dy,length,length_,handle,
            (std::min)(start.x,end.x),(std::max)(start.x,end.x),
            (std::min)(start.y,end.y),(std::max)(start.y,end.y)});
        length_+=length;
    }
    float length() const { return length_; }

    Closest closest(RoutePoint position) const {
        Closest result;
        if (segments_.size()==1) {
            const auto& segment=segments_.front();
            float along=segment.dx*(position.x-segment.start.x)+
                        segment.dy*(position.y-segment.start.y);
            RoutePoint point;
            if (along<0) {
                along=0;
                point=segment.start;
            } else if (along>segment.length) {
                along=segment.length;
                point=segment.end;
            } else {
                point={segment.start.x+along*segment.dx,
                       segment.start.y+along*segment.dy,0};
            }
            const float offsetX=position.x-point.x,offsetY=position.y-point.y;
            result={segment.handle,point,segment.prefix+along,
                offsetX*offsetX+offsetY*offsetY};
            return result;
        }
        for (const auto& segment:segments_) {
            // Once a nearby segment has supplied a useful upper bound, a
            // segment whose axis-aligned bounds are farther away cannot win.
            // This is only a rejection test: candidates are still visited in
            // route order and the original projection/tie behaviour is kept.
            const float dx = position.x < segment.minX ? segment.minX-position.x :
                position.x > segment.maxX ? position.x-segment.maxX : 0.0f;
            const float dy = position.y < segment.minY ? segment.minY-position.y :
                position.y > segment.maxY ? position.y-segment.maxY : 0.0f;
            if (dx*dx + dy*dy > result.distanceSquared) continue;
            float along=segment.dx*(position.x-segment.start.x)+
                        segment.dy*(position.y-segment.start.y);
            RoutePoint point;
            if (along<0) {
                along=0;
                point=segment.start;
            } else if (along>segment.length) {
                // Past an interior edge, the following segment owns the turn.
                if (&segment!=&segments_.back()) continue;
                along=segment.length;
                point=segment.end;
            } else {
                point={segment.start.x+along*segment.dx,
                       segment.start.y+along*segment.dy,0};
            }
            const float offsetX=position.x-point.x,offsetY=position.y-point.y;
            const float distance=offsetX*offsetX+offsetY*offsetY;
            if (distance<result.distanceSquared) {
                result={segment.handle,point,segment.prefix+along,distance};
            }
        }
        return result;
    }
};
}
