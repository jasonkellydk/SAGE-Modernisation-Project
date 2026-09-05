module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

export module Graphics.Scene.Shadows.DirectionalRenderer;
export import Graphics.Scene.Shadows;
export import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Lighting.Environment;
import Graphics.Passes.Shadow;
import Graphics.Resources.Pools.ResourcePool;

namespace Graphics
{
export struct ShadowCasterTag;
export using ShadowCasterHandle = ResourceHandle<ShadowCasterTag>;
// Casters are extracted once with the same geometry, UVs, and opacity as their
// visible material. Imported textures remain owned by the submitting scene
// until Render completes. No gameplay object or asset-format type crosses here.
export class DirectionalShadowRenderer final
{
public:
    DirectionalShadowRenderer() = default;
    DirectionalShadowRenderer(const DirectionalShadowRenderer&) = delete;
    DirectionalShadowRenderer& operator=(const DirectionalShadowRenderer&) = delete;
    ~DirectionalShadowRenderer() { Shutdown(); }
    bool Initialize(Device& device,const std::filesystem::path& shaders)
    {
        if (m_device == &device) return true;
        Shutdown();
        if (!m_renderer.Initialize(device,shaders)) return false;
        m_device = &device;
        return true;
    }

    void Clear_Casters() noexcept
    {
        for (const auto& caster : m_casters)
            if (caster.transient) Destroy_Caster(caster.mesh);
        m_casters.clear();
    }

    ShadowCasterHandle Create_Caster(std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        if (m_device == nullptr) return {};
        const auto mesh = m_renderer.Create_Mesh(vertices,indices);
        if (!mesh.Is_Valid()) return {};
        Mesh geometry;
        geometry.handle = mesh;
        if (!vertices.empty()) {
            geometry.minimum = geometry.maximum = vertices.front().position;
            for (const auto& vertex : vertices) for (std::size_t axis=0;axis<3;++axis) {
                geometry.minimum[axis] = std::min(geometry.minimum[axis],vertex.position[axis]);
                geometry.maximum[axis] = std::max(geometry.maximum[axis],vertex.position[axis]);
            }
        }
        return m_meshes.Create(geometry);
    }

    bool Is_Caster_Valid(ShadowCasterHandle handle) const noexcept
    {
        return m_meshes.Resolve(handle) != nullptr;
    }

    bool Destroy_Caster(ShadowCasterHandle handle) noexcept
    {
        const auto* mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr) return false;
        m_renderer.Destroy_Mesh(mesh->handle);
        return m_meshes.Destroy(handle);
    }

    void Shutdown() noexcept
    {
        Clear_Casters();
        m_meshes.For_Each([&](ShadowCasterHandle,const Mesh& mesh) {
            m_renderer.Destroy_Mesh(mesh.handle);
        });
        m_meshes.Clear();
        if (m_device != nullptr) {
            auto& environment = Get_Environment_Lighting();
            environment.parameters.shadow_options[0] = 0;
            environment.shadow_textures = {};
            m_maps.Shutdown(*m_device);
        }
        m_renderer.Shutdown();
        m_graph = {};
        m_plan = {};
        m_device = nullptr;
    }

    bool Add_Caster(std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices, PropParameters parameters,
        std::span<const RHITextureHandle> textures, const PropStyle& material_style = {})
    {
        const auto mesh = Create_Caster(vertices,indices);
        if (!mesh.Is_Valid()) return false;
        if (!Add_Caster(mesh,parameters,textures,material_style)) {
            Destroy_Caster(mesh);
            return false;
        }
        m_casters.back().transient = true;
        return true;
    }

    // Retained geometry remains valid across frame-list clears. Its owner
    // releases it when geometry changes, before this renderer is destroyed.
    bool Add_Caster(ShadowCasterHandle mesh,PropParameters parameters,
        std::span<const RHITextureHandle> textures,const PropStyle& material_style = {})
    {
        if (!Is_Caster_Valid(mesh) || textures.size() > 4) return false;
        Caster caster;
        caster.mesh = mesh;
        caster.parameters = parameters;
        caster.parameters.shroud = caster.parameters.shroud_only = 0;
        caster.parameters.fog_state = {};
        caster.style = material_style;
        caster.style.blend = RHIBlendMode::Disabled;
        caster.style.source_blend = RHIBlendFactor::One;
        caster.style.destination_blend = RHIBlendFactor::Zero;
        caster.style.depth_test = caster.style.depth_write = true;
        caster.style.depth_comparison = RHIComparison::LessEqual;
        caster.style.color_write_mask = 0;
        caster.style.stencil = {};
        caster.style.wireframe = false;
        caster.style.cull = RHICullMode::None;
        caster.style.depth_bias = 0;
        caster.texture_count = static_cast<std::uint32_t>(textures.size());
        for (std::size_t index=0;index<textures.size();++index) caster.textures[index] = textures[index];
        m_casters.push_back(caster);
        return true;
    }

