#include <array>
#include <span>
#include <vector>
#include "W3DDevice/GameClient/WaterMaterial.h"
#include "W3DDevice/GameClient/W3DGraphicsResources.h"
#include "WW3D2/WW3D.h"
#include "WW3D2/RendObj.h"
#include "WW3D2/Scene.h"
#include "WWMath/matrix4.h"
#include <algorithm>
import Graphics.Scene.Views.CameraMatrices;
import Graphics.Scene.DrawParameters;
import Graphics.Backends.DX11.FrameRuntime;

namespace {
std::array<float,4> Copy_Vector(const Vector4& value) { return {value.X,value.Y,value.Z,value.W}; }
}
WaterMaterialClass::WaterMaterialClass() = default;
WaterMaterialClass::~WaterMaterialClass() = default;
void WaterMaterialClass::Set_Common_Constants(const WaterMaterialParameters& parameters)
{
    m_parameters.shroud_projection = Copy_Vector(parameters.shroud_projection);
    m_parameters.animation = Copy_Vector(parameters.animation);
    m_parameters.camera_position = Copy_Vector(parameters.camera_position);
    m_parameters.displacement_domain = Copy_Vector(parameters.displacement_domain);
    m_parameters.tint = Copy_Vector(parameters.tint);
    m_parameters.effects = Copy_Vector(parameters.effects);
    m_parameters.surface_options = Copy_Vector(parameters.surface_options);
}
void WaterMaterialClass::Set_Fog(SceneClass* scene)
{
    m_parameters.fog_state = {};
    if (!scene) return;
    scene->Get_Fog_Range(&m_parameters.fog_state[0],&m_parameters.fog_state[1]);
    m_parameters.fog_state[2] = Graphics::Get_Scene_Draw_Parameters().fog.enabled ? 1.0f : 0.0f;
    const auto& color = scene->Get_Fog_Color();
    m_parameters.fog_color = {color.X,color.Y,color.Z,1};
}
bool WaterMaterialClass::Apply_Ocean(TextureBaseClass *surface_texture,
	TextureBaseClass *displacement_texture,
	TextureBaseClass *normal_texture, TextureBaseClass *foam_texture,
	TextureBaseClass *reflection_texture, Graphics::RHITextureHandle refraction_texture,
	TextureBaseClass *environment_texture, TextureBaseClass *shroud_texture,
	Graphics::RHITextureHandle scene_depth_texture,
	const WaterMaterialParameters &parameters, bool additive_blend)
{
	if (!Graphics::Shared_Frame_Device())
	{
		return false;
	}

	m_style.pass = Graphics::WaterPass::Ocean;
	m_textures[0] = Resolve_Graphics_Texture(surface_texture);
	m_textures[1] = Resolve_Graphics_Texture(displacement_texture);
	m_textures[2] = Resolve_Graphics_Texture(normal_texture);
	m_textures[3] = Resolve_Graphics_Texture(foam_texture);
	m_textures[4] = Resolve_Graphics_Texture(reflection_texture);
	m_textures[5] = refraction_texture;
	m_textures[6] = Resolve_Graphics_Texture(environment_texture);
	m_textures[7] = Resolve_Graphics_Texture(shroud_texture);
	m_textures[8] = scene_depth_texture;
	Set_Common_Constants(parameters);
	m_style.blend = additive_blend ? Graphics::RHIBlendMode::Additive : Graphics::RHIBlendMode::Alpha;
    m_style.clamp_texture = false;
	return true;
}

bool WaterMaterialClass::Apply_Displacement(
	TextureBaseClass *static_displacement_texture,
	const Vector4 &animation, const Vector4 &displacement_domain)
{
	if (!Graphics::Shared_Frame_Device())
	{
		return false;
	}

	m_style.pass = Graphics::WaterPass::Displacement;
	for (unsigned stage = 0; stage < 8; ++stage)
	{
		m_textures[stage] = Resolve_Graphics_Texture(stage == 0 ? static_displacement_texture : nullptr);
	}
    m_parameters.animation = Copy_Vector(animation);
    m_parameters.displacement_domain = Copy_Vector(displacement_domain);
	m_style.blend = Graphics::RHIBlendMode::Disabled;
    m_style.clamp_texture = false;
	return true;
}

