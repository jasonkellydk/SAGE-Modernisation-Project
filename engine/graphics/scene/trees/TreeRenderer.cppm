module;
#include <array>
#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

export module Graphics.Scene.Trees.Renderer;
import engine.profiling;
export import Graphics.Scene.Trees.Geometry;
export import Graphics.RHI;
import Graphics.Resources.Pools.ResourcePool;
import Graphics.Shaders.Library;
import Graphics.Scene.Lighting.Environment;
import Graphics.Scene.Shadows.DirectionalRenderer;

namespace Graphics
{
export struct TreeMeshTag;
export using TreeMeshHandle = ResourceHandle<TreeMeshTag>;

export struct TreeParameters final
{
    std::array<float,16> view_projection{};
    std::array<std::array<float,4>,10> sway{};
    std::array<float,4> shroud_projection{};
    std::array<float,4> options{0,0.5f,1,0}; // Shroud, cutoff, overbright.
};
static_assert(sizeof(TreeParameters) == 256);

struct TreeMesh final
{
    TreeGeometry geometry;
    RHIBufferHandle vertices{};
    RHIBufferHandle indices{};
};

// Owns geometry and GPU resources across map and device lifetimes. CPU geometry
// survives a device reset; upload is deferred until the next draw on that device.
export class TreeRenderer final
{
public:
    TreeRenderer() = default;
    TreeRenderer(const TreeRenderer &) = delete;
    TreeRenderer &operator=(const TreeRenderer &) = delete;
    ~TreeRenderer() { Shutdown(); }

    bool Initialize(Device &device, const std::filesystem::path &directory)
    {
        if (m_device == &device && m_constants.Is_Valid()) return true;
        Shutdown();
        m_shaders = ShaderLibrary{};
        ShaderPrecompiledDesc shader;
        shader.program.vertex_shader = 13;
        shader.program.fragment_shader = 13;
        shader.program.source_key = 0x5452454553484431ull;
        shader.program.stages = ShaderStageMask::Vertex | ShaderStageMask::Pixel;
        shader.vertex_path = directory / "tree.vso";
        shader.fragment_path = directory / "tree.pso";
        m_shader = m_shaders.Load_Precompiled(shader);
        if (!m_shaders.Is_Loaded(m_shader)) return false;
        m_device = &device;
        m_constants = device.Create_Buffer({sizeof(TreeParameters), RHIBufferUsage::Constant});
        if (!m_constants.Is_Valid() || !m_environment.Initialize(device)) { Shutdown(); return false; }
        return true;
    }

    void Shutdown() noexcept
    {
        Release_Shadow_Geometry();
        if (m_device != nullptr) {
            m_environment.Shutdown(*m_device);
            m_meshes.For_Each([&](TreeMeshHandle handle, const TreeMesh &) {
                Release_GPU(*m_meshes.Resolve(handle));
            });
            if (m_pipeline.Is_Valid()) m_device->Destroy_Pipeline(m_pipeline);
            if (m_constants.Is_Valid()) m_device->Destroy_Buffer(m_constants);
        }
        m_pipeline = {};
        m_constants = {};
        m_device = nullptr;
    }

    TreeMeshHandle Create_Mesh(std::span<const TreeVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        TreeMesh mesh;
        if (!mesh.geometry.Assign(vertices, indices)) return {};
        return m_meshes.Create(std::move(mesh));
    }

    bool Update_Mesh(TreeMeshHandle handle, std::span<const TreeVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        TreeMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr) return false;
        const auto old_vertices=mesh->geometry.Vertices();
        const auto old_indices=mesh->geometry.Indices();
        if (old_vertices.size()==vertices.size() && old_indices.size()==indices.size()
            && (vertices.empty() || std::memcmp(old_vertices.data(),vertices.data(),vertices.size_bytes())==0)
            && (indices.empty() || std::memcmp(old_indices.data(),indices.data(),indices.size_bytes())==0)) return true;
        if (!mesh->geometry.Assign(vertices,indices)) return false;
        Release_GPU(*mesh);
        return true;
    }

