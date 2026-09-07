module;
#include <algorithm>
#include <array>
#include <span>
#include <vector>
export module Graphics.Scene.Props.MaterialPassQueue;
export import Graphics.Scene.Props.Renderer;
namespace Graphics {
// Procedural passes whose base geometry is omitted execute after the opaque
// depth pass. Retain submission order; these are not transparency-sorted draws.
export class MaterialPassQueue final {
public:
    MaterialPassQueue()=default;
    MaterialPassQueue(const MaterialPassQueue&)=delete;
    MaterialPassQueue& operator=(const MaterialPassQueue&)=delete;
    bool Submit(PropRenderer& renderer,std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices,const PropStyle& style,
        const PropParameters& parameters,std::span<const RHITextureHandle> textures)
    {
        const auto mesh=renderer.Create_Mesh(vertices,indices);
        if (!mesh.Is_Valid()) return false;
        const bool submitted = Submit(renderer,mesh,style,parameters,textures);
        renderer.Destroy_Mesh(mesh);
        return submitted;
    }
    bool Submit(PropRenderer& renderer,PropMeshHandle mesh,const PropStyle& style,
        const PropParameters& parameters,std::span<const RHITextureHandle> textures)
    {
        if (textures.size()>PropTextureCount || (m_renderer && m_renderer!=&renderer)
            || !renderer.Retain_Mesh(mesh)) return false;
        Batch batch{mesh,style,parameters};
        std::copy(textures.begin(),textures.end(),batch.textures.begin());
        m_batches.push_back(batch); m_renderer=&renderer;
        return true;
    }
    bool Flush(CommandList& commands)
    {
        bool success=true;
        for (const auto& batch : m_batches)
            if (!m_renderer->Draw(commands,batch.mesh,batch.style,batch.parameters,batch.textures)) success=false;
        Clear();
        return success;
    }
    void Begin_Group() { m_groups.push_back(m_batches.size()); }
    bool Flush_Groups_Reversed(CommandList& commands)
    {
        if (m_groups.empty()) return Flush(commands);
        bool success = true;
        std::size_t end = m_batches.size();
        for (auto group = m_groups.rbegin(); group != m_groups.rend(); ++group) {
            for (auto index = *group; index < end; ++index) {
                const auto& batch = m_batches[index];
                if (!m_renderer->Draw(commands,batch.mesh,batch.style,batch.parameters,batch.textures)) success = false;
            }
            end = *group;
        }
        // Batches submitted before the first explicit group form the oldest group.
        for (std::size_t index=0; index<end; ++index) {
            const auto& batch = m_batches[index];
            if (!m_renderer->Draw(commands,batch.mesh,batch.style,batch.parameters,batch.textures)) success = false;
        }
        Clear();
        return success;
    }
    void Clear() noexcept
    {
        for (const auto& batch : m_batches) m_renderer->Destroy_Mesh(batch.mesh);
        m_batches.clear(); m_groups.clear(); m_renderer=nullptr;
    }
    bool Empty() const noexcept { return m_batches.empty(); }
private:
    struct Batch {
        PropMeshHandle mesh;
        PropStyle style;
        PropParameters parameters;
        std::array<RHITextureHandle,PropTextureCount> textures{};
    };
    // Explicitly clear at frame cancellation or renderer shutdown. Texture
    // ownership remains with the caller until this queue has flushed/cleared.
    PropRenderer* m_renderer=nullptr;
    std::vector<Batch> m_batches;
    std::vector<std::size_t> m_groups;
};
}
