module;
#include "../../profiling/Tracy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

export module Graphics.Scene.Terrain.Renderer;

export import Graphics.Scene.Terrain.Geometry;
export import Graphics.RHI;
import Graphics.Shaders.Library;
import Graphics.Resources.Textures.Sampling;
import Graphics.Scene.Lighting.Environment;
import Graphics.Scene.Terrain.Visibility;
import Graphics.Scene.Shadows.DirectionalRenderer;

namespace Graphics
{

export enum class TerrainSurfacePass : std::uint8_t
{
    Surface,
    Overlay,
    Shroud,
    Shoreline,
    Mask
};

export struct TerrainDrawParameters final
{
    std::array<float, 16> view_projection{};
    std::array<float, 4> cloud_projection{};
    std::array<float, 4> lightmap_projection{};
    std::array<float, 4> shroud_projection{};
    std::array<float, 4> features{};
    std::array<float, 4> lighting{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> options{};
    std::array<float,4> camera_position{};
    // Enabled, green flip, height amplitude in world units, reserved.
    std::array<float,4> surface{};
};
static_assert(sizeof(TerrainDrawParameters) == 192);

struct TerrainPipeline final
{
    std::size_t pass;
    bool wireframe;
    RHISamplerDescription sampler;
    RHIPipelineHandle handle;
};

// One instance owns one surface batch. Adapters supply cells and resolved
// texture handles; all topology construction and GPU lifetime stays here.
export class TerrainRenderer final
{
public:
    TerrainRenderer() = default;
    TerrainRenderer(const TerrainRenderer &) = delete;
    TerrainRenderer &operator=(const TerrainRenderer &) = delete;
    ~TerrainRenderer() { Shutdown(); }

    bool Add_Shadow_Caster(DirectionalShadowRenderer& shadows,
        const std::array<float,16>& world)
    {
        if (m_geometry.Indices().empty()) return true;
        PropParameters parameters;
        parameters.textured = 0;
        const auto source = m_geometry.Vertices();
        const auto indices = m_geometry.Indices();
        const bool current = m_shadow_owner == &shadows && world == m_shadow_world
            && m_shadow_cell_patches.size() == source.size()/4
            && std::all_of(m_shadow_meshes.begin(),m_shadow_meshes.end(),
                [&](auto mesh) { return !mesh.Is_Valid() || shadows.Is_Caster_Valid(mesh); });
        if (!current) {
            Release_Shadow_Caster();
            m_shadow_owner = &shadows;
            m_shadow_world = world;
            auto minimum = source.front().position, maximum = minimum;
            for (const auto& vertex : source) for (unsigned axis=0; axis<2; ++axis) {
                minimum[axis] = std::min(minimum[axis],vertex.position[axis]);
                maximum[axis] = std::max(maximum[axis],vertex.position[axis]);
            }
            m_shadow_cell_patches.resize(source.size()/4);
            for (std::size_t cell=0; cell<source.size()/4; ++cell) {
                unsigned patch = 0;
                for (unsigned axis=0; axis<2; ++axis) {
                    const double extent = double(maximum[axis])-minimum[axis];
                    const double center = (double(source[cell*4].position[axis])+source[cell*4+2].position[axis])*.5;
                    const unsigned coordinate = extent>0
                        ? static_cast<unsigned>(std::clamp((center-minimum[axis])*8/extent,0.,7.)) : 0;
                    patch += coordinate*(axis==0 ? 1u : 8u);
                }
                m_shadow_cell_patches[cell] = patch;
                m_shadow_patch_cells[patch].push_back(cell);
            }
            m_shadow_dirty.fill(true);
        }
        for (unsigned patch=0; patch<64; ++patch) {
            if (!m_shadow_dirty[patch]) continue;
            std::vector<PropVertex> vertices;
            std::vector<std::uint32_t> triangles;
            vertices.reserve(m_shadow_patch_cells[patch].size()*4);
            triangles.reserve(m_shadow_patch_cells[patch].size()*6);
            for (const auto cell : m_shadow_patch_cells[patch]) {
                const auto first = static_cast<std::uint32_t>(vertices.size());
                for (unsigned corner=0; corner<4; ++corner) {
                    PropVertex vertex;
                    const auto& position = source[cell*4+corner].position;
                    for (unsigned row=0; row<3; ++row)
                        vertex.position[row] = world[row*4]*position[0]+world[row*4+1]*position[1]
                            +world[row*4+2]*position[2]+world[row*4+3];
                    vertices.push_back(vertex);
                }
                for (unsigned index=0; index<6; ++index)
                    triangles.push_back(first+indices[cell*6+index]-static_cast<std::uint32_t>(cell*4));
            }
            if (!triangles.empty()) {
                const auto mesh = shadows.Create_Caster(vertices,triangles);
                if (!mesh.Is_Valid()) return false;
                if (m_shadow_meshes[patch].Is_Valid()) shadows.Destroy_Caster(m_shadow_meshes[patch]);
                m_shadow_meshes[patch] = mesh;
            }
            m_shadow_dirty[patch] = false;
        }
        for (auto mesh : m_shadow_meshes)
            if (mesh.Is_Valid() && !shadows.Add_Caster(mesh,parameters,{})) return false;
        return true;
    }