    bool Destroy_Mesh(TreeMeshHandle handle) noexcept
    {
        TreeMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr) return false;
        Release_GPU(*mesh);
        return m_meshes.Destroy(handle);
    }

    // Use the same height-relative deformation and atlas alpha as the visible
    // tree pass. Caster geometry may include trees outside the camera view.
    bool Add_Shadow_Caster(DirectionalShadowRenderer& shadows,
        std::span<const TreeVertex> vertices, std::span<const std::uint32_t> indices,
        const TreeParameters& parameters, RHITextureHandle texture)
    {
        if (indices.empty()) return true;
        if (!texture.Is_Valid()) return false;
        auto& renderer = shadows.Caster_Renderer();
        if (m_shadow_renderer != &renderer) {
            Release_Shadow_Geometry();
            m_shadow_renderer = &renderer;
        }
        const bool same = m_shadow_mesh.Is_Valid()
            && m_shadow_geometry.Vertices().size() == vertices.size()
            && m_shadow_geometry.Indices().size() == indices.size()
            && std::memcmp(m_shadow_geometry.Vertices().data(),vertices.data(),vertices.size_bytes()) == 0
            && std::memcmp(m_shadow_geometry.Indices().data(),indices.data(),indices.size_bytes()) == 0;
        if (!same) {
            if (!m_shadow_geometry.Assign(vertices,indices)) return false;
            std::map<std::pair<unsigned,std::uint32_t>,std::uint32_t> bones;
            m_shadow_bones.clear();
            auto& bind_pose = m_shadow_bind_pose;
            bind_pose.clear();
            bind_pose.reserve(vertices.size());
            for (const auto& source : vertices) {
                const auto sway_index = static_cast<unsigned>(std::clamp(source.sway[0],1.0f,10.0f))-1;
                const auto key = std::pair{sway_index,std::bit_cast<std::uint32_t>(source.sway[2])};
                const auto [entry,inserted] = bones.try_emplace(key,static_cast<std::uint32_t>(bones.size()));
                if (inserted) m_shadow_bones.push_back({sway_index,source.sway[2]});
                PropVertex vertex;
                vertex.position = source.position;
                vertex.position[2] -= source.sway[2];
                vertex.uv = source.uv;
                vertex.bone_index = static_cast<float>(entry->second);
                bind_pose.push_back(vertex);
            }
            if (m_shadow_bones.size() > 65536)
                return Add_CPU_Shadow_Caster(shadows,vertices,indices,parameters,texture);
            // Keep CPU allocation capacity when moving/toppling trees change
            // the forest geometry. Deferred shadow draws may still own the old
            // mesh, in which case Update_Mesh refuses mutation and we publish
            // a replacement with its own lifetime.
            if (!m_shadow_mesh.Is_Valid() || !renderer.Update_Mesh(m_shadow_mesh,bind_pose,indices)) {
                const auto next = renderer.Create_Mesh(bind_pose,indices);
                if (!next.Is_Valid()) return false;
                if (m_shadow_mesh.Is_Valid()) renderer.Destroy_Mesh(m_shadow_mesh);
                m_shadow_mesh = next;
            }
            m_shadow_pose.resize(m_shadow_bones.size());
        }
        for (std::size_t i=0; i<m_shadow_bones.size(); ++i) {
            const auto& bone = m_shadow_bones[i];
            const auto& sway = parameters.sway[bone.sway];
            m_shadow_pose[i] = {1,0,sway[0],0, 0,1,sway[1],0, 0,0,1+sway[2],bone.base};
        }
        PropParameters material;
        material.alpha_cutoff = parameters.options[1];
        const auto skin = m_shadow_skin.Update(renderer.Instances().Palettes(),m_shadow_pose.size(),
            [&](std::size_t bone) -> const auto& { return m_shadow_pose[bone]; });
        const auto instance = m_shadow_instance.Update(renderer.Instances(),material,skin);
        PropStyle style;
        style.samplers[0].address.fill(RHISamplerAddress::Clamp);
        return shadows.Add_Caster(renderer,m_shadow_mesh,material,std::array{texture},style,instance);
    }

private:
    void Release_Shadow_Geometry() noexcept
    {
        m_shadow_instance.Reset();
        m_shadow_skin.Reset();
        if (m_shadow_renderer && m_shadow_mesh.Is_Valid()) m_shadow_renderer->Destroy_Mesh(m_shadow_mesh);
        m_shadow_mesh = {};
        m_shadow_renderer = nullptr;
        m_shadow_geometry = {};
        m_shadow_bind_pose = {};
        m_shadow_bones.clear();
        m_shadow_pose.clear();
    }

    // Preserve support for inputs exceeding the GPU palette address range.
    bool Add_CPU_Shadow_Caster(DirectionalShadowRenderer& shadows,
        std::span<const TreeVertex> vertices, std::span<const std::uint32_t> indices,
        const TreeParameters& parameters, RHITextureHandle texture)
    {
        if (indices.empty()) return true;
        if (!texture.Is_Valid()) return false;
        std::vector<PropVertex> casters;
        casters.reserve(vertices.size());
        for (const auto& source : vertices) {
            PropVertex vertex;
            vertex.position = source.position;
            vertex.uv = source.uv;
            const auto sway_index = static_cast<unsigned>(std::clamp(source.sway[0],1.0f,10.0f))-1;
            const float height = source.position[2]-source.sway[2];
            for (unsigned axis=0;axis<3;++axis)
                vertex.position[axis] += height*parameters.sway[sway_index][axis];
            casters.push_back(vertex);
        }
        PropParameters material;
        material.alpha_cutoff = parameters.options[1];
        PropStyle style;
        style.samplers[0].address.fill(RHISamplerAddress::Clamp);
        const std::array textures{texture};
        return shadows.Add_Caster(casters,indices,material,textures,style);
    }