bool WaterMaterialClass::Apply_Surface(TextureBaseClass *surface_texture,
	TextureBaseClass *normal_texture, TextureBaseClass *foam_texture,
	TextureBaseClass *edge_texture, TextureBaseClass *reflection_texture,
	Graphics::RHITextureHandle refraction_texture, TextureBaseClass *environment_texture,
	TextureBaseClass *shroud_texture, Graphics::RHITextureHandle scene_depth_texture,
	const WaterMaterialParameters &parameters, bool additive_blend)
{
	if (!Graphics::Shared_Frame_Device())
	{
		return false;
	}

	m_style.pass = Graphics::WaterPass::Surface;
	m_textures[0] = Resolve_Graphics_Texture(surface_texture);
	m_textures[1] = Resolve_Graphics_Texture(normal_texture);
	m_textures[2] = Resolve_Graphics_Texture(foam_texture);
	m_textures[3] = Resolve_Graphics_Texture(edge_texture);
	m_textures[4] = Resolve_Graphics_Texture(reflection_texture);
	m_textures[5] = refraction_texture;
	m_textures[6] = Resolve_Graphics_Texture(environment_texture);
	m_textures[7] = Resolve_Graphics_Texture(shroud_texture);
	m_textures[8] = scene_depth_texture;
	Set_Common_Constants(parameters);
	m_style.blend = additive_blend ? Graphics::RHIBlendMode::Additive : Graphics::RHIBlendMode::Alpha;
    m_style.clamp_texture = false;
	return true;
}

bool WaterMaterialClass::Apply_Track(TextureBaseClass *wave_texture)
{
	if (!Graphics::Shared_Frame_Device())
	{
		return false;
	}

	m_style.pass = Graphics::WaterPass::Track;
	for (unsigned stage = 0; stage < 8; ++stage)
	{
		m_textures[stage] = Resolve_Graphics_Texture(stage == 0 ? wave_texture : nullptr);
	}
	m_style.blend = Graphics::RHIBlendMode::Alpha;
    m_style.clamp_texture = false;
	return true;
}

bool WaterMaterialClass::Apply_Sky(TextureBaseClass *texture, bool alpha_blend,
	bool clamp_texture)
{
	if (!Graphics::Shared_Frame_Device())
	{
		return false;
	}

	m_style.pass = Graphics::WaterPass::Sky;
	for (unsigned stage = 0; stage < 8; ++stage)
	{
		m_textures[stage] = Resolve_Graphics_Texture(stage == 0 ? texture : nullptr);
	}
	m_style.blend = alpha_blend ? Graphics::RHIBlendMode::Alpha : Graphics::RHIBlendMode::Disabled;
    m_style.clamp_texture = clamp_texture;
	return true;
}


void WaterMaterialClass::Shutdown() { m_textures = {}; }
bool WaterMaterialClass::ReacquireResources() { return Graphics::Shared_Frame_Device() != nullptr; }

bool WaterMaterialClass::Draw(Graphics::WaterMeshHandle mesh, const Matrix4x4& world, bool wireframe)
{
    auto* device = Graphics::Shared_Frame_Device();
    if (!device) return false;
    std::copy_n(&world[0][0], 16, m_parameters.world.data());
    m_parameters.view = Graphics::Get_Camera_Matrices().view.values;
    m_parameters.projection = Graphics::Get_Camera_Matrices().projection.values;
    auto style = m_style;
    style.wireframe = wireframe;
    return Graphics::Get_Water_Renderer().Draw(device->Immediate_Command_List(),mesh,style,m_parameters,m_textures);
}

bool Upload_Water_Geometry(Graphics::WaterMeshHandle& mesh,
    std::span<const WaterSurfaceVertex> source, std::span<const unsigned short> source_indices, bool triangle_strip)
{
    std::vector<Graphics::WaterVertex> vertices;
    vertices.reserve(source.size());
    for (const auto& input : source) {
        Graphics::WaterVertex vertex;
        vertex.position = {input.x,input.y,input.z};
        vertex.normal = {input.nx,input.ny,input.nz};
        vertex.uv = {input.u1,input.v1};
        vertex.secondary_uv = {input.u2,input.v2};
        vertex.color = {((input.diffuse>>16)&255)/255.0f,((input.diffuse>>8)&255)/255.0f,
            (input.diffuse&255)/255.0f,((input.diffuse>>24)&255)/255.0f};
        vertices.push_back(vertex);
    }
    std::vector<std::uint32_t> indices(source_indices.begin(),source_indices.end());
    if (triangle_strip) indices = Graphics::Expand_Water_Strip(indices);
    auto& renderer = Graphics::Get_Water_Renderer();
    if (mesh.Is_Valid()) return renderer.Update_Mesh(mesh,vertices,indices);
    mesh = renderer.Create_Mesh(vertices,indices);
    return mesh.Is_Valid();
}