    bool Initialize(Device &device, const std::filesystem::path &shader_directory)
    {
        if (m_device == &device && m_constants.Is_Valid()) return true;
        Shutdown();
        m_device = &device;
        ShaderPrecompiledDesc shader;
        shader.program.vertex_shader = 11;
        shader.program.fragment_shader = 11;
        shader.program.source_key = 0x5445525241494e31ull;
        shader.program.stages = ShaderStageMask::Vertex | ShaderStageMask::Pixel;
        shader.vertex_path = shader_directory / "terrain.vso";
        shader.fragment_path = shader_directory / "terrain.pso";
        m_shaders = ShaderLibrary{};
        m_shader = m_shaders.Load_Precompiled(shader);
        if (!m_shaders.Is_Loaded(m_shader)) {
            Shutdown();
            return false;
        }
        m_constants = device.Create_Buffer({sizeof(TerrainDrawParameters), RHIBufferUsage::Constant});
        if (!m_constants.Is_Valid() || !m_environment.Initialize(device)) {
            Shutdown();
            return false;
        }
        if (!Upload_Surface()) { Shutdown(); return false; }
        return true;
    }

    void Shutdown() noexcept
    {
        Release_GPU_Surface();
        if (m_device != nullptr) {
            m_environment.Shutdown(*m_device);
            for (const auto &pipeline : m_pipelines)
                m_device->Destroy_Pipeline(pipeline.handle);
            if (m_constants.Is_Valid()) m_device->Destroy_Buffer(m_constants);
        }
        m_device = nullptr;
        m_pipelines = {};
        m_vertices = {};
        m_indices = {};
        m_constants = {};
        m_index_count = 0;
        m_vertex_bytes = m_index_bytes = 0;
    }

    void Release_Surface() noexcept
    {
        Release_GPU_Surface();
        m_geometry = {};
    }

private:
    void Release_GPU_Surface() noexcept
    {
        Release_Shadow_Caster();
        if (m_device != nullptr) {
            if (m_vertices.Is_Valid()) m_device->Destroy_Buffer(m_vertices);
            if (m_indices.Is_Valid()) m_device->Destroy_Buffer(m_indices);
        }
        m_vertices = {};
        m_indices = {};
        m_index_count = 0;
        m_vertex_bytes = m_index_bytes = 0;
    }

public:
    bool Set_Cells(std::span<const TerrainCell> cells)
    {
        if (!m_geometry.Build(cells)) return false;
        Release_Shadow_Caster();
        m_index_count = 0;
        return m_device == nullptr || Upload_Surface();
    }

