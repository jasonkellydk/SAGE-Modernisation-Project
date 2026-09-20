#pragma once
#include <cmath>

namespace navigation {
struct ArrivalFacing {
    float angle=0;
    bool requested=false, turning=false;
    void set(float value) { angle=value; requested=true; turning=false; }
    void cancel() { angle=0; requested=false; turning=false; }
    bool update(bool positionReached, float currentAngle) {
        if (!requested || (!positionReached && !turning)) return false;
        constexpr float twoPi=6.2831853071795864769f;
        if (std::abs(std::remainder(angle-currentAngle,twoPi))<=0.03f) {
            cancel();
            return false;
        }
        turning=true;
        return true;
    }
};
}
