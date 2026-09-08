import Graphics.Materials.State;
#include <array>
#include <bit>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
import Assets.Adapters.W3D.Particles;
import Assets.Images.PixelEncoding;
import Graphics.Scene.Particles.EmitterTracks;

#include "W3DDevice/GameClient/W3DEmitterLoader.h"
#include "WW3D2/PartEmt.h"
#include "WW3D2/AssetMgr.h"
#include "WW3D2/Texture.h"
#include "WWLib/chunkio.h"
#include "WWLib/ref_ptr.h"

namespace {

// Borrowed constructor views own their backing arrays through construction.
template<class T> struct EmitterTrackView {
    ParticlePropertyStruct<T> property{};
    Graphics::EmitterTrackValues<T> data;

    template<class Track, class Convert>
    EmitterTrackView(const Track& track, Convert convert) : data(track,convert) {
        property = {data.start, data.random, static_cast<unsigned>(data.values.size()),
            data.times.data(), data.values.data()};
    }
};

std::unique_ptr<Vector3Randomizer> Create_Randomizer(const Assets::EmitterRandomizerDesc& data)
{
    const auto& d = data.dimensions;
    switch (data.kind) {
    case Assets::EmitterRandomizerKind::SolidBox:
        return std::make_unique<Vector3SolidBoxRandomizer>(Vector3(d.x,d.y,d.z));
    case Assets::EmitterRandomizerKind::SolidSphere:
        return std::make_unique<Vector3SolidSphereRandomizer>(d.x);
    case Assets::EmitterRandomizerKind::HollowSphere:
        return std::make_unique<Vector3HollowSphereRandomizer>(d.x);
    case Assets::EmitterRandomizerKind::SolidCylinder:
        return std::make_unique<Vector3SolidCylinderRandomizer>(d.x,d.y);
    default: return {};
    }
}

Graphics::MaterialState Create_Shader(const Assets::EmitterShaderDesc& data)
{
    Graphics::MaterialState shader;
    shader.Set_Depth_Compare(static_cast<Graphics::MaterialState::DepthCompareType>(data.depth_compare));
    shader.Set_Depth_Mask(data.depth_write ? Graphics::MaterialState::DEPTH_WRITE_ENABLE : Graphics::MaterialState::DEPTH_WRITE_DISABLE);
    shader.Set_Color_Mask(Graphics::MaterialState::COLOR_WRITE_ENABLE);
    shader.Set_Fog_Func(Graphics::MaterialState::FOG_DISABLE);
    shader.Set_Dst_Blend_Func(static_cast<Graphics::MaterialState::DstBlendFuncType>(data.destination_blend));
    switch (data.source_blend) {
    case Assets::EmitterBlendFactor::Zero: shader.Set_Src_Blend_Func(Graphics::MaterialState::SRCBLEND_ZERO); break;
    case Assets::EmitterBlendFactor::One: shader.Set_Src_Blend_Func(Graphics::MaterialState::SRCBLEND_ONE); break;
    case Assets::EmitterBlendFactor::SourceAlpha: shader.Set_Src_Blend_Func(Graphics::MaterialState::SRCBLEND_SRC_ALPHA); break;
    case Assets::EmitterBlendFactor::OneMinusSourceAlpha: shader.Set_Src_Blend_Func(Graphics::MaterialState::SRCBLEND_ONE_MINUS_SRC_ALPHA); break;
    default: WWASSERT(false); break;
    }
    shader.Set_Primary_Gradient(static_cast<Graphics::MaterialState::PriGradientType>(data.primary_gradient));
    shader.Set_Secondary_Gradient(static_cast<Graphics::MaterialState::SecGradientType>(data.secondary_gradient));
    shader.Set_Texturing(data.texturing ? Graphics::MaterialState::TEXTURING_ENABLE : Graphics::MaterialState::TEXTURING_DISABLE);
    shader.Set_Alpha_Test(data.alpha_test ? Graphics::MaterialState::ALPHATEST_ENABLE : Graphics::MaterialState::ALPHATEST_DISABLE);
    // The material pass consumes authored detail functions through these slots.
    shader.Set_Post_Detail_Color_Func(static_cast<Graphics::MaterialState::DetailColorFuncType>(data.detail_color_function));
    shader.Set_Post_Detail_Alpha_Func(static_cast<Graphics::MaterialState::DetailAlphaFuncType>(data.detail_alpha_function));
    return shader;
}

ParticleEmitterClass* Create_Emitter(const Assets::EmitterAssetDesc& data)
{
    auto position = Create_Randomizer(data.creation_volume);
    auto velocity = Create_Randomizer(data.velocity_random);
    if (!position || !velocity) return nullptr;
    auto* manager = WW3DAssetManager::Get_Instance();
    RefCountPtr<TextureClass> texture;
    if (!data.texture_name.empty())
        texture.Assign_No_Add_Ref(manager->Get_Texture(data.texture_name.c_str(),MIP_LEVELS_ALL,Assets::PixelEncoding::Unknown));
    Graphics::MaterialState shader = Create_Shader(data.shader);
    if (data.texture_blend_policy != Assets::EmitterTextureBlendPolicy::Authored) {
        shader = Graphics::MaterialState::AdditiveSprite();
        if (data.texture_blend_policy == Assets::EmitterTextureBlendPolicy::AlphaSpriteWhenTextureHasAlpha
            && texture.Peek() && Assets::Has_Explicit_Pixel_Alpha(texture->Get_Texture_Format()))
            shader = Graphics::MaterialState::AlphaSprite();
    }
    if (manager->Get_Activate_Fog_On_Load()) shader.Enable_Fog_For_Blend();
    EmitterTrackView<Vector3> color(data.color,[](const auto& v) { return Vector3(v.r,v.g,v.b); });
    const auto scalar = [](float v) { return v; };
    EmitterTrackView<float> opacity(data.opacity,scalar), size(data.size,scalar), rotation(data.rotation,scalar),
        frames(data.frame,scalar), blur(data.blur_time,scalar);
    const auto& line = data.line_properties;
    W3dEmitterLinePropertiesStruct line_properties{};
    line_properties.Flags = (line.merge_intersections ? 1u : 0u) | (line.freeze_random ? 2u : 0u)
        | (line.disable_sorting ? 4u : 0u) | (line.end_caps ? 8u : 0u)
        | (static_cast<unsigned>(line.texture_mapping) << 24);
    line_properties.SubdivisionLevel = line.subdivision_level;
    line_properties.NoiseAmplitude = line.noise_amplitude;
    line_properties.MergeAbortFactor = line.merge_abort_factor;
    line_properties.TextureTileFactor = line.texture_tile_factor;
    line_properties.UPerSec = line.uv_offset_rate.x;
    line_properties.VPerSec = line.uv_offset_rate.y;
    const std::array<int,5> render_modes{W3D_EMITTER_RENDER_MODE_TRI_PARTICLES,W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES,
        W3D_EMITTER_RENDER_MODE_LINE,W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA,W3D_EMITTER_RENDER_MODE_LINEGRP_PRISM};
    const auto mode = static_cast<unsigned>(data.geometry_mode);
    if (mode >= render_modes.size()) return nullptr;
    auto* emitter = NEW_REF(ParticleEmitterClass,(data.emission_rate,data.burst_size,position.get(),
        Vector3(data.velocity.x,data.velocity.y,data.velocity.z),velocity.get(),data.outward_velocity,
        data.velocity_inheritance,color.property,opacity.property,size.property,rotation.property,
        data.initial_orientation_random,frames.property,blur.property,
        Vector3(data.acceleration.x,data.acceleration.y,data.acceleration.z),data.lifetime,data.future_start_time,
        texture.Peek(),shader,static_cast<int>(data.max_emissions),0,false,render_modes[mode],
        static_cast<int>(std::countr_zero(data.atlas.columns)),&line_properties));
    position.release(); velocity.release();
    emitter->Set_Name(data.name.c_str());
    return emitter;
}
}

Graphics::ModelFactory<RenderObjClass>* Load_ParticleEmitter_Factory(ChunkLoadClass& source)
{
    std::vector<std::byte> bytes(source.Cur_Chunk_Length());
    if (source.Read(bytes.data(),static_cast<unsigned>(bytes.size())) != bytes.size()) return nullptr;
    Assets::EmitterAssetDesc description;
    std::string error;
    if (!Assets::W3D::W3DRead_Emitter(bytes,description,error)) return nullptr;
    auto data = std::make_shared<const Assets::EmitterAssetDesc>(std::move(description));
    return new Graphics::ModelFactory<RenderObjClass>(data->name,RenderObjClass::CLASSID_PARTICLEEMITTER,
        [data] { return Create_Emitter(*data); });
}
