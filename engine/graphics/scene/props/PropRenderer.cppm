module;
#include "../../profiling/Tracy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <utility>
#include <vector>

export module Graphics.Scene.Props.Renderer;
export import Graphics.Scene.Props.Geometry;
export import Graphics.Scene.Props.Surface;
export import Graphics.RHI;
import Graphics.Scene.DrawParameters;
import Graphics.Resources.Pools.ResourcePool;
import Graphics.Shaders.Library;
import Graphics.Scene.Lighting.Environment;

namespace Graphics
{
export struct PropMeshTag;
export using PropMeshHandle = ResourceHandle<PropMeshTag>;

export struct PropParameters final
{
    std::array<float,16> view_projection{};
    std::array<float,4> shroud_projection{};
    std::array<float,4> fog_color{};
    std::array<float,4> fog_state{};
    std::array<float,4> camera_position{};
    float textured = 1;
    float secondary_texture = 0;
    float alpha_cutoff = 0;
    float primary_gradient = 1;
    float detail_color = 0;
    float detail_alpha = 0;
    float secondary_gradient = 0;
    float shroud = 0;
    float shroud_only = 0;
    float opacity = 1;
    float texture_luminance = 0;
    float normal_in_world_space = 0;
    std::array<float,4> scene_ambient{};
    std::array<std::array<float,4>,4> light_direction{};
    std::array<std::array<float,4>,4> light_diffuse{};
    std::array<std::array<float,4>,4> light_specular{};
    std::array<float,16> view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::array<std::array<float,16>,2> uv_transform{{
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};
    std::array<float,4> uv_sources{};
    std::array<float,4> bump_matrix{};
    // World-space local lights. Position.w: 0 directional, 1 point, 2 spot.
    std::array<std::array<float,4>,4> light_position{};
    std::array<std::array<float,4>,4> light_attenuation{};
    std::array<std::array<float,4>,4> light_ambient{};
    std::array<std::array<float,4>,4> light_spot{};
    // Affine instance transform. Geometry stays in its owner's local space.
    std::array<float,16> world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    PropSurfaceParameters surface{};
};
static_assert(sizeof(PropParameters) == 992);

export struct PropStyle final
{
    RHIBlendMode blend = RHIBlendMode::Alpha;
    bool depth_write = true;
    bool depth_test = true;
    RHIBlendFactor source_blend = RHIBlendFactor::One;
    RHIBlendFactor destination_blend = RHIBlendFactor::Zero;
    std::array<RHISamplerDescription,2> samplers{};
    RHIComparison depth_comparison = RHIComparison::LessEqual;
    RHICullMode cull = RHICullMode::None;
    bool front_counter_clockwise = false;
    std::uint8_t color_write_mask = 15;
    RHIStencilDescription stencil{};
    std::int32_t depth_bias=0;
    bool wireframe=false;
    bool operator==(const PropStyle &) const = default;
};

export PropStyle Resolve_Prop_Style(PropStyle authored, const SceneDrawParameters& scene) noexcept
{
    authored.color_write_mask &= scene.color_write_mask;
    authored.depth_bias = scene.depth_bias;
    authored.wireframe = scene.wireframe;
    authored.stencil = scene.stencil;
    return authored;
}

struct PropMesh final
{
    PropGeometry geometry;
    RHIBufferHandle vertices{};
    RHIBufferHandle indices{};
    std::size_t references = 1;
};

struct PropPipeline final
{
    PropStyle style;
    RHIPipelineHandle handle;
};

// Owns geometry and GPU resources across map and device lifetimes. CPU geometry
// survives a device reset; upload is deferred until the next draw on that device.
export class PropRenderer final
{
public:
    PropRenderer() = default;
    PropRenderer(const PropRenderer &) = delete;
    PropRenderer &operator=(const PropRenderer &) = delete;
    ~PropRenderer() { Shutdown(); }

    bool Initialize(Device &device, const std::filesystem::path &directory)
    {
        if (m_device == &device && m_constants.Is_Valid()) return true;
        Shutdown();
        m_shaders = ShaderLibrary{};
        ShaderPrecompiledDesc shader;
        shader.program.vertex_shader = 20;
        shader.program.fragment_shader = 20;
        shader.program.source_key = 0x50524F504D415431ull;
        shader.program.stages = ShaderStageMask::Vertex | ShaderStageMask::Pixel;
        shader.vertex_path = directory / "prop.vso";
        shader.fragment_path = directory / "prop.pso";
        m_shader = m_shaders.Load_Precompiled(shader);
        if (!m_shaders.Is_Loaded(m_shader)) return false;
        m_device = &device;
        m_constants = device.Create_Buffer({sizeof(PropParameters), RHIBufferUsage::Constant});
        if (!m_constants.Is_Valid() || !m_environment.Initialize(device)) { Shutdown(); return false; }
        return true;
    }

