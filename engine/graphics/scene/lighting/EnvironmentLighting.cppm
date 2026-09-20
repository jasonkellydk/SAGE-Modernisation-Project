module;
#include "../../profiling/Tracy.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>
#include <span>

export module Graphics.Scene.Lighting.Environment;
export import Graphics.RHI;

namespace Graphics
{
export struct EnvironmentLocalLight final
{
    std::array<float,4> position_range{};
    // Linear radiant intensity; distance falloff is evaluated by the renderer.
    std::array<float,4> intensity{};
    std::array<float,4> direction_cone{0,0,-1,-1};
    std::array<float,4> spot{1,1,0,0};
};
// Frame input. The scene retains the cloud texture until its last draw and
// clears the input before releasing it. Height participates in the projection.
export struct EnvironmentLightingParameters final
{
    std::array<float,4> cloud_multiplier{};
    std::array<float,4> cloud_offset_strength{};
    std::array<float,4> ambient{};
    std::array<float,4> clip_plane{};
    std::array<std::array<float,16>,4> shadow_view_projection{};
    std::array<float,4> shadow_splits{};
    std::array<float,4> shadow_view_depth{};
    // Cascade count, constant depth bias, slope bias, cascade blend fraction.
    std::array<float,4> shadow_options{0,0.0005f,1.5f,0.1f};
    std::array<float,4> camera{};
    std::array<float,4> sun_direction{0,0,1,0};
    std::array<float,4> sun_radiance{};
    std::array<float,4> sky_radiance{.32f,.48f,.8f,0};
    std::array<float,4> ground_radiance{.12f,.1f,.07f,0};
    // Modern materials, linear HDR output, environment strength, exposure.
    std::array<float,4> pbr_options{0,0,1,1};
    std::array<std::array<float,4>,2> secondary_sun_direction{};
    std::array<std::array<float,4>,2> secondary_sun_radiance{};
    std::array<float,4> local_light_options{};
    std::array<std::array<float,4>,64> local_positions{};
    std::array<std::array<float,4>,64> local_diffuse{};
    std::array<std::array<float,4>,64> local_ambient{};
    // XYZ points away from the light; W is outer cone cosine (-1 for point).
    std::array<std::array<float,4>,64> local_direction{};
    std::array<std::array<float,4>,64> local_spot{};
    // Atlas tile size, tiles per row, atlas width, atlas height.
    std::array<float,4> local_shadow_atlas{};
};
static_assert(sizeof(EnvironmentLightingParameters) == 5680);

export struct EnvironmentLightingState final
{
    EnvironmentLightingParameters parameters;
    RHITextureHandle cloud_texture{};
    std::array<RHITextureHandle,4> shadow_textures{};
    RHITextureHandle local_shadow_texture{};
};

export EnvironmentLightingState& Get_Environment_Lighting() noexcept
{
    static EnvironmentLightingState state;
    return state;
}

// Each renderer owns its constant buffer. Shared scene inputs do not retain a
// device pointer or own imported textures across renderer/device restarts.
export class EnvironmentLightingBinding final
{
public:
    bool Initialize(Device& device)
    {
        m_parameters_uploaded = false;
        m_constants = device.Create_Buffer({sizeof(EnvironmentLightingParameters), RHIBufferUsage::Constant});
        return m_constants.Is_Valid();
    }

    void Shutdown(Device& device) noexcept
    {
        if (m_constants.Is_Valid()) device.Destroy_Buffer(m_constants);
        if (m_prefiltered.Is_Valid()) device.Destroy_Texture(m_prefiltered);
        m_prefiltered = {};
        m_constants = {};
        m_parameters_uploaded = false;
    }

    bool Bind(Device& device, CommandList& commands)
    {
        std::array<RHIBindlessResource,8> resources{};
        unsigned count=0;
        return Prepare_Resources(device, resources, count)
            && commands.Set_Bindless_Resources(std::span(resources.data(),count));
    }

