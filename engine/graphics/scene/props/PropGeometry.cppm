module;
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

export module Graphics.Scene.Props.Geometry;

namespace Graphics
{
// Vertex attributes have the GPU's IEEE binary32 representation. Testing the
// exponent avoids an out-of-line CRT classification call for every component.
static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
bool Finite_Prop_Component(float value) noexcept
{
    constexpr std::uint32_t exponent_mask = 0x7f800000u;
    return (std::bit_cast<std::uint32_t>(value) & exponent_mask) != exponent_mask;
}
export struct PropVertex final
{
    std::array<float, 3> position{};
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 2> uv{};
    std::array<float, 2> secondary_uv{};
    std::array<float, 3> normal{0,0,1};
    std::array<float,4> material_ambient{1,1,1,0};
    std::array<float,4> material_diffuse{1,1,1,1};
    std::array<float,4> material_emissive{};
    std::array<float,4> material_specular{0,0,0,1};
    std::array<float,4> secondary_color{};
};
static_assert(sizeof(PropVertex) == 136);

// Builds one material batch without expanding shared source vertices into
// triangle corners. Source indices define identity: equal positions with
// different UVs, normals or colors remain distinct. Begin separates materials.
export class PropBatchBuilder final
{
public:
    bool Begin(std::size_t source_vertex_count)
    {
        if (source_vertex_count > std::numeric_limits<std::uint32_t>::max()/sizeof(PropVertex)) return false;
        m_vertices.clear();
        m_indices.clear();
        m_remap.assign(source_vertex_count,InvalidIndex);
        return true;
    }

    template<typename ExtractVertex>
    bool Append(std::uint32_t source_index,const ExtractVertex& extract)
    {
        if (source_index >= m_remap.size()
            || m_indices.size() >= std::numeric_limits<std::uint32_t>::max()/sizeof(std::uint32_t)) return false;
        auto& index = m_remap[source_index];
        if (index == InvalidIndex) {
            const auto next = static_cast<std::uint32_t>(m_vertices.size());
            m_vertices.push_back(extract(source_index));
            index = next;
        }
        m_indices.push_back(index);
        return true;
    }

    std::span<const PropVertex> Vertices() const noexcept { return m_vertices; }
    std::span<const std::uint32_t> Indices() const noexcept { return m_indices; }

private:
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> m_remap;
    std::vector<PropVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
};

// Indexed material geometry shared by terrain decorations. The input
// topology is preserved, including segment boundaries and overlapping layers.
export class PropGeometry final
{
public:
    bool Assign(std::span<const PropVertex> vertices, std::span<const std::uint32_t> indices)
    {
        constexpr auto maximum_bytes = std::numeric_limits<std::uint32_t>::max();
        if (vertices.size() > maximum_bytes / sizeof(PropVertex)
            || indices.size() > maximum_bytes / sizeof(std::uint32_t)
            || indices.size() % 3 != 0) return false;
        for (const auto &vertex : vertices) {
            for (float value : vertex.position) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.color) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.normal) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.secondary_color) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.uv) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.secondary_uv) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.material_ambient) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.material_diffuse) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.material_emissive) if (!Finite_Prop_Component(value)) return false;
            for (float value : vertex.material_specular) if (!Finite_Prop_Component(value)) return false;
        }
        for (std::uint32_t index : indices) if (index >= vertices.size()) return false;
        m_vertices.assign(vertices.begin(), vertices.end());
        m_indices.assign(indices.begin(), indices.end());
        return true;
    }

    std::span<const PropVertex> Vertices() const noexcept { return m_vertices; }
    std::span<const std::uint32_t> Indices() const noexcept { return m_indices; }

private:
    std::vector<PropVertex> m_vertices;
    std::vector<std::uint32_t> m_indices;
};
}