    bool Render(CommandList& commands,const View& view,const RenderLight& light,
        const ShadowSettings& settings,RHITextureHandle color_target,
        RHITextureHandle depth_target,RHIViewport viewport)
    {
        if (m_device == nullptr || !color_target.Is_Valid() || !depth_target.Is_Valid()) return false;
        ShadowCascades cascades;
        if (!Build_Shadow_Cascades(view,LightHandle(0,1),light,settings,cascades)) return false;
        if (!Prepare_Maps(settings)) return false;
        auto& environment = Get_Environment_Lighting();
        const auto saved = environment;
        environment.parameters.shadow_options[0] = 0;
        environment.parameters.cloud_offset_strength[3] = 0;
        environment.parameters.clip_plane = {};
        const bool rendered = m_plan.Execute(m_graph,commands,
            [&](GraphPassHandle pass,CommandList& list,const PassResources& resources) {
                std::uint32_t cascade = 0;
                while (cascade<cascades.count && m_passes[cascade]!=pass) ++cascade;
                if (cascade == cascades.count) return false;
                if (!list.Set_Depth_Target(resources.Texture(m_maps.Target(cascade)))
                    || !list.Set_Viewport({0,0,settings.map_size,settings.map_size})
                    || !list.Clear_Depth(1)) return false;
                const auto planes = Cascade_Planes(cascades.views[cascade].view_projection);
                for (const auto& caster : m_casters) {
                    auto parameters = caster.parameters;
                    parameters.view_projection = cascades.views[cascade].view_projection.values;
                    const auto* mesh = m_meshes.Resolve(caster.mesh);
                    if (mesh == nullptr) return false;
                    if (!Intersects_Cascade(*mesh,planes)) continue;
                    if (!m_renderer.Draw(list,mesh->handle,caster.style,parameters,
                        std::span(caster.textures.data(),caster.texture_count))) return false;
                }
                return true;
            });
        environment = saved;
        // Restoration is attempted even if a caster submission failed.
        const bool restored = commands.Set_Render_Targets(color_target,depth_target)
            && commands.Set_Viewport(viewport);
        environment.parameters.shadow_options[0] = 0;
        if (!rendered || !restored) return false;
        for (std::uint32_t cascade=0;cascade<cascades.count;++cascade) {
            environment.parameters.shadow_view_projection[cascade] = cascades.views[cascade].view_projection.values;
            environment.parameters.shadow_splits[cascade] = cascades.views[cascade].split_far;
            environment.shadow_textures[cascade] = m_maps.Texture(cascade);
        }
        for (std::size_t column=0;column<4;++column)
            environment.parameters.shadow_view_depth[column] = -view.view_matrix(2,column);
        environment.parameters.shadow_options[0] = static_cast<float>(cascades.count);
        return true;
    }

private:
    struct Mesh final
    {
        PropMeshHandle handle{};
        std::array<float,3> minimum{};
        std::array<float,3> maximum{};
    };

    static std::array<std::array<float,4>,6> Cascade_Planes(const Matrix4x4& matrix) noexcept
    {
        std::array<std::array<float,4>,6> planes{};
        for (std::size_t column=0;column<4;++column) {
            planes[0][column] = matrix(3,column) + matrix(0,column);
            planes[1][column] = matrix(3,column) - matrix(0,column);
            planes[2][column] = matrix(3,column) + matrix(1,column);
            planes[3][column] = matrix(3,column) - matrix(1,column);
            planes[4][column] = matrix(2,column);
            planes[5][column] = matrix(3,column) - matrix(2,column);
        }
        return planes;
    }

    static bool Intersects_Cascade(const Mesh& mesh,
        const std::array<std::array<float,4>,6>& planes) noexcept
    {
        // Test the padded light volume, never the main camera frustum. Only
        // reject when the entire box is outside one plane. This also retains
        // geometry crossing a volume with every vertex outside that volume.
        for (const auto& plane : planes) {
            float distance = plane[3];
            float magnitude = std::abs(plane[3]);
            for (std::size_t axis=0;axis<3;++axis) {
                const float support = plane[axis] >= 0 ? mesh.maximum[axis] : mesh.minimum[axis];
                const float term = plane[axis] * support;
                distance += term;
                magnitude += std::abs(term);
            }
            // Expand at the boundary to account for floating-point rounding.
            if (distance < -0.00001f * (1.0f + magnitude)) return false;
        }
        return true;
    }

    struct Caster final
    {
        ShadowCasterHandle mesh{};
        bool transient = false;
        PropParameters parameters;
        PropStyle style;
        std::array<RHITextureHandle,4> textures{};
        std::uint32_t texture_count = 0;
    };

    bool Prepare_Maps(const ShadowSettings& settings)
    {
        if (m_maps.Count() == settings.cascade_count && m_maps.Map_Size() == settings.map_size && m_plan.Is_Valid()) return true;
        Get_Environment_Lighting().parameters.shadow_options[0] = 0;
        Get_Environment_Lighting().shadow_textures = {};
        m_maps.Shutdown(*m_device);
        m_graph = {};
        m_plan = {};
        if (!m_maps.Initialize(*m_device,m_graph,settings.cascade_count,settings.map_size)
            || !ShadowPass::Add_Cascades_To_Graph(m_graph,m_maps,m_passes,20)) return false;
        for (std::uint32_t cascade=0;cascade<settings.cascade_count;++cascade)
            m_bindings[cascade] = GraphResourceBinding::Texture(m_maps.Target(cascade),m_maps.Texture(cascade));
        return m_plan.Compile(m_graph,std::span(m_bindings.data(),settings.cascade_count));
    }

    Device* m_device = nullptr;
    PropRenderer m_renderer;
    ResourcePool<Mesh,ShadowCasterHandle> m_meshes;
    ShadowMapResources m_maps;
    RenderGraph m_graph;
    ExecutionPlan m_plan;
    std::array<GraphPassHandle,4> m_passes{};
    std::array<GraphResourceBinding,4> m_bindings{};
    std::vector<Caster> m_casters;
};

export DirectionalShadowRenderer& Get_Directional_Shadow_Renderer() noexcept
{
    static DirectionalShadowRenderer renderer;
    return renderer;
}
}