public:
    bool Draw(CommandList &commands, TreeMeshHandle handle,
        const TreeParameters &parameters, std::span<const RHITextureHandle> textures)
    {
        engine::profiling::Scope profile_scope_231("Graphics.Trees.Draw");
        TreeMesh *mesh = m_meshes.Resolve(handle);
        if (m_device == nullptr || mesh == nullptr || textures.size() != 2) return false;
        if (mesh->geometry.Indices().empty()) return true;
        const auto has_texture = [&](std::size_t slot) {
            return slot < textures.size() && textures[slot].Is_Valid();
        };
        if (!has_texture(0) || (parameters.options[0] > 0.5f && !has_texture(1))) return false;
        if (!Upload(*mesh)) return false;
        const RHIPipelineHandle pipeline = Pipeline();
        if (!pipeline.Is_Valid() || !m_device->Update_Buffer(m_constants, 0,
            std::as_bytes(std::span(&parameters, 1)))) return false;
        std::array<RHIBindlessResource, 5> bindings{};
        bindings[0].type = RHIResourceType::Material;
        bindings[0].buffer = m_constants;
        std::size_t count = 1;
        for (std::size_t slot = 0; slot < textures.size(); ++slot) {
            if (!textures[slot].Is_Valid()) continue;
            bindings[count].type = RHIResourceType::Texture;
            bindings[count].index = ResourceIndex{static_cast<std::uint32_t>(slot), 1};
            bindings[count++].texture = textures[slot];
        }
        return commands.Bind_Pipeline(pipeline)
            && commands.Set_Bindless_Resources(std::span(bindings.data(), count))
            && m_environment.Bind(*m_device, commands)
            && commands.Set_Vertex_Buffer(0, mesh->vertices, sizeof(TreeVertex), 0)
            && commands.Set_Index_Buffer(mesh->indices, RHIIndexFormat::UInt32, 0)
            && commands.Draw_Indexed(static_cast<std::uint32_t>(mesh->geometry.Indices().size()), 0);
    }

private:
    void Release_GPU(TreeMesh &mesh) noexcept
    {
        if (m_device != nullptr) {
            if (mesh.vertices.Is_Valid()) m_device->Destroy_Buffer(mesh.vertices);
            if (mesh.indices.Is_Valid()) m_device->Destroy_Buffer(mesh.indices);
        }
        mesh.vertices = {};
        mesh.indices = {};
    }

    bool Upload(TreeMesh &mesh)
    {
        if (mesh.vertices.Is_Valid() && mesh.indices.Is_Valid()) return true;
        const auto vertices = std::as_bytes(mesh.geometry.Vertices());
        const auto indices = std::as_bytes(mesh.geometry.Indices());
        mesh.vertices = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(vertices.size()), RHIBufferUsage::Vertex, sizeof(TreeVertex)}, vertices);
        mesh.indices = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(indices.size()), RHIBufferUsage::Index, sizeof(std::uint32_t)}, indices);
        if (!mesh.vertices.Is_Valid() || !mesh.indices.Is_Valid()) {
            Release_GPU(mesh);
            return false;
        }
        return true;
    }

    RHIPipelineHandle Pipeline()
    {
        if (m_pipeline.Is_Valid()) return m_pipeline;
        RHIPipeline description;
        description.vertex_format = RHIVertexFormat::Position3Color4UV2UV2Normal3;
        description.blend_mode = RHIBlendMode::Disabled;
        description.depth_write = true;
        description.cull_mode = RHICullMode::None;
        description.color_write_mask = 15;
        description.sampler_count = 16;
        for (unsigned i=0;i<2;++i) description.samplers[i].address.fill(RHISamplerAddress::Clamp);
        m_pipeline = m_device->Create_Pipeline(description,
            {m_shaders.Bytecode(m_shader, ShaderStage::Vertex)}, {m_shaders.Bytecode(m_shader, ShaderStage::Pixel)});
        return m_pipeline;
    }

    EnvironmentLightingBinding m_environment;
    struct ShadowBone { unsigned sway; float base; };
    PropRenderer* m_shadow_renderer = nullptr;
    PropMeshHandle m_shadow_mesh{};
    PropInstanceOwner m_shadow_instance;
    PropSkinOwner m_shadow_skin;
    TreeGeometry m_shadow_geometry;
    std::vector<PropVertex> m_shadow_bind_pose;
    std::vector<ShadowBone> m_shadow_bones;
    std::vector<PropBoneTransform> m_shadow_pose;
    Device *m_device = nullptr;
    ShaderLibrary m_shaders;
    ShaderHandle m_shader{};
    RHIBufferHandle m_constants{};
    ResourcePool<TreeMesh, TreeMeshHandle> m_meshes;
    RHIPipelineHandle m_pipeline{};
};

namespace { TreeRenderer g_tree_renderer; }
export TreeRenderer &Get_Tree_Renderer() noexcept { return g_tree_renderer; }
}
