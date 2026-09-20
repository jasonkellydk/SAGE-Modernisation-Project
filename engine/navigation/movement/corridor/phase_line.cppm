module;
#include <algorithm>
#include <cstdint>

export module engine.navigation.movement.corridor.phase_line;

export namespace navigation {
struct LineCell { std::int32_t x=0,y=0; };

struct PhaseLineSample { std::int64_t x=0,y=0; };
;

// Owned raster state for a search suspended between individual line samples.
// No visitor or world reference survives a call. The existing synchronous
// visitor below remains the production path until bounded expansion is wired.
class PhaseLineCursor {
    std::int64_t x_,y_,major_,minor_,sx_,sy_,phase_,step_=0;
    bool horizontal_,crossing_=false,done_=false;
    void finishMajor() {
        if (horizontal_) x_+=sx_; else y_+=sy_;
        if (++step_>major_) done_=true;
    }
public:
    PhaseLineCursor(LineCell start,LineCell end):x_(start.x),y_(start.y) {
        const auto dx=std::int64_t(end.x)-start.x,dy=std::int64_t(end.y)-start.y;
        const auto ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
        horizontal_=ax>=ay;
        major_=horizontal_?ax:ay;minor_=horizontal_?ay:ax;
        sx_=dx<0?-1:1;sy_=dy<0?-1:1;
        phase_=major_/2;
    }
    bool done() const { return done_; }
    bool atMajorStep() const { return !done_ && !crossing_; }
    PhaseLineSample position() const { return {x_,y_}; }
    bool next(PhaseLineSample& sample) {
        if (done_) return false;
        sample={x_,y_};
        if (crossing_) {
            crossing_=false;
            finishMajor();
        } else {
            phase_+=minor_;
            if (phase_>=major_) {
                phase_-=major_;
                if (horizontal_) y_+=sy_; else x_+=sx_;
                crossing_=true;
            } else finishMajor();
        }
        return true;
    }
};

// Visit the major-axis cell followed by the minor-axis crossing, when present.
// Crossing samples include the final phase, including the zero-length case;
// movement, bridge and visibility callers depend on this sample sequence.
// Return false immediately when the visitor rejects a cell. Wide arithmetic
// keeps endpoint subtraction and a crossing beyond INT32_MAX well-defined.
template<class Visit>
bool visitPhaseLine(LineCell start,LineCell end,Visit visit) {
    const std::int64_t dx=std::int64_t(end.x)-start.x,dy=std::int64_t(end.y)-start.y;
    const auto ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    const std::int64_t sx=dx<0?-1:1,sy=dy<0?-1:1;
    const bool horizontal=ax>=ay;
    const auto major=horizontal?ax:ay,minor=horizontal?ay:ax;
    std::int64_t x=start.x,y=start.y,phase=major/2;
    for (std::int64_t step=0;step<=major;++step) {
        if (!visit(x,y)) return false;
        phase+=minor;
        if (phase>=major) {
            phase-=major;
            if (horizontal) y+=sy; else x+=sx;
            if (!visit(x,y)) return false;
        }
        if (horizontal) x+=sx; else y+=sy;
    }
    return true;
}

}
