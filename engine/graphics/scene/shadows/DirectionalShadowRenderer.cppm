module;
#include "../../profiling/Tracy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <numeric>
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
    // Shared owner for persistent caster geometry and immutable pose snapshots.
    PropRenderer& Caster_Renderer() noexcept { return m_renderer; }
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

    std::uint64_t Caster_Sort_Count() const noexcept { return m_caster_sort_count; }
    std::uint64_t Rendered_Cascade_Count() const noexcept { return m_rendered_cascades; }
    std::uint64_t Reused_Cascade_Count() const noexcept { return m_reused_cascades; }
    void Clear_Casters() noexcept
    {
        for (const auto& caster : m_casters)
            if (caster.source != nullptr) {
                caster.source->Instances().Release(caster.instance);
                caster.source->Destroy_Mesh(caster.source_mesh);
            }
        m_casters.clear();
        m_batches.clear(); m_instance_worlds.clear(); m_instance_indices.clear();
        if (m_transient_mesh.Is_Valid()) m_renderer.Update_Mesh(m_transient_mesh,{},{});
        m_transient_index_count = 0;
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
        m_cache_valid = {};
        Clear_Casters();
        if (m_transient_mesh.Is_Valid()) m_renderer.Destroy_Mesh(m_transient_mesh);
        m_transient_mesh = {};
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
        std::span<const std::uint32_t> indices, const PropParameters& parameters,
        std::span<const RHITextureHandle> textures, const PropStyle& material_style = {})
    {
        if (m_device == nullptr || textures.size() > PropTextureCount) return false;
        if (!m_transient_mesh.Is_Valid()) m_transient_mesh = m_renderer.Create_Mesh({},{});
        if (!m_transient_mesh.Is_Valid() || !m_renderer.Append_Mesh(m_transient_mesh,vertices,indices)) return false;
        auto caster = Make_Caster(parameters,textures,material_style);
        caster.first_index = m_transient_index_count;
        caster.index_count = static_cast<std::uint32_t>(indices.size());
        m_transient_index_count += caster.index_count;
        if (!vertices.empty()) {
            caster.bounds.minimum = caster.bounds.maximum = vertices.front().position;
            for (const auto& vertex : vertices) for (std::size_t axis=0;axis<3;++axis) {
                caster.bounds.minimum[axis] = std::min(caster.bounds.minimum[axis],vertex.position[axis]);
                caster.bounds.maximum[axis] = std::max(caster.bounds.maximum[axis],vertex.position[axis]);
            }
        }
        caster.bounds = Transform_Bounds(caster.bounds,parameters.world);
        m_casters.push_back(caster);
        return true;
    }

    // Retained geometry remains valid across frame-list clears. Its owner
    // releases it when geometry changes, before this renderer is destroyed.
    bool Add_Caster(ShadowCasterHandle mesh,const PropParameters& parameters,
        std::span<const RHITextureHandle> textures,const PropStyle& material_style = {})
    {
        if (!Is_Caster_Valid(mesh) || textures.size() > PropTextureCount) return false;
        auto caster = Make_Caster(parameters,textures,material_style);
        caster.mesh = mesh;
        caster.bounds = Transform_Bounds(*m_meshes.Resolve(mesh),parameters.world);
        m_casters.push_back(caster);
        return true;
    }

    // Share the exact mesh version used by color/reflection passes. Retain it
    // through all cascades even if its source publishes new geometry meanwhile.
    bool Add_Caster(PropRenderer& source,PropMeshHandle mesh,const PropParameters& parameters,
        std::span<const RHITextureHandle> textures,const PropStyle& material_style = {}, PropInstanceHandle instance = {})
    {
        if (m_device == nullptr || textures.size() > PropTextureCount) return false;
        const auto* geometry = source.Mesh_Geometry(mesh);
        if (geometry == nullptr || !source.Retain_Mesh(mesh)) return false;
        auto caster = Make_Caster(parameters,textures,material_style);
        caster.source = &source;
        caster.source_mesh = mesh;
        if (!instance.Is_Valid()) instance=source.Instances().Update({},parameters);
        else if (!source.Instances().Retain(instance)) { source.Destroy_Mesh(mesh); return false; }
        caster.instance=instance;
        {
            GRAPHICS_PROFILE_SCOPE("Graphics.Shadows.MeshBounds");
            if (!source.Mesh_Bounds(mesh,instance,caster.bounds.minimum,caster.bounds.maximum)) {
                source.Instances().Release(instance); source.Destroy_Mesh(mesh); return false;
            }
        }
        caster.bounds = Transform_Bounds(caster.bounds,source.Instances().Resolve(instance)->world);
        m_casters.push_back(caster);
        return true;
    }

    bool Render(CommandList& commands,const View& view,const RenderLight& light,
        const ShadowSettings& settings,RHITextureHandle color_target,
        RHITextureHandle depth_target,RHIViewport viewport)
    {
        GRAPHICS_PROFILE_SCOPE("Graphics.Shadows.Render");
        if (m_device == nullptr || !color_target.Is_Valid() || !depth_target.Is_Valid()) return false;
        ShadowCascades cascades;
        if (!Build_Shadow_Cascades(view,LightHandle(0,1),light,settings,cascades)) return false;
        if (!Prepare_Maps(settings)) return false;
        Prepare_Visibility(cascades);
        std::array<bool,Max_Shadow_Cascades> dirty;
        if (settings.cache_maps) dirty=Prepare_Cache(cascades);
        else { dirty.fill(true); m_cacheable={}; }
        const bool any_dirty = std::any_of(dirty.begin(),dirty.begin()+cascades.count,[](bool value){return value;});
        if (any_dirty && !Prepare_Batches()) { m_cache_valid = {}; return false; }
        auto& environment = Get_Environment_Lighting();
        const auto saved = environment;
        environment.parameters.shadow_options[0] = 0;
        environment.parameters.cloud_offset_strength[3] = 0;
        environment.parameters.clip_plane = {};
        const bool rendered = !any_dirty || m_plan.Execute(m_graph,commands,
            [&](GraphPassHandle pass,CommandList& list,const PassResources& resources) {
                std::uint32_t cascade = 0;
                while (cascade<cascades.count && m_passes[cascade]!=pass) ++cascade;
                if (cascade == cascades.count) return false;
                if (!dirty[cascade]) return true;
                if (!list.Set_Depth_Target(resources.Texture(m_maps.Target(cascade)))
                    || !list.Set_Viewport({0,0,settings.map_size,settings.map_size})
                    || !list.Clear_Depth(1)) return false;
                for (const auto& batch : m_batches) {
                    const auto& first = m_casters[m_caster_order[batch.first]];
                    std::size_t instance_count = 0;
                    for (auto index=batch.first; index<batch.end; ++index) {
                        const auto& caster = m_casters[m_caster_order[index]];
                        if (m_cascade_visibility[m_caster_order[index]] & (1u << cascade)) {
                            if (!first.source) m_instance_worlds[instance_count] = caster.world;
                            m_instance_indices[instance_count] = caster.instance.Get_Index();
                            ++instance_count;
                        }
                    }
                    if (instance_count == 0) continue;
                    const auto textures=std::span(first.textures.data(),first.texture_count);
                    if (first.source) {
                        auto parameters=first.parameters;
                        parameters.view.view_projection=cascades.views[cascade].view_projection.values;
                        if (!batch.renderer->Draw_Records(list,batch.mesh,first.style,parameters,textures,
                            std::span<const std::uint32_t>(m_instance_indices.data(),instance_count))) return false;
                    } else {
                        PropParameters parameters;
                        std::memcpy(&parameters,&first.parameters.view,sizeof(PropViewConstants));
                        std::memcpy(&parameters.textured,&first.parameters.material,sizeof(PropMaterialConstants));
                        parameters.view_projection=cascades.views[cascade].view_projection.values;
                        parameters.world=m_instance_worlds.front();
                        const auto worlds=instance_count==1 ? std::span<const std::array<float,16>>{}
                            : std::span<const std::array<float,16>>(m_instance_worlds.data(),instance_count);
                        if (!batch.renderer->Draw_Range(list,batch.mesh,first.style,parameters,textures,
                            batch.first_index,batch.index_count,worlds)) return false;
                    }
                }
                return true;
            });
        environment = saved;
        // Restoration is attempted even if a caster submission failed.
        const bool restored = commands.Set_Render_Targets(color_target,depth_target)
            && commands.Set_Viewport(viewport);
        environment.parameters.shadow_options[0] = 0;
        if (!rendered || !restored) { m_cache_valid = {}; return false; }
        for (std::uint32_t cascade=0;cascade<cascades.count;++cascade) {
            if (dirty[cascade]) {
                ++m_rendered_cascades;
                std::swap(m_cached_keys[cascade],m_current_keys[cascade]);
            } else ++m_reused_cascades;
            m_cache_valid[cascade] = m_cacheable[cascade];
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
    struct CacheKey final {
        std::vector<std::byte> data;
        std::vector<PropStyle> styles;
        bool operator==(const CacheKey&) const = default;
        template<class Value> void Append(const Value& value) {
            const auto bytes=std::as_bytes(std::span(&value,1));
            data.insert(data.end(),bytes.begin(),bytes.end());
        }
    };

    std::array<bool,Max_Shadow_Cascades> Prepare_Cache(const ShadowCascades& cascades)
    {
        GRAPHICS_PROFILE_SCOPE("Graphics.Shadows.CacheInputs");
        for (unsigned i=0;i<cascades.count;++i) {
            auto& key=m_current_keys[i]; key.data.clear(); key.styles.clear();
            key.Append(cascades.views[i].view_projection.values);
            m_cacheable[i]=true;
        }
        for (std::size_t caster_index=0;caster_index<m_casters.size();++caster_index) {
            const auto& caster=m_casters[caster_index];
            const auto visibility=m_cascade_visibility[caster_index];
            if (!visibility) continue;
            auto* renderer=caster.source ? caster.source : &m_renderer;
            auto mesh=caster.source_mesh;
            if (!caster.source) {
                const auto* stored=m_meshes.Resolve(caster.mesh);
                mesh=stored ? stored->handle : m_transient_mesh;
            }
            const auto* geometry=renderer->Mesh_Geometry(mesh);
            std::array<std::uint64_t,PropTextureCount> versions{};
            bool cacheable=geometry!=nullptr;
            for (unsigned texture=0;texture<caster.texture_count;++texture) {
                if (!caster.textures[texture].Is_Valid()) continue;
                versions[texture]=m_device->Texture_Content_Version(caster.textures[texture]);
                cacheable &= versions[texture]!=0;
            }
            for (unsigned i=0;i<cascades.count;++i) {
                if (!(visibility & (1u << i))) continue;
                auto& key=m_current_keys[i];
                m_cacheable[i] &= cacheable;
                key.Append(renderer); key.Append(mesh.Get_Index()); key.Append(mesh.Get_Generation());
                key.Append(geometry ? geometry->Revision() : 0);
                key.Append(caster.first_index); key.Append(caster.index_count);
                key.Append(caster.parameters);
                const auto* instance=caster.source ? caster.source->Instances().Resolve(caster.instance) : nullptr;
                key.Append(instance ? instance->world : caster.world);
                key.Append(caster.texture_count);
                key.Append(caster.textures); key.Append(versions);
                key.styles.push_back(caster.style);
                // Compare pose contents, not reused palette addresses or
                // lighting-only instance generations. Wind also uses palettes.
                const auto pose=caster.source ? caster.source->Instances().Pose(caster.instance)
                    : std::span<const PropBoneTransform>{};
                key.Append(pose.size());
                const auto bytes=std::as_bytes(pose);
                key.data.insert(key.data.end(),bytes.begin(),bytes.end());
            }
        }
        std::array<bool,Max_Shadow_Cascades> dirty{};
        for (unsigned i=0;i<cascades.count;++i)
            dirty[i]=!m_cache_valid[i] || !m_cacheable[i] || m_cached_keys[i]!=m_current_keys[i];
        return dirty;
    }

    struct Mesh final
    {
        PropMeshHandle handle{};
        std::array<float,3> minimum{};
        std::array<float,3> maximum{};
    };

    // Cache validation and drawing use the same bounds and cascade matrices.
    // Compute membership once per render and share it between both consumers.
    std::vector<std::uint8_t> m_cascade_visibility;
    void Prepare_Visibility(const ShadowCascades& cascades)
    {
        std::array<std::array<std::array<float,4>,6>,Max_Shadow_Cascades> planes;
        for (unsigned i=0;i<cascades.count;++i)
            planes[i]=Cascade_Planes(cascades.views[i].view_projection);
        m_cascade_visibility.assign(m_casters.size(),0);
        for (std::size_t index=0;index<m_casters.size();++index)
            for (unsigned i=0;i<cascades.count;++i)
                if (Intersects_Cascade(m_casters[index].bounds,planes[i]))
                    m_cascade_visibility[index] |= static_cast<std::uint8_t>(1u << i);
    }

    static Mesh Transform_Bounds(const Mesh& local,const std::array<float,16>& world) noexcept
    {
        // Skinned palettes already publish world-space bounds. Identity does
        // not introduce rounding, so it needs neither transformation nor padding.
        if (world==std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}) return local;
        Mesh result;
        result.handle = local.handle;
        for (unsigned axis=0;axis<3;++axis) {
            const auto offset=axis*4;
            float lower=0,upper=0,magnitude=std::abs(world[offset+3]);
            // Each row's extrema select endpoints independently. Pad outward
            // for CPU/GPU float rounding, including negative scales and shear.
            for (unsigned column=0;column<3;++column) {
                const float a=world[offset+column]*local.minimum[column];
                const float b=world[offset+column]*local.maximum[column];
                lower+=(std::min)(a,b);upper+=(std::max)(a,b);
                magnitude+=(std::max)(std::abs(a),std::abs(b));
            }
            const float error=8*std::numeric_limits<float>::epsilon()*magnitude
                +8*std::numeric_limits<float>::min();
            result.minimum[axis]=(lower+world[offset+3])-error;
            result.maximum[axis]=(upper+world[offset+3])+error;
        }
        return result;
    }

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
        explicit Caster(const PropParameters& input) : parameters(input),world(input.world) {}
        PropRenderer* source = nullptr;
        PropMeshHandle source_mesh{};
        PropInstanceHandle instance{};
        ShadowCasterHandle mesh{};
        Mesh bounds;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        // Depth consumes view, alpha/mapping and world inputs. RGB lighting
        // remains with color submissions instead of every cascade's hot records.
        PropSharedParameters parameters;
        std::array<float,16> world;
        PropStyle style;
        std::array<RHITextureHandle,PropTextureCount> textures{};
        std::uint32_t texture_count = 0;
    };

    struct SortKey final {
        PropRenderer* source=nullptr;
        PropMeshHandle source_mesh{};
        ShadowCasterHandle mesh{};
        std::uint32_t first_index=0;
        bool operator==(const SortKey&) const noexcept = default;
    };

    struct Batch final {
        PropRenderer* renderer;
        PropMeshHandle mesh;
        std::uint32_t first_index, index_count;
        std::size_t first, end;
    };

    bool Prepare_Batches()
    {
        // Every caster uses LessEqual depth writes with color and stencil
        // writes disabled. Equal-depth fragments therefore commute as well.
        // Group retained geometry once, before any cascade visibility loops.
        bool reorder=m_sort_keys.size()!=m_casters.size() || m_caster_order.size()!=m_casters.size();
        m_sort_keys.resize(m_casters.size());
        for (std::size_t index=0;index<m_casters.size();++index) {
            const auto& caster=m_casters[index];
            const SortKey key{caster.source,caster.source_mesh,caster.mesh,caster.first_index};
            if (key!=m_sort_keys[index]) { m_sort_keys[index]=key;reorder=true; }
        }
        const auto less = [&](std::size_t a, std::size_t b) {
            const auto& left = m_sort_keys[a]; const auto& right = m_sort_keys[b];
            if (left.source != right.source) return std::less<PropRenderer*>{}(left.source,right.source);
            if (left.source_mesh.Get_Index() != right.source_mesh.Get_Index())
                return left.source_mesh.Get_Index() < right.source_mesh.Get_Index();
            if (left.source_mesh.Get_Generation() != right.source_mesh.Get_Generation())
                return left.source_mesh.Get_Generation() < right.source_mesh.Get_Generation();
            if (left.mesh.Get_Index() != right.mesh.Get_Index()) return left.mesh.Get_Index() < right.mesh.Get_Index();
            if (left.mesh.Get_Generation() != right.mesh.Get_Generation()) return left.mesh.Get_Generation() < right.mesh.Get_Generation();
            return left.first_index < right.first_index;
        };
        if (reorder) {
            m_caster_order.resize(m_casters.size());
            std::iota(m_caster_order.begin(),m_caster_order.end(),std::size_t{0});
            std::sort(m_caster_order.begin(),m_caster_order.end(),less);
            ++m_caster_sort_count;
        }
        m_batches.clear();
        m_batches.reserve(m_casters.size());
        m_instance_worlds.resize(m_casters.size());
        m_instance_indices.resize(m_casters.size());
        for (std::size_t first=0; first<m_caster_order.size();) {
            const auto& caster = m_casters[m_caster_order[first]];
            assert(caster.style.color_write_mask == 0 && !caster.style.stencil.enabled);
            auto* renderer = caster.source != nullptr ? caster.source : &m_renderer;
            auto mesh = caster.source_mesh;
            std::uint32_t first_index = 0, index_count = 0;
            if (caster.source == nullptr) {
                if (caster.mesh.Is_Valid()) {
                    const auto* stored = m_meshes.Resolve(caster.mesh);
                    if (stored == nullptr) return false;
                    mesh = stored->handle;
                } else {
                    mesh = m_transient_mesh;
                    first_index = caster.first_index; index_count = caster.index_count;
                }
            }
            if (caster.source != nullptr || caster.mesh.Is_Valid()) {
                const auto* geometry = renderer->Mesh_Geometry(mesh);
                if (geometry == nullptr) return false;
                index_count = static_cast<std::uint32_t>(geometry->Indices().size());
            }
            auto end = first+1;
            while (end < m_caster_order.size()) {
                const auto& next = m_casters[m_caster_order[end]];
                if (next.source != caster.source || next.source_mesh != caster.source_mesh || next.mesh != caster.mesh
                    || next.first_index != caster.first_index || next.index_count != caster.index_count
                    || next.style != caster.style || next.texture_count != caster.texture_count || next.textures != caster.textures
                    || !Same_Depth_Parameters(next.parameters,caster.parameters)) break;
                ++end;
            }
            m_batches.push_back({renderer,mesh,first_index,index_count,first,end});
            first = end;
        }
        return true;
    }

    static bool Same_Depth_Parameters(const PropSharedParameters& left,const PropSharedParameters& right) noexcept
    {
        return std::memcmp(&left,&right,sizeof(PropSharedParameters))==0;
    }

    static Caster Make_Caster(const PropParameters& parameters,std::span<const RHITextureHandle> textures,
        const PropStyle& material_style)
    {
        Caster caster(parameters);
        // Depth depends on position, generated UVs and material alpha only.
        // Canonicalize colour-only inputs before batching and cache comparison:
        // changing fog, shroud or a normal map must not invalidate a silhouette.
        auto& view = caster.parameters.view;
        view.view_projection = {}; // Replaced by each light cascade at draw time.
        view.shroud_projection = view.fog_color = view.fog_state = view.camera_position = {};
        auto& material = caster.parameters.material;
        material.shroud = material.shroud_only = 0;
        material.detail_color = material.secondary_gradient = material.texture_luminance = 0;
        const auto shading_model = material.surface.shading_model;
        const auto vertex_uv_offset = material.surface.maps & PropSurfaceVertexAlphaUVOffset;
        material.surface = {};
        material.surface.shading_model = shading_model;
        material.surface.maps = vertex_uv_offset;
        const auto camera_uv = [](float source) { return source == 1 || source == 2 || source == 3; };
        if (!camera_uv(material.uv_sources[0]) && !camera_uv(material.uv_sources[2])) view.view = {};
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
        // Only the base/detail stages contribute alpha. Surface maps (4..10)
        // and shroud (3) affect RGB exclusively, including the surface shader.
        caster.texture_count = static_cast<std::uint32_t>((std::min)(textures.size(),std::size_t{2}));
        for (std::size_t index=0;index<caster.texture_count;++index) caster.textures[index] = textures[index];
        return caster;
    }

    bool Prepare_Maps(const ShadowSettings& settings)
    {
        if (m_maps.Count() == settings.cascade_count && m_maps.Map_Size() == settings.map_size && m_plan.Is_Valid()) return true;
        m_cache_valid = {};
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
    std::array<CacheKey,Max_Shadow_Cascades> m_cached_keys, m_current_keys;
    std::array<bool,Max_Shadow_Cascades> m_cache_valid{}, m_cacheable{};
    std::uint64_t m_rendered_cascades=0, m_reused_cascades=0;
    PropRenderer m_renderer;
    ResourcePool<Mesh,ShadowCasterHandle> m_meshes;
    PropMeshHandle m_transient_mesh{};
    std::uint32_t m_transient_index_count = 0;
    ShadowMapResources m_maps;
    RenderGraph m_graph;
    ExecutionPlan m_plan;
    std::array<GraphPassHandle,4> m_passes{};
    std::array<GraphResourceBinding,4> m_bindings{};
    std::vector<Caster> m_casters;
    // This cache owns only ordering metadata. Every frame still rebuilds batch
    // compatibility and evaluates current bounds, poses, textures and materials.
    std::vector<SortKey> m_sort_keys;
    std::vector<std::size_t> m_caster_order;
    std::uint64_t m_caster_sort_count=0;
    std::vector<Batch> m_batches;
    std::vector<std::array<float,16>> m_instance_worlds;
    std::vector<std::uint32_t> m_instance_indices;
};

export DirectionalShadowRenderer& Get_Directional_Shadow_Renderer() noexcept
{
    static DirectionalShadowRenderer renderer;
    return renderer;
}
}
