import Graphics.Materials.State;
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
import Assets.Adapters.W3D.Particles;
import Assets.Images.PixelEncoding;
import Engine.Core.Math.RandomStream;
import Engine.Core.Math.RandomVector3Generator;
import Engine.Core.Math.Vector3;

#include "W3DDevice/GameClient/W3DEmitterLoader.h"
#include "W3DDevice/GameClient/W3DEmitterRenderObject.h"
#include "W3DDevice/GameClient/W3DAssetCatalog.h"
#include "W3DDevice/GameClient/W3DTextureHandle.h"
#include "WWLib/chunkio.h"
#include "WWLib/ref_ptr.h"
import engine.debug;

namespace {

std::uint64_t Stable_Seed(std::string_view text) noexcept
{
	std::uint64_t hash = 14695981039346656037ull;
	for (const unsigned char value : text) {
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash;
}

std::optional<Engine::Math::RandomVector3Generator> Create_Randomizer(
	const Assets::EmitterRandomizerDesc& data, std::uint64_t seed, std::uint64_t sequence)
{
    const auto& d = data.dimensions;
    const Engine::Math::Vector3 dimensions{d.x, d.y, d.z};
    const auto make = [&](Engine::Math::Vector3Distribution distribution, Engine::Math::Vector3 values) {
        return Engine::Math::RandomVector3Generator(distribution, values,
            Engine::Math::RandomStream::Derive_Seed(seed, sequence));
    };
    switch (data.kind) {
    case Assets::EmitterRandomizerKind::SolidBox:
        return make(Engine::Math::Vector3Distribution::Box, dimensions);
    case Assets::EmitterRandomizerKind::SolidSphere:
        return make(Engine::Math::Vector3Distribution::SolidSphere, {d.x, 0, 0});
    case Assets::EmitterRandomizerKind::HollowSphere:
        return make(Engine::Math::Vector3Distribution::SphereSurface, {d.x, 0, 0});
    case Assets::EmitterRandomizerKind::SolidCylinder:
        return make(Engine::Math::Vector3Distribution::Cylinder, {d.x, d.y, 0});
    default: return std::nullopt;
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
    default: engine::debug::assert_condition((false), "false", __FILE__, __LINE__, "assertion failed"); break;
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

W3DEmitterRenderObject* Create_Emitter(const Assets::EmitterAssetDesc& data)
{
    const std::uint64_t seed = Stable_Seed(data.name);
    auto position = Create_Randomizer(data.creation_volume, seed, 0);
    auto velocity = Create_Randomizer(data.velocity_random, seed, 1);
    if (!position || !velocity) return nullptr;
	    auto* catalog = W3DAssetCatalog::Get_Instance();
    RefCountPtr<W3DTextureHandle> texture;
	if (catalog != nullptr && !data.texture_name.empty())
	        texture.Assign_No_Add_Ref(catalog->Get_Texture(data.texture_name.c_str(),MIP_LEVELS_ALL,Assets::PixelEncoding::Unknown));
    Graphics::MaterialState shader = Create_Shader(data.shader);
    if (data.texture_blend_policy != Assets::EmitterTextureBlendPolicy::Authored) {
        shader = Graphics::MaterialState::AdditiveSprite();
        if (data.texture_blend_policy == Assets::EmitterTextureBlendPolicy::AlphaSpriteWhenTextureHasAlpha
            && texture.Peek() && Assets::Has_Explicit_Pixel_Alpha(texture->Get_Texture_Format()))
            shader = Graphics::MaterialState::AlphaSprite();
    }
	    if (catalog != nullptr && catalog->Get_Fog_On_Load()) shader.Enable_Fog_For_Blend();
    auto* emitter = new W3DEmitterRenderObject(data, texture.Peek(), shader,
        std::move(*position), std::move(*velocity));
    emitter->Set_Name(data.name.c_str());
    return emitter;
}
}

Graphics::ModelFactory<W3DRenderObject>* Load_ParticleEmitter_Factory(ChunkLoadClass& source)
{
    std::vector<std::byte> bytes(source.Cur_Chunk_Length());
    if (source.Read(bytes.data(),static_cast<unsigned>(bytes.size())) != bytes.size()) return nullptr;
    Assets::EmitterAssetDesc description;
    std::string error;
    if (!Assets::W3D::W3DRead_Emitter(bytes,description,error)) return nullptr;
    auto data = std::make_shared<const Assets::EmitterAssetDesc>(std::move(description));
    return new Graphics::ModelFactory<W3DRenderObject>(data->name,W3DRenderObject::CLASSID_PARTICLEEMITTER,
        [data] { return Create_Emitter(*data); });
}
