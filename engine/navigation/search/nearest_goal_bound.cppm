module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

export module engine.navigation.search.nearest_goal_bound;
export namespace navigation {
// Resumable distance-only endpoint bound. Acceptance must read a stable view
// throughout the scan. One work credit is one in-bounds layer candidate.
class NearestGoalBound {
    std::int64_t left_=0,top_=0,right_=0,bottom_=0,x_=0,y_=0,radius_=0,limit_=0,offset_=0;
    unsigned layers_=0,layer_=0,phase_=0,edge_=0;
    bool done_=true;
    double minimum_=std::numeric_limits<double>::infinity();
    std::uint64_t examined_=0;
    bool candidate(int& x,int& y) {
        while (!done_) {
            if (radius_>limit_ || double(radius_)*radius_>minimum_) { done_=true;return false; }
            if (phase_==0) { x=int(x_);y=int(y_);return true; }
            if (phase_==1) {
                offset_=std::max(offset_,left_-x_);
                if (offset_>std::min(radius_,right_-x_) || (y_-radius_<top_ && y_+radius_>bottom_)) {
                    phase_=2;edge_=0;offset_=-radius_+1;continue;
                }
                const auto yy=y_+(edge_?radius_:-radius_);
                if (yy>=top_ && yy<=bottom_) { x=int(x_+offset_);y=int(yy);return true; }
            } else {
                offset_=std::max(offset_,top_-y_);
                if (offset_>std::min(radius_-1,bottom_-y_) || (x_-radius_<left_ && x_+radius_>right_)) {
                    ++radius_;phase_=1;edge_=0;offset_=-radius_;continue;
                }
                const auto xx=x_+(edge_?radius_:-radius_);
                if (xx>=left_ && xx<=right_) { x=int(xx);y=int(y_+offset_);return true; }
            }
            if (++edge_==2) { edge_=0;++offset_; }
        }
        return false;
    }
public:
    void begin(int left,int top,int right,int bottom,int x,int y,unsigned layers) {
        if (left>right || top>bottom || x<left || x>right || y<top || y>bottom || !layers)
            throw std::invalid_argument("Invalid endpoint-bound grid");
        left_=left;top_=top;right_=right;bottom_=bottom;x_=x;y_=y;layers_=layers;
        radius_=offset_=0;layer_=phase_=edge_=0;done_=false;examined_=0;
        minimum_=std::numeric_limits<double>::infinity();
        limit_=std::max({x_-left_,right_-x_,y_-top_,bottom_-y_});
    }
    template<class Accept>
    unsigned advance(unsigned budget,Accept accept) {
        unsigned used=0;
        int x,y;
        while (used<budget && candidate(x,y)) {
            if (accept(x,y,layer_)) {
                const double dx=double(x)-x_,dy=double(y)-y_;
                minimum_=std::min(minimum_,dx*dx+dy*dy);
            }
            ++used;++examined_;
            if (++layer_==layers_) {
                layer_=0;
                if (phase_==0) { radius_=1;phase_=1;offset_=-1; }
                else if (++edge_==2) { edge_=0;++offset_; }
            }
        }
        return used;
    }
    void cancel() { done_=true;minimum_=std::numeric_limits<double>::infinity();examined_=0; }
    bool complete() const { return done_; }
    std::uint64_t examined() const { return examined_; }
    std::optional<double> minimum() const {
        return minimum_==std::numeric_limits<double>::infinity()?std::nullopt:std::optional<double>{minimum_};
    }
};
}