    bool Update_Cells(std::size_t first, std::span<const TerrainCell> cells)
    {
        const auto old_vertices = m_geometry.Vertices();
        if (first > old_vertices.size()/4 || cells.size() > old_vertices.size()/4-first) return false;
        bool footprint_changed = false;
        for (std::size_t i=0; i<cells.size(); ++i) {
            const auto& low = old_vertices[(first+i)*4].position;
            const auto& high = old_vertices[(first+i)*4+2].position;
            const auto& cell = cells[i];
            footprint_changed |= low[0]!=cell.origin[0] || low[1]!=cell.origin[1]
                || high[0]!=cell.origin[0]+cell.spacing[0] || high[1]!=cell.origin[1]+cell.spacing[1];
        }
        if (!m_geometry.Update(first,cells)) return false;
        if (cells.empty()) return true;
        if (footprint_changed) Release_Shadow_Caster();
        else if (!m_shadow_cell_patches.empty())
            for (std::size_t i=first; i<first+cells.size(); ++i)
                m_shadow_dirty[m_shadow_cell_patches[i]] = true;
        if (m_device == nullptr) return true;
        if (!m_vertices.Is_Valid() || !m_indices.Is_Valid() || m_index_count == 0)
            return Upload_Surface();
        const auto vertices = std::as_bytes(m_geometry.Vertices().subspan(first*4,cells.size()*4));
        const auto indices = std::as_bytes(m_geometry.Indices().subspan(first*6,cells.size()*6));
        if (!m_device->Update_Buffer(m_vertices,static_cast<std::uint32_t>(first*4*sizeof(TerrainVertex)),vertices)
            || !m_device->Update_Buffer(m_indices,static_cast<std::uint32_t>(first*6*sizeof(std::uint32_t)),indices)) {
            m_index_count = 0;
            return false;
        }
        return true;
    }

private:
    bool Upload_Surface()
    {
        if (m_geometry.Indices().empty()) {
            m_index_count = 0;
            return true;
        }
        const auto vertices = std::as_bytes(m_geometry.Vertices());
        const auto indices = std::as_bytes(m_geometry.Indices());
        if (m_vertices.Is_Valid() && m_indices.Is_Valid()
            && vertices.size() == m_vertex_bytes && indices.size() == m_index_bytes) {
            m_index_count = 0;
            if (!m_device->Update_Buffer(m_vertices, 0, vertices) || !m_device->Update_Buffer(m_indices, 0, indices))
                return false;
            m_index_count = static_cast<std::uint32_t>(m_geometry.Indices().size());
            return true;
        }
        const RHIBufferHandle vertex_buffer = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(vertices.size()), RHIBufferUsage::Vertex, sizeof(TerrainVertex)}, vertices);
        if (!vertex_buffer.Is_Valid()) return false;
        const RHIBufferHandle index_buffer = m_device->Create_Buffer_Initialized(
            {static_cast<std::uint32_t>(indices.size()), RHIBufferUsage::Index, sizeof(std::uint32_t)}, indices);
        if (!index_buffer.Is_Valid()) {
            m_device->Destroy_Buffer(vertex_buffer);
            return false;
        }
        if (m_vertices.Is_Valid()) m_device->Destroy_Buffer(m_vertices);
        if (m_indices.Is_Valid()) m_device->Destroy_Buffer(m_indices);
        m_vertex_bytes = vertices.size();
        m_index_bytes = indices.size();
        m_vertices = vertex_buffer;
        m_indices = index_buffer;
        m_index_count = static_cast<std::uint32_t>(m_geometry.Indices().size());
        return true;
    }

public:
    // Targets and viewport are established by the frame/pass owner. This also
    // permits drawing into a reflection target without assuming a swap chain.
    bool Render(CommandList &commands, TerrainSurfacePass pass,
        const TerrainDrawParameters &parameters, std::span<const RHITextureHandle> textures,
        bool linear_filter = true, bool wireframe = false) noexcept
    {
        GRAPHICS_PROFILE_SCOPE("Graphics.Terrain.Render");
        const std::size_t pass_index = static_cast<std::size_t>(pass);
        if (m_device == nullptr || pass_index >= 5 || textures.size() > 8)
            return false;
        if (m_index_count == 0) return true;
        const auto has_texture = [&](std::size_t slot) {
            return slot < textures.size() && textures[slot].Is_Valid();
        };
        if (pass == TerrainSurfacePass::Shoreline) {
            if (!has_texture(0)) return false;
        } else if (pass == TerrainSurfacePass::Shroud || pass == TerrainSurfacePass::Mask) {
            if (!has_texture(4)) return false;
        } else if (parameters.features[2] <= 0.5f) {
            if (!has_texture(0) || (pass == TerrainSurfacePass::Surface && !has_texture(1))
                || (parameters.features[0] > 0.5f && !has_texture(2))
                || (parameters.features[1] > 0.5f && !has_texture(3))
                || (parameters.options[1] > 0.5f && !has_texture(4))) return false;
        }
        if (parameters.surface[0] > .5f && parameters.options[2] < .5f && pass_index < 2 && (!has_texture(5) || !has_texture(6) || !has_texture(7))) return false;
        if (!m_device->Update_Buffer(m_constants, 0,
            std::as_bytes(std::span<const TerrainDrawParameters>(&parameters, 1))))
            return false;
        std::array<RHIBindlessResource, 9> bindings{};
        bindings[0].type = RHIResourceType::Material;
        bindings[0].buffer = m_constants;
        std::size_t count = 1;
        for (std::size_t index = 0; index < textures.size(); ++index) {
            if (!textures[index].Is_Valid()) continue;
            bindings[count].type = RHIResourceType::Texture;
            bindings[count].index = ResourceIndex{static_cast<std::uint32_t>(index), 1};
            bindings[count].texture = textures[index];
            ++count;
        }
        const auto pipeline = Pipeline(pass_index, linear_filter, wireframe);
        if (!pipeline.Is_Valid()) return false;
        if (!(commands.Bind_Pipeline(pipeline)
            && commands.Set_Bindless_Resources(std::span<const RHIBindlessResource>(bindings.data(), count))
            && m_environment.Bind(*m_device, commands)
            && commands.Set_Vertex_Buffer(0, m_vertices, sizeof(TerrainVertex), 0)
            && commands.Set_Index_Buffer(m_indices, RHIIndexFormat::UInt32, 0))) return false;
        for (const TerrainGeometryBatch &batch : m_geometry.Batches()) {
            if (Is_Terrain_Batch_Visible(batch, parameters.view_projection)
                && !commands.Draw_Indexed(batch.index_count, batch.first_index)) return false;
        }
        return true;
    }

private:
    RHIPipelineHandle Pipeline(std::size_t pass, bool linear_filter, bool wireframe)
    {
        auto sampler = Resolve_Texture_Sampling(TextureSampling{}, Get_Texture_Sampling_Settings());
        sampler.address.fill(RHISamplerAddress::Clamp);
        if (!linear_filter) {
            sampler.Set_Filter(RHISamplerFilter::Point);
            sampler.anisotropy = 1;
        }
        for (const auto &pipeline : m_pipelines)
            if (pipeline.pass == pass && pipeline.wireframe == wireframe && pipeline.sampler == sampler)
                return pipeline.handle;

        RHIPipeline description;
        description.vertex_format = RHIVertexFormat::Position3Color4UV2UV2Normal3;
        description.cull_mode = RHICullMode::None;
        description.wireframe = wireframe;
        description.depth_write = pass == 0 || pass == 4;
        description.color_write_mask = pass >= 3 ? 8 : 7;
        description.blend_mode = pass == 1 ? RHIBlendMode::Alpha
            : pass == 2 ? RHIBlendMode::Multiply : RHIBlendMode::Disabled;
        description.sampler_count = 16;
        description.samplers[0] = description.samplers[1] = sampler;
        const auto handle = m_device->Create_Pipeline(description,
            {m_shaders.Bytecode(m_shader, ShaderStage::Vertex)},
            {m_shaders.Bytecode(m_shader, ShaderStage::Pixel)});
        if (handle.Is_Valid()) m_pipelines.push_back({pass, wireframe, sampler, handle});
        return handle;
    }