    void Shutdown() noexcept
    {
        if (m_device != nullptr) {
            m_environment.Shutdown(*m_device);
            m_meshes.For_Each([&](PropMeshHandle handle, const PropMesh &) {
                Release_GPU(*m_meshes.Resolve(handle));
            });
            for (const auto &pipeline : m_pipelines) m_device->Destroy_Pipeline(pipeline.handle);
            if (m_constants.Is_Valid()) m_device->Destroy_Buffer(m_constants);
        }
        m_pipelines.clear();
        m_constants = {};
        m_device = nullptr;
    }

    PropMeshHandle Create_Mesh(std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        PropMesh mesh;
        if (!mesh.geometry.Assign(vertices, indices)) return {};
        return m_meshes.Create(std::move(mesh));
    }

    bool Update_Mesh(PropMeshHandle handle, std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        PropMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr || mesh->references != 1 || !mesh->geometry.Assign(vertices, indices)) return false;
        Release_GPU(*mesh);
        return true;
    }

    bool Destroy_Mesh(PropMeshHandle handle) noexcept
    {
        PropMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr) return false;
        if (mesh->references > 1) { --mesh->references; return true; }
        Release_GPU(*mesh);
        return m_meshes.Destroy(handle);
    }

    bool Append_Mesh(PropMeshHandle handle, std::span<const PropVertex> vertices,
        std::span<const std::uint32_t> indices)
    {
        PropMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr || mesh->references != 1 || !mesh->geometry.Append(vertices, indices)) return false;
        Release_GPU(*mesh);
        return true;
    }

    // Deferred consumers own a reference until they draw or cancel. Shared
    // geometry cannot be mutated; publish a replacement version instead.
    bool Retain_Mesh(PropMeshHandle handle) noexcept
    {
        auto* mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr || mesh->references == std::numeric_limits<std::size_t>::max()) return false;
        ++mesh->references;
        return true;
    }

    const PropGeometry* Mesh_Geometry(PropMeshHandle handle) const noexcept
    {
        const auto* mesh = m_meshes.Resolve(handle);
        return mesh == nullptr ? nullptr : &mesh->geometry;
    }

    bool Draw(CommandList &commands, PropMeshHandle handle, const PropStyle &style,
        const PropParameters &parameters, std::span<const RHITextureHandle> textures)
    {
        const PropMesh *mesh = m_meshes.Resolve(handle);
        if (mesh == nullptr) return false;
        return Draw_Range(commands, handle, style, parameters, textures, 0,
            static_cast<std::uint32_t>(mesh->geometry.Indices().size()));
    }

    bool Draw_Range(CommandList &commands, PropMeshHandle handle, const PropStyle &style,
        const PropParameters &parameters, std::span<const RHITextureHandle> textures,
        std::uint32_t first_index, std::uint32_t index_count)
    {
        GRAPHICS_PROFILE_SCOPE("Graphics.Props.Draw");
        PropMesh *mesh = m_meshes.Resolve(handle);
        if (m_device == nullptr || mesh == nullptr || textures.size() > PropTextureCount) return false;
        const auto size = mesh->geometry.Indices().size();
        if (first_index > size || index_count > size - first_index || index_count % 3 != 0) return false;
        if (index_count == 0) return true;
        const auto has_texture = [&](std::size_t slot) {
            return slot < textures.size() && textures[slot].Is_Valid();
        };
        if ((parameters.textured > 0.5f && !has_texture(0))
            || (parameters.secondary_texture > 0.5f && !has_texture(1))
            || ((parameters.shroud > 0.5f || parameters.shroud_only > 0.5f) && !has_texture(3))) return false;
        if (parameters.surface.shading_model > .5f)
            for (std::size_t role=0; role<PropSurfaceTextureCount; ++role)
                if ((parameters.surface.maps & (1u << role)) != 0 && !has_texture(PropSurfaceTextureFirst+role)) return false;
        if (!Upload(*mesh)) return false;
        const RHIPipelineHandle pipeline = Pipeline(style);
        if (!pipeline.Is_Valid() || !m_device->Update_Buffer(m_constants, 0,
            std::as_bytes(std::span(&parameters, 1)))) return false;
        std::array<RHIBindlessResource, PropTextureCount+1> bindings{};
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
            && commands.Set_Vertex_Buffer(0, mesh->vertices, sizeof(PropVertex), 0)
            && commands.Set_Index_Buffer(mesh->indices, RHIIndexFormat::UInt32, 0)
            && commands.Draw_Indexed(index_count, first_index);
    }

