module;
#include <cstdint>
#include <limits>

export module engine.navigation.spatial.query.visit_epoch;

export namespace navigation::spatial {
// Zero denotes unvisited. Clear every retained mark before reusing an epoch.
class VisitEpoch {
    std::uint32_t value_;
public:
    explicit VisitEpoch(std::uint32_t initial=1) : value_(initial) {}
    template<class Reset>
    std::uint32_t next(Reset reset) {
        if (value_==std::numeric_limits<std::uint32_t>::max()) {
            reset();
            value_=1;
        } else ++value_;
        return value_;
    }
};
}
