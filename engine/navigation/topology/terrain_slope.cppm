module;
#include <algorithm>
#include <array>

export module engine.navigation.topology.terrain_slope;

export namespace navigation {
inline bool terrainCellIsCliff(const std::array<float, 4>& heights) {
    const auto [lowest, highest] = std::minmax_element(heights.begin(), heights.end());
    return *highest - *lowest > 9.8f;
}
}