    bool Prepare_Resources(Device& device, std::span<RHIBindlessResource> resources, unsigned& count)
    {
        count=0;
        if (resources.size()<8) return false;
        for (auto& resource : resources.first(8)) resource={};
        GRAPHICS_PROFILE_SCOPE("Graphics.Environment.Bind");
        const auto& state = Get_Environment_Lighting();
        auto parameters = state.parameters;
        if (!state.local_shadow_texture.Is_Valid()) parameters.local_shadow_atlas={};
        if (parameters.pbr_options[0] > .5f && !Prefilter(device)) return false;
        if (!state.cloud_texture.Is_Valid()) parameters.cloud_offset_strength[3] = 0;
        const auto cascade_count = static_cast<unsigned>(parameters.shadow_options[0]);
        if (cascade_count > state.shadow_textures.size()) return false;
        for (unsigned cascade=0;cascade<cascade_count;++cascade)
            if (!state.shadow_textures[cascade].Is_Valid()) return false;
        // This binding exclusively owns its buffer. Keep the uploaded bytes
        // across draws, but still resolve and bind current texture generations.
        if (!m_parameters_uploaded || std::memcmp(&m_parameters,&parameters,sizeof(parameters)) != 0) {
            if (!device.Update_Buffer(m_constants, 0, std::as_bytes(std::span(&parameters,1)))) return false;
            m_parameters = parameters;
            m_parameters_uploaded = true;
        }
        resources[0].type = RHIResourceType::Material;
        resources[0].constant_buffer_slot = 7;
        resources[0].buffer = m_constants;
        count = 1;
        if (parameters.local_shadow_atlas[0]>0) {
            resources[count].type=RHIResourceType::Texture;
            resources[count].index=ResourceIndex{17,1};
            resources[count++].texture=state.local_shadow_texture;
        }
        if (parameters.pbr_options[0] > .5f) {
            resources[count].type = RHIResourceType::Texture;
            resources[count].index = ResourceIndex{16,1};
            resources[count++].texture = m_prefiltered;
        }
        if (parameters.cloud_offset_strength[3] > 0) {
            resources[count].type = RHIResourceType::Texture;
            resources[count].index = ResourceIndex{15,1};
            resources[count++].texture = state.cloud_texture;
        }
        for (unsigned cascade=0;cascade<cascade_count;++cascade) {
            resources[count].type = RHIResourceType::Texture;
            resources[count].index = ResourceIndex{11+cascade,1};
            resources[count++].texture = state.shadow_textures[cascade];
        }
        return true;
    }

private:
    // GGX-filtered sky/ground basis. Linear combinations of the basis remain
    // valid for any sky colour, so time-of-day changes never rebuild the probe.
    bool Prefilter(Device& device)
    {
        if (m_prefiltered.Is_Valid()) return true;
        constexpr unsigned width=64,height=32,levels=7,samples=64;
        if (!m_prefiltered.Is_Valid())
            m_prefiltered=device.Create_Texture({width,height,levels,RHITextureFormat::RGBA32_Float});
        if (!m_prefiltered.Is_Valid()) return false;
        constexpr float pi=3.14159265358979323846f;
        for (unsigned mip=0;mip<levels;++mip) {
            const unsigned w=std::max(1u,width>>mip),h=std::max(1u,height>>mip);
            std::vector<std::array<float,4>> pixels(w*h);
            const float roughness=float(mip)/float(levels-1),alpha=std::max(.001f,roughness*roughness);
            for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
                const float longitude=((x+.5f)/w-.5f)*2*pi,latitude=(.5f-(y+.5f)/h)*pi;
                const std::array n{std::cos(latitude)*std::cos(longitude),std::cos(latitude)*std::sin(longitude),std::sin(latitude)};
                const std::array t{-std::sin(longitude),std::cos(longitude),0.0f};
                const std::array b{-n[2]*std::cos(longitude),-n[2]*std::sin(longitude),std::cos(latitude)};
                auto& color=pixels[y*w+x]; float weight=0;
                for (unsigned i=0;i<samples;++i) {
                    unsigned bits=i; float radical=0,scale=.5f;
                    for (;bits;bits>>=1,scale*=.5f) radical+=(bits&1)*scale;
                    const float phi=2*pi*(i+.5f)/samples;
                    const float ct=std::sqrt((1-radical)/(1+(alpha*alpha-1)*radical)),st=std::sqrt(std::max(0.0f,1-ct*ct));
                    const float hz=t[2]*std::cos(phi)*st+b[2]*std::sin(phi)*st+n[2]*ct;
                    const float lz=2*ct*hz-n[2],nl=std::max(0.0f,2*ct*ct-1);
                    // Broad horizon haze and a dim ground bounce form the probe.
                    const float sky=std::clamp(lz*.5f+.5f,0.0f,1.0f);
                    const float horizon=std::pow(1-std::abs(lz),4.0f);
                    // Store the two linear basis weights, not lit RGB. Animated
                    // sky colours are applied by the shader without re-filtering
                    // or uploading seven texture levels during a frame.
                    color[0]+=(sky+horizon*.1f)*nl;
                    color[1]+=(1-sky)*nl;
                    weight+=nl;
                }
                for(unsigned c=0;c<3;++c) color[c]/=std::max(weight,.0001f);
                color[3]=1;
            }
            if (!device.Update_Texture(m_prefiltered,{std::as_bytes(std::span(pixels)),w*16,w*h*16,mip})) return false;
        }
        return true;
    }

public:
    // Effects use their entire texture table. Only publish the shared colour
    // and lighting constants; never overwrite particle or beam texture slots.
    bool Bind_Constants(Device& device, CommandList& commands)
    {
        const auto& parameters=Get_Environment_Lighting().parameters;
        if (!m_parameters_uploaded || std::memcmp(&m_parameters,&parameters,sizeof(parameters))!=0) {
            if (!device.Update_Buffer(m_constants,0,std::as_bytes(std::span(&parameters,1)))) return false;
            m_parameters=parameters; m_parameters_uploaded=true;
        }
        RHIBindlessResource resource;
        resource.type=RHIResourceType::Material; resource.constant_buffer_slot=7; resource.buffer=m_constants;
        return commands.Set_Bindless_Resources(std::span(&resource,1));
    }
private:
    RHITextureHandle m_prefiltered{};
    RHIBufferHandle m_constants{};
    EnvironmentLightingParameters m_parameters{};
    bool m_parameters_uploaded = false;
};
}