    void Release_Shadow_Caster() noexcept
    {
        if (m_shadow_owner != nullptr)
            for (auto mesh:m_shadow_meshes) m_shadow_owner->Destroy_Caster(mesh);
        m_shadow_owner = nullptr;
        m_shadow_meshes = {};
        m_shadow_cell_patches.clear();
        for (auto& cells : m_shadow_patch_cells) cells.clear();
        m_shadow_dirty.fill(false);
    }

    DirectionalShadowRenderer* m_shadow_owner = nullptr;
    std::array<ShadowCasterHandle,64> m_shadow_meshes{};
    std::array<bool,64> m_shadow_dirty{};
    std::vector<unsigned> m_shadow_cell_patches;
    std::array<std::vector<std::size_t>,64> m_shadow_patch_cells;
    std::array<float,16> m_shadow_world{};
    EnvironmentLightingBinding m_environment;
    Device *m_device = nullptr;
    TerrainGeometry m_geometry;
    ShaderLibrary m_shaders;
    ShaderHandle m_shader{};
    std::vector<TerrainPipeline> m_pipelines;
    RHIBufferHandle m_vertices{};
    RHIBufferHandle m_indices{};
    RHIBufferHandle m_constants{};
    std::uint32_t m_index_count = 0;
    std::size_t m_vertex_bytes = 0;
    std::size_t m_index_bytes = 0;
};

namespace
{
std::array<TerrainRenderer, 3> g_terrain_renderers;
}

export TerrainRenderer &Get_Terrain_Renderer() noexcept { return g_terrain_renderers[0]; }
export TerrainRenderer &Get_Terrain_Overlay_Renderer() noexcept { return g_terrain_renderers[1]; }
export TerrainRenderer &Get_Terrain_Shoreline_Renderer() noexcept { return g_terrain_renderers[2]; }

export bool Initialize_Terrain_Renderers(Device &device, const std::filesystem::path &directory)
{
    for (TerrainRenderer &renderer : g_terrain_renderers) {
        if (!renderer.Initialize(device, directory)) {
            for (TerrainRenderer &created : g_terrain_renderers) created.Shutdown();
            return false;
        }
    }
    return true;
}

export void Shutdown_Terrain_Renderers() noexcept
{
    for (TerrainRenderer &renderer : g_terrain_renderers) renderer.Shutdown();
}

}