private:
    void Release_GPU(PropMesh &mesh) noexcept
    {
        if (m_device != nullptr) {
            if (mesh.vertices.Is_Valid()) m_device->Destroy_Buffer(mesh.vertices);
            if (mesh.indices.Is_Valid()) m_device->Destroy_Buffer(mesh.indices);
        }
        mesh.vertices = {};
        mesh.indices = {};
    }

    bool Upload(PropMesh &mesh)
    {
        GRAPHICS_PROFILE_SCOPE("Graphics.Props.Upload");
        if (mesh.vertices.Is_Valid() && mesh.indices.Is_Valid()) return true;
        const auto vertices = std::as_bytes(mesh.geometry.Vertices());
        const auto indices = std::as_bytes(mesh.geometry.Indices());
        mesh.vertices = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(vertices.size()), RHIBufferUsage::Vertex, sizeof(PropVertex)}, vertices);
        mesh.indices = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(indices.size()), RHIBufferUsage::Index, sizeof(std::uint32_t)}, indices);
        if (!mesh.vertices.Is_Valid() || !mesh.indices.Is_Valid()) {
            Release_GPU(mesh);
            return false;
        }
        return true;
    }

    RHIPipelineHandle Pipeline(const PropStyle &style)
    {
        for (const auto &entry : m_pipelines) if (entry.style == style) return entry.handle;
        RHIPipeline description;
        description.vertex_format = RHIVertexFormat::Position3Color4UV2UV2Normal3;
        description.vertex_element_count = 11;
        description.vertex_elements[0] = {RHIVertexSemantic::Position,0,RHIVertexElementFormat::Float3,offsetof(PropVertex,position)};
        description.vertex_elements[1] = {RHIVertexSemantic::Color,0,RHIVertexElementFormat::Float4,offsetof(PropVertex,color)};
        description.vertex_elements[2] = {RHIVertexSemantic::TexCoord,0,RHIVertexElementFormat::Float2,offsetof(PropVertex,uv)};
        description.vertex_elements[3] = {RHIVertexSemantic::TexCoord,1,RHIVertexElementFormat::Float2,offsetof(PropVertex,secondary_uv)};
        description.vertex_elements[4] = {RHIVertexSemantic::Normal,0,RHIVertexElementFormat::Float3,offsetof(PropVertex,normal)};
        description.vertex_elements[5] = {RHIVertexSemantic::Color,1,RHIVertexElementFormat::Float4,offsetof(PropVertex,material_ambient)};
        description.vertex_elements[6] = {RHIVertexSemantic::Color,2,RHIVertexElementFormat::Float4,offsetof(PropVertex,material_diffuse)};
        description.vertex_elements[7] = {RHIVertexSemantic::Color,3,RHIVertexElementFormat::Float4,offsetof(PropVertex,material_emissive)};
        description.vertex_elements[8] = {RHIVertexSemantic::Color,4,RHIVertexElementFormat::Float4,offsetof(PropVertex,material_specular)};
        description.vertex_elements[9] = {RHIVertexSemantic::Color,5,RHIVertexElementFormat::Float4,offsetof(PropVertex,secondary_color)};
        description.vertex_elements[10] = {RHIVertexSemantic::TexCoord,2,RHIVertexElementFormat::Float4,offsetof(PropVertex,tangent)};

        description.blend_mode = style.blend;
        description.depth_write = style.depth_write;
        description.depth_test = style.depth_test;
        description.blend_alpha_like_color = true;
        description.custom_blend_factors = true;
        description.source_blend = style.source_blend;
        description.destination_blend = style.destination_blend;
        description.cull_mode = style.cull;
        description.depth_comparison = style.depth_comparison;
        description.front_counter_clockwise = style.front_counter_clockwise;
        description.color_write_mask = style.color_write_mask;
        description.stencil = style.stencil;
        description.depth_bias=style.depth_bias;
        description.wireframe=style.wireframe;
        description.sampler_count = 16;
        description.samplers[0] = style.samplers[0];
        description.samplers[1] = style.samplers[1];
        for (std::size_t slot=PropSurfaceTextureFirst; slot<PropTextureCount; ++slot)
            description.samplers[slot] = style.samplers[0];
        description.samplers[3].address.fill(RHISamplerAddress::Clamp);
        const RHIPipelineHandle handle = m_device->Create_Pipeline(description,
            {m_shaders.Bytecode(m_shader, ShaderStage::Vertex)}, {m_shaders.Bytecode(m_shader, ShaderStage::Pixel)});
        if (handle.Is_Valid()) m_pipelines.push_back({style, handle});
        return handle;
    }

    EnvironmentLightingBinding m_environment;
    Device *m_device = nullptr;
    ShaderLibrary m_shaders;
    ShaderHandle m_shader{};
    RHIBufferHandle m_constants{};
    ResourcePool<PropMesh, PropMeshHandle> m_meshes;
    std::vector<PropPipeline> m_pipelines;
};

export bool Draw_Prop(PropRenderer& renderer, CommandList& commands, PropMeshHandle mesh,
    const PropStyle& base_style, PropParameters parameters, std::span<const RHITextureHandle> textures,
    bool apply_shroud)
{
    if (!renderer.Draw(commands,mesh,base_style,parameters,textures)) return false;
    if (!apply_shroud) return true;
    PropStyle style = base_style;
    style.source_blend = RHIBlendFactor::Zero;
    style.destination_blend = RHIBlendFactor::SourceColor;
    style.depth_write = false;
    style.cull = RHICullMode::None;
    style.depth_comparison = RHIComparison::Equal;
    parameters.shroud_only = 1;
    parameters.textured = parameters.secondary_texture = 0;
    return renderer.Draw(commands,mesh,style,parameters,textures);
}

namespace { PropRenderer g_prop_renderer; }
export PropRenderer &Get_Prop_Renderer() noexcept { return g_prop_renderer; }
}
