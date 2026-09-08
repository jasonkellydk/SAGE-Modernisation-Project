import Graphics.Materials.State;
#include <array>
#include "rts/profile.h"
#include "../../../../../../engine/graphics/profiling/Tracy.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
import Graphics.Materials.MeshMaterial;
import Graphics.Scene.Props.Material;
#include "GraphicsGeometry.h"
#include "GraphicsMaterial.h"
#include "Texture.h"
#include "WW3D.h"
import Graphics.Scene.DrawParameters;
import Graphics.Scene.Views.CameraMatrices;
import Graphics.Materials.TextureCoordinates;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Props.MaterialDrawState;
import Graphics.Backends.DX11.FrameRuntime;
import Graphics.Scene.Shadows.DirectionalRenderer;

bool Draw_Graphics_Prelit_Geometry(std::span<const Graphics::SurfaceVertex> source,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    Graphics::MaterialState shader, TextureClass* texture, const Matrix4x4* sorting_view, bool texture_luminance)
{
    std::vector<Graphics::PropVertex> vertices(source.size());
    for (std::size_t i=0; i<source.size(); ++i) {
        const auto& input = source[i];
        auto& output = vertices[i];
        output.position = input.position;
        output.uv = input.uv;
        output.color = input.color;
    }
    Graphics::PropParameters parameters;
    parameters.texture_luminance = texture_luminance ? 1.0f : 0.0f;
    return Draw_Graphics_Material_Geometry(vertices,indices,transform,shader,
        {texture,nullptr},parameters,sorting_view);
}

bool Draw_Graphics_Material_Geometry(std::span<const Graphics::PropVertex> vertices,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    Graphics::MaterialState shader, std::array<TextureClass*,2> source_textures,
    Graphics::PropParameters parameters, const Matrix4x4* sorting_view,
    GraphicsMaterialDrawOverrides overrides)
{
    PROFILER_SECTION_NAME("Graphics.Mesh.SubmitMaterial");
    auto* device = Graphics::Shared_Frame_Device();
    if (!device || vertices.empty() || indices.empty()) return false;
    const bool use_muzzle_flash = overrides.muzzle_flash != Graphics::MuzzleFlashDesignation::None
        && source_textures[0] != nullptr && shader.Get_Texturing()!=Graphics::MaterialState::TEXTURING_DISABLE;
    if (use_muzzle_flash)
        source_textures[1] = source_textures[0];
    std::array<Graphics::RHITextureHandle,2> textures{};
    const auto release_textures = [&]() {
        for (auto texture : textures)
            if (texture.Is_Valid()) device->Destroy_Texture(texture);
    };
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture || shader.Get_Texturing()==Graphics::MaterialState::TEXTURING_DISABLE) continue;
        if (!texture->Ensure_Render_Backend_Texture()) { release_textures(); return false; }
        const auto handle = texture->Peek_Graphics_Texture();
        if (!device->Retain_Texture(handle)) { release_textures(); return false; }
        textures[stage] = handle;
    }
    for (int row=0;row<4;++row)
        for (int column=0;column<4;++column)
            parameters.view_projection[row*4+column]=transform[row][column];
    parameters.textured=textures[0].Is_Valid() ? 1.0f : 0.0f;
    parameters.secondary_texture=textures[1].Is_Valid() ? 1.0f : 0.0f;
    const auto scene = Graphics::Get_Scene_Draw_Parameters();
    auto style = Graphics::Resolve_Prop_Material_State(shader,scene,WW3D::Is_Reflection_Render_Pass(),parameters);
    if (overrides.alpha_cutoff >= 0 && shader.Get_Alpha_Test() == Graphics::MaterialState::ALPHATEST_ENABLE)
        parameters.alpha_cutoff = overrides.alpha_cutoff;
    if (overrides.force_multiply) {
        style.source_blend = Graphics::RHIBlendFactor::DestinationColor;
        style.destination_blend = Graphics::RHIBlendFactor::SourceColor;
    }
    if (overrides.color_write_mask>=0) style.color_write_mask=static_cast<std::uint8_t>(overrides.color_write_mask);
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture) continue;
        style.samplers[stage] = Graphics::Resolve_Texture_Sampling(texture->Get_Sampling(),
            Graphics::Get_Texture_Sampling_Settings(), stage == 0);
    }
    if (use_muzzle_flash && textures[0].Is_Valid())
        Graphics::Prepare_Muzzle_Flash(parameters,style,overrides.muzzle_flash,WW3D::Get_Sync_Time());
    auto& renderer = Graphics::Get_Prop_Renderer();
    auto phase = Graphics::PropDrawPhase::Immediate;
    std::array<float,4> depth{};
    if (overrides.shadow_capture) phase = Graphics::PropDrawPhase::Shadow;
    else if (overrides.decal_pass) phase = Graphics::PropDrawPhase::Decal;
    else if (overrides.deferred_pass) phase = Graphics::PropDrawPhase::Material;
    else if (sorting_view) phase = Graphics::PropDrawPhase::Transparent;
    if (sorting_view) {
        depth = {(*sorting_view)[2][0],(*sorting_view)[2][1],
            (*sorting_view)[2][2],(*sorting_view)[2][3]};
    }
    const auto mesh = overrides.mesh.Is_Valid() ? overrides.mesh : renderer.Create_Mesh(vertices,indices);
    const bool drawn = Graphics::Get_Prop_Submission().Submit(mesh,style,parameters,textures,phase,depth);
    if (!overrides.mesh.Is_Valid()) renderer.Destroy_Mesh(mesh);
    if (!drawn) release_textures();
    return drawn;
}

void Extract_Graphics_Texture_Mappers(Graphics::PropParameters& parameters,
    const Graphics::MeshMaterial* material)
{
    GRAPHICS_PROFILE_FOCUS_SCOPE("Graphics.Mesh.TextureMappers");
    if (!material) return;
    for (unsigned stage=0;stage<parameters.uv_transform.size();++stage) {
        auto* mapper=material->mappings[stage].get();
        if (!mapper) continue;
        const auto& camera=Graphics::Get_Camera_Matrices();
        const auto mapping=mapper->Evaluate(WW3D::Get_Sync_Time(),camera.view.values,camera.projection.values);
        parameters.uv_transform[stage]=mapping.transform;
        parameters.uv_sources[stage*2]=static_cast<float>(mapping.coordinates.source);
        parameters.uv_sources[stage*2+1]=mapping.coordinates.projected ? 1.0f : 0.0f;
        if (mapping.bump) parameters.bump_matrix=*mapping.bump;
    }
}


