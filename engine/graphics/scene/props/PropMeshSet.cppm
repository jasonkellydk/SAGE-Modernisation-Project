module;
#include <cstddef>
#include <span>
#include <vector>
export module Graphics.Scene.Props.MeshSet;
export import Graphics.Scene.Props.Renderer;

namespace Graphics {
// A source owner keeps one version of each material batch. Compare actual
// contents while adapters still expose mutable arrays without revisions.
// Neither pointer identity nor a frame number proves that geometry is unchanged.
export class PropMeshSet final {
public:
    PropMeshSet() = default;
    PropMeshSet(const PropMeshSet&) = delete;
    PropMeshSet& operator=(const PropMeshSet&) = delete;
    ~PropMeshSet() { Clear(); }

    PropMeshHandle Synchronize(PropRenderer& renderer, std::size_t batch,
        std::span<const PropVertex> vertices, std::span<const std::uint32_t> indices)
    {
        if (m_renderer != nullptr && m_renderer != &renderer) return {};
        if (batch >= m_meshes.size()) m_meshes.resize(batch+1);
        const auto old = m_meshes[batch];
        const auto* geometry = renderer.Mesh_Geometry(old);
        if (geometry != nullptr && geometry->Matches(vertices,indices)) return old;
        const auto replacement = renderer.Create_Mesh(vertices,indices);
        if (!replacement.Is_Valid()) return {};
        m_renderer = &renderer;
        m_meshes[batch] = replacement;
        if (old.Is_Valid()) renderer.Destroy_Mesh(old);
        return replacement;
    }

    void Clear() noexcept
    {
        if (m_renderer != nullptr)
            for (const auto mesh : m_meshes) m_renderer->Destroy_Mesh(mesh);
        m_meshes.clear();
        m_renderer = nullptr;
    }
private:
    // The source owner releases its set before the renderer is destroyed.
    PropRenderer* m_renderer = nullptr;
    std::vector<PropMeshHandle> m_meshes;
};
}
