/*
** Command & Conquer Generals Zero Hour(tm)
**
** Water material adapter. The water render object supplies
** geometry and material inputs; this class owns the explicit shader contract.
*/

#pragma once

#include <array>
#include <span>
#include <cstdint>
import Graphics.Scene.Water.Renderer;
import Engine.Core.Math.Vector4;
class W3DTextureHandle;
class W3DScene;
namespace Engine::Math { struct Matrix4; }

// Water draw input retains packed color until graphics extraction.
struct WaterSurfaceVertex
{
	float x;
	float y;
	float z;
	float nx;
	float ny;
	float nz;
	std::uint32_t diffuse;
	float u1;
	float v1;
	float u2;
	float v2;
};

struct WaterMaterialParameters
{
	Engine::Math::Vector4 shroud_projection;
	Engine::Math::Vector4 animation;
	Engine::Math::Vector4 camera_position;
	Engine::Math::Vector4 displacement_domain;
	Engine::Math::Vector4 tint;
	Engine::Math::Vector4 effects;
	Engine::Math::Vector4 surface_options;
};

class WaterMaterialClass
{
public:
	WaterMaterialClass();
	~WaterMaterialClass();

	WaterMaterialClass(const WaterMaterialClass &) = delete;
	WaterMaterialClass &operator=(const WaterMaterialClass &) = delete;

	bool Apply_Ocean(W3DTextureHandle *surface_texture,
		Graphics::RHITextureHandle displacement_texture,
		W3DTextureHandle *normal_texture, W3DTextureHandle *foam_texture,
		W3DTextureHandle *reflection_texture, Graphics::RHITextureHandle refraction_texture,
		W3DTextureHandle *environment_texture, W3DTextureHandle *shroud_texture,
		Graphics::RHITextureHandle scene_depth_texture,
		W3DTextureHandle *caustics_texture, W3DTextureHandle *depth_lut_texture,
		const WaterMaterialParameters &parameters, bool additive_blend);
	bool Apply_Surface(W3DTextureHandle *surface_texture,
		W3DTextureHandle *normal_texture, W3DTextureHandle *foam_texture,
		W3DTextureHandle *edge_texture, W3DTextureHandle *reflection_texture,
		Graphics::RHITextureHandle refraction_texture, W3DTextureHandle *environment_texture,
		W3DTextureHandle *shroud_texture, Graphics::RHITextureHandle scene_depth_texture,
		const WaterMaterialParameters &parameters, bool additive_blend);
	bool Apply_Track(W3DTextureHandle *wave_texture);
	bool Apply_Underwater(Graphics::RHITextureHandle scene_texture,
		Graphics::RHITextureHandle depth_texture, W3DTextureHandle *caustics_texture,
		W3DTextureHandle *depth_lut_texture, const WaterMaterialParameters &parameters);
	bool Apply_Sky(W3DTextureHandle *texture, bool alpha_blend,
		bool clamp_texture);

	void Shutdown();
	bool ReacquireResources();

    bool Draw(Graphics::WaterMeshHandle mesh, const Engine::Math::Matrix4& world, bool wireframe = false);
    bool Draw_Patches(Graphics::WaterMeshHandle mesh, const Graphics::OceanPatchGrid& grid);
    void Set_Frame_Lighting(W3DScene* scene);

private:
    void Set_Common_Constants(const WaterMaterialParameters& parameters);
    Graphics::WaterParameters m_parameters;
    Graphics::WaterStyle m_style;
    std::array<Graphics::RHITextureHandle,11> m_textures{};
};

bool Upload_Water_Geometry(Graphics::WaterMeshHandle& mesh,
    std::span<const WaterSurfaceVertex> vertices, std::span<const unsigned short> indices,
    bool triangle_strip = false);
