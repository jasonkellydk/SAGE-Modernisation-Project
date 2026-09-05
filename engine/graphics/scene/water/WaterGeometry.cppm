module;
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module Graphics.Scene.Water.Geometry;

namespace Graphics
{
export struct WaterVertex final
{
    std::array<float, 3> position{};
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 2> uv{};
    std::array<float, 2> secondary_uv{};
    std::array<float, 3> normal{0,0,1};
};
static_assert(sizeof(WaterVertex) == 56);

// Indexed, prelit triangle geometry shared by terrain decorations. The input
// topology is preserved, including segment boundaries and overlapping layers.
export std::vector<std::uint32_t> Expand_Water_Strip(std::span<const std::uint32_t> strip)
{
    std::vector<std::uint32_t> triangles;
    if (strip.size() < 3) return triangles;
    triangles.reserve((strip.size()-2)*3);
    for (std::size_t i=2;i<strip.size();++i) {
        const auto a = strip[i-2], b = strip[i-1], c = strip[i];
        if (a==b || b==c || a==c) continue;
        if (i%2==0) triangles.insert(triangles.end(),{a,b,c});
        else triangles.insert(triangles.end(),{b,a,c});
    }
    return triangles;
}

export class WaterGeometry final
{
public:
    bool Assign(std::span<const WaterVertex> vertices, std::span<const std::uint32_t> indices)
    {
        constexpr auto maximum_bytes = std::numeric_limits<std::uint32_t>::max();
        if (vertices.size() > maximum_bytes / sizeof(WaterVertex)
            || indices.size() > maximum_bytes / sizeof(std::uint32_t)
            || indices.size() % 3 != 0) return false;
        for (const auto &vertex : vertices) {
            for (float value : vertex.position) if (!std::isfinite(value)) return false;
            for (float value : vertex.color) if (!std::isfinite(value)) return false;
            for (float value : vertex.normal) if (!std::isfinite(value)) return false;
            for (float value : vertex.uv) if (!std::isfinite(value)) return false;
        }
        for (std::uint32_t index : indices) if (index >= vertices.size()) return false;
        m_vertices.assign(vertices.begin(), vertices.end());
        m_indices.assign(indices.begin(), indices.end());
        return true;
    }

    std::span<const WaterVertex> Vertices() const noexcept { return m_vertices; }
    std::span<const std::uint32_t> Indices() const noexcept { return m_indices; }

private:
    std::vector<WaterVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
};
}
