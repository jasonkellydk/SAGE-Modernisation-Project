export module Graphics.Scene.Screen.Filters;
import std;
export import Graphics.RHI;
import Graphics.Shaders.Library;
namespace Graphics
{
export struct ScreenFilterVertex final
{
    std::array<float,3> position{};
    std::array<float,4> color{1,1,1,1};
    std::array<float,2> uv{};
    std::array<float,2> mask_uv{};
    std::array<float,3> normal{};
};
static_assert(sizeof(ScreenFilterVertex)==56);
export struct ScreenFilterParameters final
{
    std::array<float,4> tint{1,1,1,1};
    // operation: 0 copy, 1 luminance tint, 2 multiply mask, 3 swap red/blue,
    // 4 per-channel normalized color transfer through a caller-owned 256x1 RGB curve.
    float operation=0;
    float fade=1;
    float vertex_alpha=0;
    float textured=1;
};
export struct ScreenFilterStyle final
{
    bool blend=false;
    RHIBlendFactor source=RHIBlendFactor::SourceAlpha;
    RHIBlendFactor destination=RHIBlendFactor::InverseSourceAlpha;
    std::uint8_t color_write_mask=15;
    bool operator==(const ScreenFilterStyle&) const = default;
};
export class ScreenFilterRenderer final
{
public:
    ~ScreenFilterRenderer() { Shutdown(); }
    bool Initialize(Device& device,const std::filesystem::path& directory)
    {
        if (m_device==&device && m_vertices.Is_Valid()) return true;
        Shutdown(); m_shaders=ShaderLibrary{};
        ShaderPrecompiledDesc shader;
        shader.program.vertex_shader=24; shader.program.fragment_shader=24;
        shader.program.source_key=0x53435246494C5431ull;
        shader.program.stages=ShaderStageMask::Vertex|ShaderStageMask::Pixel;
        shader.vertex_path=directory/"screen_filter.vso";
        shader.fragment_path=directory/"screen_filter.pso";
        m_shader=m_shaders.Load_Precompiled(shader);
        if (!m_shaders.Is_Loaded(m_shader)) return false;
        m_device=&device;
        m_vertices=device.Create_Buffer({4*sizeof(ScreenFilterVertex),RHIBufferUsage::Vertex,sizeof(ScreenFilterVertex)});
        m_constants=device.Create_Buffer({sizeof(ScreenFilterParameters),RHIBufferUsage::Constant});
        const std::array<std::uint32_t,6> indices{0,1,2,2,1,3};
        m_indices=device.Create_Buffer_Initialized({sizeof(indices),RHIBufferUsage::Index,sizeof(std::uint32_t)},std::as_bytes(std::span(indices)));
        if (!m_vertices.Is_Valid() || !m_constants.Is_Valid() || !m_indices.Is_Valid()) { Shutdown(); return false; }
        return true;
    }
    void Shutdown() noexcept
    {
        if (m_device) {
            for (auto& entry:m_pipelines) m_device->Destroy_Pipeline(entry.handle);
            for (auto handle:{m_vertices,m_indices,m_constants}) if(handle.Is_Valid()) m_device->Destroy_Buffer(handle);
        }
        m_pipelines.clear(); m_device=nullptr; m_vertices={}; m_indices={}; m_constants={};
    }
    bool Draw(CommandList& commands,std::span<const ScreenFilterVertex,4> vertices,
        const ScreenFilterParameters& parameters,const ScreenFilterStyle& style,
        RHITextureHandle texture,RHITextureHandle mask={})
    {
        if (!m_device || (parameters.textured>0.5f && !texture.Is_Valid())
            || ((parameters.operation==2 || parameters.operation==4) && !mask.Is_Valid())) return false;
        const auto pipeline=Pipeline(style);
        if (!pipeline.Is_Valid() || !m_device->Update_Buffer(m_vertices,0,std::as_bytes(vertices))
            || !m_device->Update_Buffer(m_constants,0,std::as_bytes(std::span(&parameters,1)))) return false;
        std::array<RHIBindlessResource,3> bindings{};
        bindings[0].type=RHIResourceType::Material; bindings[0].buffer=m_constants;
        std::size_t count=1;
        for (unsigned slot=0;slot<2;++slot) {
            const auto handle=slot==0 ? texture : mask;
            if (!handle.Is_Valid()) continue;
            bindings[count].type=RHIResourceType::Texture;
            bindings[count].index=ResourceIndex{slot,1}; bindings[count++].texture=handle;
        }
        return commands.Bind_Pipeline(pipeline)
            && commands.Set_Bindless_Resources(std::span(bindings.data(),count))
            && commands.Set_Vertex_Buffer(0,m_vertices,sizeof(ScreenFilterVertex),0)
            && commands.Set_Index_Buffer(m_indices,RHIIndexFormat::UInt32,0)
            && commands.Draw_Indexed(6,0);
    }
private:
    RHIPipelineHandle Pipeline(const ScreenFilterStyle& style)
    {
        for (const auto& entry:m_pipelines) if(entry.style==style) return entry.handle;
        RHIPipeline description;
        description.vertex_format=RHIVertexFormat::Position3Color4UV2UV2Normal3;
        description.depth_test=false; description.depth_write=false; description.cull_mode=RHICullMode::None;
        description.blend_mode=style.blend ? RHIBlendMode::Alpha : RHIBlendMode::Disabled;
        description.blend_alpha_like_color=true; description.custom_blend_factors=true;
        description.source_blend=style.source; description.destination_blend=style.destination;
        description.color_write_mask=style.color_write_mask; description.sampler_count=2;
        for (unsigned i=0;i<2;++i) description.samplers[i].address.fill(RHISamplerAddress::Clamp);
        const auto handle=m_device->Create_Pipeline(description,
            {m_shaders.Bytecode(m_shader,ShaderStage::Vertex)},{m_shaders.Bytecode(m_shader,ShaderStage::Pixel)});
        if (handle.Is_Valid()) m_pipelines.push_back({style,handle});
        return handle;
    }
    struct Entry { ScreenFilterStyle style; RHIPipelineHandle handle; };
    Device* m_device=nullptr;
    ShaderLibrary m_shaders;
    ShaderHandle m_shader{};
    RHIBufferHandle m_vertices{},m_indices{},m_constants{};
    std::vector<Entry> m_pipelines;
};
// Owns a frame-sized source and a normalized per-channel lookup curve. The
// caller draws its complete frame into Source_Target, then transfers it into
// its presentation target. Input policy and curve generation belong to callers.
export class ColorTransferFrame final {
public:
    using Curve=std::array<std::array<float,4>,256>;
    ~ColorTransferFrame() {Shutdown();}
    ColorTransferFrame()=default;
    ColorTransferFrame(const ColorTransferFrame&)=delete;
    ColorTransferFrame& operator=(const ColorTransferFrame&)=delete;
    bool Prepare(Device& device,unsigned width,unsigned height,const Curve& curve) {
        if(!width || !height || !std::ranges::all_of(curve,[](const auto& entry) {
            return std::ranges::all_of(entry,[](float x){return std::isfinite(x) && x>=0 && x<=1;});
        })) return false;
        if(m_device!=&device) {Shutdown();m_device=&device;}
        if(!m_source.Is_Valid() || m_width!=width || m_height!=height) {
            const auto texture=device.Create_Texture({width,height,1,RHITextureFormat::RGBA8_UNorm,
                static_cast<unsigned>(RHITextureUsage::RenderTarget)|static_cast<unsigned>(RHITextureUsage::ShaderResource)});
            if(!texture.Is_Valid()) return false;
            if(m_source.Is_Valid()) device.Destroy_Texture(m_source);
            m_source=texture;m_width=width;m_height=height;
        }
        if(!m_curve.Is_Valid() || curve!=m_values) {
            const auto texture=device.Create_Texture_Initialized({256,1,1,RHITextureFormat::RGBA32_Float},
                {std::as_bytes(std::span(curve)),256*16});
            if(!texture.Is_Valid()) return false;
            if(m_curve.Is_Valid()) device.Destroy_Texture(m_curve);
            m_curve=texture;m_values=curve;
        }
        return true;
    }
    RHITextureHandle Source_Target() const noexcept {return m_source;}
    bool Draw_Output(ScreenFilterRenderer& renderer,CommandList& commands,RHITextureHandle target,RHITextureHandle depth) {
        if(!m_source.Is_Valid() || !m_curve.Is_Valid() || !target.Is_Valid() || target==m_source) return false;
        std::array<ScreenFilterVertex,4> vertices{};
        vertices[0].position={1,-1,0};vertices[0].uv={1,1};vertices[1].position={1,1,0};vertices[1].uv={1,0};
        vertices[2].position={-1,-1,0};vertices[2].uv={0,1};vertices[3].position={-1,1,0};vertices[3].uv={0,0};
        ScreenFilterParameters parameters;parameters.operation=4;
        return commands.Set_Render_Targets(target,depth) && commands.Set_Viewport({0,0,m_width,m_height}) &&
            renderer.Draw(commands,vertices,parameters,{},m_source,m_curve);
    }
    void Shutdown() noexcept {
        if(m_device) for(const auto texture:{m_source,m_curve}) if(texture.Is_Valid()) m_device->Destroy_Texture(texture);
        m_device=nullptr;m_source={};m_curve={};m_width=m_height=0;
    }
private:
    Device* m_device{};RHITextureHandle m_source{},m_curve{};unsigned m_width{},m_height{};Curve m_values{};
};
namespace { ScreenFilterRenderer g_screen_filters; }
export ScreenFilterRenderer& Get_Screen_Filter_Renderer() noexcept { return g_screen_filters; }
}
