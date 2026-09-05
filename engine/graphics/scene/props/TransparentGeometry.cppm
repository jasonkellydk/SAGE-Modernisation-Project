module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

export module Graphics.Scene.Props.TransparentGeometry;
export import Graphics.Scene.Props.Renderer;

namespace Graphics {
// Deferred triangle ordering across material batches. Textures remain owned by
// the caller until Flush or Clear; geometry is copied when it is submitted.
export class TransparentGeometry final {
public:
    bool Submit(PropRenderer& renderer, std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices, const PropStyle& style,
        const PropParameters& parameters, std::span<const RHITextureHandle> textures,
        const std::array<float,4>& camera_depth)
    {
        if (textures.size()>4) return false;
        for (float value : camera_depth) if (!std::isfinite(value)) return false;
        const auto mesh=renderer.Create_Mesh(vertices,indices);
        if (!mesh.Is_Valid()) return false;
        const auto batch_index=m_batches.size();
        Batch batch{mesh,style,parameters};
        std::copy(textures.begin(),textures.end(),batch.textures.begin());
        m_batches.push_back(batch);
        for (std::size_t index=0; index<indices.size(); index+=3) {
            float depth=0;
            for (std::size_t corner=0; corner<3; ++corner) {
                const auto& position=vertices[indices[index+corner]].position;
                depth+=position[0]*camera_depth[0]+position[1]*camera_depth[1]
                    +position[2]*camera_depth[2]+camera_depth[3];
            }
            m_triangles.push_back({batch_index,static_cast<std::uint32_t>(index),depth/3});
        }
        return true;
    }

    bool Flush(PropRenderer& renderer, CommandList& commands)
    {
        // Camera space looks down -Z. Stable ties retain submission order.
        std::stable_sort(m_triangles.begin(),m_triangles.end(),
            [](const Triangle& left,const Triangle& right) { return left.depth<right.depth; });
        bool success=true;
        for (std::size_t i=0; i<m_triangles.size();) {
            const auto triangle=m_triangles[i];
            const auto& batch=m_batches[triangle.batch];
            std::uint32_t count=3;
            ++i;
            while (i<m_triangles.size() && m_triangles[i].batch==triangle.batch
                && m_triangles[i].first==triangle.first+count) { count+=3; ++i; }
            if (!renderer.Draw_Range(commands,batch.mesh,batch.style,batch.parameters,
                batch.textures,triangle.first,count)) success=false;
        }
        Clear(renderer);
        return success;
    }

    void Clear(PropRenderer& renderer) noexcept
    {
        for (const auto& batch : m_batches) renderer.Destroy_Mesh(batch.mesh);
        m_batches.clear(); m_triangles.clear();
    }
    bool Empty() const noexcept { return m_triangles.empty(); }
private:
    struct Batch {
        PropMeshHandle mesh;
        PropStyle style;
        PropParameters parameters;
        std::array<RHITextureHandle,4> textures{};
    };
    struct Triangle { std::size_t batch; std::uint32_t first; float depth; };
    std::vector<Batch> m_batches;
    std::vector<Triangle> m_triangles;
};
}
