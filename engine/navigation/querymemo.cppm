module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

export module engine.navigation.querymemo;

// This cache is deliberately independent of the game-facing navigation
// adapter. It stores only a transient result of one occupancy query and has
// no authority over terrain, zones, search costs, parents or the frontier.
export extern "C++" {
namespace navigation {


// Ground group routes repeatedly ask for the same footprint clearance while
// expanding neighbors and validating rays. Occupancy is immutable during one
// synchronous search; discard every verdict when that search ends.
class GroundClearanceQueryMemo {
    struct Entry { std::uint32_t epoch = 0; int diameter = 0, value = 0; bool crusher = false; };
    std::array<std::vector<Entry>, 16> layers_;
    int width_ = 0, height_ = 0;
    std::uint32_t epoch_ = 0;
    bool active_ = false;
public:
    bool enabled = true;
    std::uint64_t evaluations = 0, hits = 0;
    class Scope {
        GroundClearanceQueryMemo& memo_;
    public:
        explicit Scope(GroundClearanceQueryMemo& memo) : memo_(memo) {}
        Scope(const Scope&) = delete;
        ~Scope() { memo_.active_ = false; }
    };
    Scope begin(int width, int height) {
        assert(!active_);
        if (width != width_ || height != height_ || ++epoch_ == 0) {
            for (auto& layer : layers_) layer.clear();
            width_ = width; height_ = height; epoch_ = 1;
        }
        active_ = enabled && width > 0 && height > 0;
        return Scope(*this);
    }
    template<class Compute>
    int evaluate(int x, int y, int layer, int diameter, bool crusher, Compute compute) {
        if (!active_ || x < 0 || y < 0 || x >= width_ || y >= height_ || layer < 0 || layer >= 16) {
            ++evaluations; return compute();
        }
        auto& entries = layers_[layer];
        if (entries.empty()) entries.resize(std::size_t(width_) * height_);
        auto& entry = entries[std::size_t(y) * width_ + x];
        if (entry.epoch == epoch_ && entry.diameter == diameter && entry.crusher == crusher) {
            ++hits; return entry.value;
        }
        ++evaluations;
        const int value = compute();
        entry = {epoch_, diameter, value, crusher};
        return value;
    }
};

} // namespace navigation
} // extern "C++"
