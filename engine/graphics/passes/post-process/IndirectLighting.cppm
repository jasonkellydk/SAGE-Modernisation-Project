module;
#include <array>
#include <algorithm>
#include <filesystem>
#include <span>

export module Graphics.Passes.IndirectLighting;
import Graphics.RHI;
import Graphics.FrameTargets;
import Graphics.Resources.Textures.Snapshot;
import Graphics.Shaders.Library;

namespace Graphics {
export struct IndirectLightingInput {
    std::array<float,16> projection{},inverse_projection{},view{};
    float radius=64.f,occlusion_radius=12.f,bias=.35f,strength=1.f;
};

// Forward material buffers plus one screen-space diffuse bounce. Resources are
// rebuilt on resize and cleared each frame; no stale map or camera history is used.
export class IndirectLightingRenderer final {
public:
    ~IndirectLightingRenderer(){Shutdown();}
    bool Initialize(Device& device,const std::filesystem::path& directory) {
        if(m_device==&device) return true;
        Shutdown();m_device=&device;
        ShaderLibrary library;ShaderPrecompiledDesc shader;
        shader.program.vertex_shader=shader.program.fragment_shader=48;
        shader.program.source_key=0x494e444952454354ull;
        shader.program.stages=ShaderStageMask::Vertex|ShaderStageMask::Pixel;
        shader.vertex_path=directory/"indirect_lighting.vso";
        shader.fragment_path=directory/"indirect_lighting.pso";
        const auto loaded=library.Load_Precompiled(shader);
        if(!library.Is_Loaded(loaded)){Shutdown();return false;}
        RHIPipeline pipeline;pipeline.depth_test=pipeline.depth_write=false;
        pipeline.cull_mode=RHICullMode::None;
        pipeline.samplers[0].address.fill(RHISamplerAddress::Clamp);
        m_pipeline=device.Create_Pipeline(pipeline,{library.Bytecode(loaded,ShaderStage::Vertex)},
            {library.Bytecode(loaded,ShaderStage::Pixel)});
        const std::array<Vertex,3> vertices{{{{-1,1,0},{1,1,1,1},{0,0}},
            {{3,1,0},{1,1,1,1},{2,0}},{{-1,-3,0},{1,1,1,1},{0,2}}}};
        m_vertices=device.Create_Buffer_Initialized({sizeof(vertices),RHIBufferUsage::Vertex,sizeof(Vertex)},std::as_bytes(std::span(vertices)));
        m_constants=device.Create_Buffer({sizeof(Parameters),RHIBufferUsage::Constant});
        if(!m_pipeline.Is_Valid() || !m_vertices.Is_Valid() || !m_constants.Is_Valid()){Shutdown();return false;}
        return true;
    }
    void Shutdown() noexcept {
        m_color.Shutdown();m_depth.Shutdown();
        if(m_device) {
            Release_Targets();
            if(m_pipeline.Is_Valid())m_device->Destroy_Pipeline(m_pipeline);
            if(m_vertices.Is_Valid())m_device->Destroy_Buffer(m_vertices);
            if(m_constants.Is_Valid())m_device->Destroy_Buffer(m_constants);
        }
        m_device=nullptr;m_pipeline={};m_vertices={};m_constants={};
    }
    std::array<RHITextureHandle,7> Begin_Frame(unsigned width,unsigned height) {
        std::array<RHITextureHandle,7> result{};
        if(!m_device || !width || !height) return result;
        if(width!=m_width || height!=m_height) {
            std::array<RHITextureHandle,4> created{};
            for(unsigned i=0;i<4;++i) {
                const auto format=i==0 ? RHITextureFormat::RGBA8_UNorm : RHITextureFormat::RGBA16_Float;
                created[i]=m_device->Create_Texture({i==3 ? (width+1)/2 : width,
                    i==3 ? (height+1)/2 : height,1,format,
                    static_cast<unsigned>(RHITextureUsage::RenderTarget)|static_cast<unsigned>(RHITextureUsage::ShaderResource)});
                if(!created[i].Is_Valid()) {
                    for(const auto handle:created)if(handle.Is_Valid())m_device->Destroy_Texture(handle);
                    return result;
                }
            }
            Release_Targets();m_targets=created;m_width=width;m_height=height;
        }
        auto& commands=m_device->Immediate_Command_List();
        for(unsigned i=0;i<3;++i) {
            if(!commands.Clear_Color_Target(m_targets[i],{0,0,0,0})) return {};
            result[i]=m_targets[i];
        }
        return result;
    }
    bool Render(CommandList& commands,const FrameTargets& targets,const IndirectLightingInput& input,
        RHITextureFormat depth_format=RHITextureFormat::D24_UNorm_S8,
        RHITextureFormat color_format=RHITextureFormat::RGBA16_Float) {
        if(!m_device || targets.backbuffer.width!=m_width || targets.backbuffer.height!=m_height) return false;
        if(!commands.Reset_State()
            || !m_color.Capture(*m_device,commands,targets.backbuffer.texture,m_width,m_height,color_format)
            || !m_depth.Capture(*m_device,commands,targets.depth.texture,m_width,m_height,depth_format)) return false;
        Parameters parameters;parameters.input=input;
        bool drawn=Draw(commands,m_targets[3],(m_width+1)/2,(m_height+1)/2,parameters);
        parameters.operation=1;
        drawn=drawn && Draw(commands,targets.backbuffer.texture,m_width,m_height,parameters);
        const bool restored=commands.Reset_State() && commands.Set_Render_Targets(targets.backbuffer.texture,targets.depth.texture)
            && commands.Set_Viewport({0,0,m_width,m_height});
        return drawn && restored;
    }
private:
    struct Vertex {std::array<float,3> position;std::array<float,4> color;std::array<float,2> uv;};
    struct Parameters {IndirectLightingInput input;unsigned operation=0;std::array<float,3> padding{};};
    static_assert(sizeof(Parameters)==224);
    void Release_Targets() {
        for(const auto handle:m_targets)if(handle.Is_Valid())m_device->Destroy_Texture(handle);
        m_targets={};m_width=m_height=0;
    }
    bool Draw(CommandList& commands,RHITextureHandle target,unsigned width,unsigned height,const Parameters& parameters) {
        if(!m_device->Update_Buffer(m_constants,0,std::as_bytes(std::span(&parameters,1)))) return false;
        std::array<RHIBindlessResource,7> bindings{};
        bindings[0].type=RHIResourceType::Material;bindings[0].buffer=m_constants;
        const std::array textures{m_color.Texture(),m_depth.Texture(),m_targets[0],m_targets[1],m_targets[2],m_targets[3]};
        const unsigned count=parameters.operation==0 ? 5 : 6;
        for(unsigned i=0;i<count;++i) {
            bindings[i+1].type=RHIResourceType::Texture;bindings[i+1].index=ResourceIndex{i,1};bindings[i+1].texture=textures[i];
        }
        return commands.Reset_State() && commands.Set_Color_Target(target) && commands.Set_Viewport({0,0,width,height})
            && commands.Bind_Pipeline(m_pipeline) && commands.Set_Bindless_Resources(std::span(bindings.data(),count+1))
            && commands.Set_Vertex_Buffer(0,m_vertices,sizeof(Vertex),0) && commands.Draw(3,0,1,0);
    }
    Device* m_device=nullptr;
    RHIPipelineHandle m_pipeline{};
    RHIBufferHandle m_vertices{},m_constants{};
    std::array<RHITextureHandle,4> m_targets{};
    TextureSnapshot m_color,m_depth;
    unsigned m_width=0,m_height=0;
};
export IndirectLightingRenderer& Get_Indirect_Lighting_Renderer(){static IndirectLightingRenderer renderer;return renderer;}
}
