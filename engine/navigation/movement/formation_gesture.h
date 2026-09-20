#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace navigation {
enum class FormationInput { LeftDown, RightDown, LeftUp, RightUp, Move, Cancel };
struct FormationGestureResult { bool capture=false, began=false, changed=false, commit=false, cancelled=false; };

// Client-only gesture state. Only the final facing and spacing enter the
// simulation command; event timing and preview animation never do.
class FormationGesture {
public:
    void reset() { *this=FormationGesture{}; }
    bool active() const { return active_; }
    float facing() const { return facing_; }
    bool visible() const { return visible_; }
    float frontage() const { return frontage_; }
    FormationGestureResult update(FormationInput input, float x, float y, bool canBegin,
        float initialFacing=0, std::uint64_t milliseconds=0) {
        FormationGestureResult result;
        if (input == FormationInput::Cancel) {
            result.capture = active_ || draining_;
            result.cancelled = active_;
            active_ = false;
            visible_ = false;
            draining_ = left_ || right_;
            return result;
        }
        if (input == FormationInput::LeftDown) left_ = true;
        if (input == FormationInput::RightDown) right_ = true;
        if (input == FormationInput::LeftUp) left_ = false;
        if (input == FormationInput::RightUp) right_ = false;
        if (draining_) {
            result.capture = true;
            draining_ = left_ || right_;
            return result;
        }
        if (!active_ && left_ && right_ && canBegin) {
            active_ = true;
            anchorX_ = x; anchorY_ = y;
            baseFacing_ = facing_ = initialFacing;
            frontage_ = 20;
            visible_ = false;
            haveDirection_ = false;
            started_ = milliseconds;
            result.began = true;
        }
        if (!active_) return result;
        result.capture = true;
        const float dx=x-anchorX_, dy=y-anchorY_;
        const float distance=std::hypot(dx,dy);
        if (distance >= 40) {
            facing_=std::atan2(dy,dx);
            frontage_=distance;
            haveDirection_=true;
            visible_=true;
        } else if (!haveDirection_) {
            facing_=baseFacing_;
            frontage_=20;
        }
        visible_=visible_ || milliseconds-started_ >= 300;
        result.changed = true;
        if (!left_ || !right_) {
            result.commit = visible_;
            active_ = false;
            draining_ = left_ || right_;
        }
        return result;
    }
private:
    bool left_=false, right_=false, active_=false, draining_=false;
    bool visible_=false, haveDirection_=false;
    std::uint64_t started_=0;
    float anchorX_=0, anchorY_=0;
    float baseFacing_=0, facing_=0, frontage_=20;
};
}
