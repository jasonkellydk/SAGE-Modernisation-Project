#include <array>
#include "rts/profile.h"
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "VertMaterial.h"
#include "Mapper.h"
#include "GraphicsGeometry.h"
#include "GraphicsMaterial.h"
#include "Texture.h"
#include "WW3D.h"
import Graphics.Scene.DrawParameters;
import Graphics.Scene.Props.Submission;
import Graphics.Materials.Fog;
import Graphics.Backends.DX11.FrameRuntime;
import Graphics.Scene.Shadows.DirectionalRenderer;

bool Draw_Graphics_Prelit_Geometry(std::span<const VertexFormatXYZDUV1> source,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    ShaderClass shader, TextureClass* texture, const Matrix4x4* sorting_view, bool texture_luminance)
{
    std::vector<Graphics::PropVertex> vertices(source.size());
    for (std::size_t i=0; i<source.size(); ++i) {
        const auto& input = source[i];
        auto& output = vertices[i];
        output.position = {input.x,input.y,input.z};
        output.uv = {input.u1,input.v1};
        output.color = {((input.diffuse>>16)&255)/255.0f,((input.diffuse>>8)&255)/255.0f,
            (input.diffuse&255)/255.0f,((input.diffuse>>24)&255)/255.0f};
    }
    Graphics::PropParameters parameters;
    parameters.texture_luminance = texture_luminance ? 1.0f : 0.0f;
    return Draw_Graphics_Material_Geometry(vertices,indices,transform,shader,
        {texture,nullptr},parameters,sorting_view);
}

bool Draw_Graphics_Material_Geometry(std::span<const Graphics::PropVertex> vertices,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    ShaderClass shader, std::array<TextureClass*,2> source_textures,
    Graphics::PropParameters parameters, const Matrix4x4* sorting_view,
    GraphicsMaterialDrawOverrides overrides)
{
    PROFILER_SECTION_NAME("Graphics.Mesh.SubmitMaterial");
    auto* device = Graphics::Shared_Frame_Device();
    if (!device || vertices.empty() || indices.empty()) return false;
    std::array<Graphics::RHITextureHandle,2> textures{};
    const auto release_textures = [&]() {
        for (auto texture : textures)
            if (texture.Is_Valid()) device->Destroy_Texture(texture);
    };
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture || shader.Get_Texturing()==ShaderClass::TEXTURING_DISABLE) continue;
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
    parameters.primary_gradient=static_cast<float>(shader.Get_Primary_Gradient());
    parameters.secondary_gradient=static_cast<float>(shader.Get_Secondary_Gradient());
    parameters.detail_color=static_cast<float>(shader.Get_Post_Detail_Color_Func());
    parameters.detail_alpha=static_cast<float>(shader.Get_Post_Detail_Alpha_Func());
    parameters.alpha_cutoff=shader.Get_Alpha_Test()==ShaderClass::ALPHATEST_ENABLE ? 96.0f/255.0f : 0;
    const auto scene = Graphics::Get_Scene_Draw_Parameters();
    constexpr std::array fog_modes{Graphics::MaterialFogMode::Disabled,Graphics::MaterialFogMode::Scene,
        Graphics::MaterialFogMode::Black,Graphics::MaterialFogMode::White};
    const auto fog=Graphics::Resolve_Material_Fog(scene.fog,
        fog_modes[shader.Get_Fog_Func()]);
    parameters.fog_state=fog.state;
    parameters.fog_color=fog.color;
    if (overrides.alpha_cutoff>=0 && shader.Get_Alpha_Test()==ShaderClass::ALPHATEST_ENABLE)
        parameters.alpha_cutoff=overrides.alpha_cutoff;
    Graphics::PropStyle style;
    style.depth_write = shader.Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_ENABLE;
    constexpr std::array comparisons{Graphics::RHIComparison::Never,Graphics::RHIComparison::Less,
        Graphics::RHIComparison::Equal,Graphics::RHIComparison::LessEqual,Graphics::RHIComparison::Greater,
        Graphics::RHIComparison::NotEqual,Graphics::RHIComparison::GreaterEqual,Graphics::RHIComparison::Always};
    style.depth_comparison = comparisons[shader.Get_Depth_Compare()];
    constexpr std::array sources{Graphics::RHIBlendFactor::Zero,Graphics::RHIBlendFactor::One,
        Graphics::RHIBlendFactor::SourceAlpha,Graphics::RHIBlendFactor::InverseSourceAlpha};
    constexpr std::array destinations{Graphics::RHIBlendFactor::Zero,Graphics::RHIBlendFactor::One,
        Graphics::RHIBlendFactor::SourceColor,Graphics::RHIBlendFactor::InverseSourceColor,
        Graphics::RHIBlendFactor::SourceAlpha,Graphics::RHIBlendFactor::InverseSourceAlpha};
    style.source_blend = sources[shader.Get_Src_Blend_Func()];
    style.destination_blend = destinations[shader.Get_Dst_Blend_Func()];
    if (overrides.force_multiply) {
        style.source_blend=Graphics::RHIBlendFactor::DestinationColor;
        style.destination_blend=Graphics::RHIBlendFactor::SourceColor;
    }
    style.cull = shader.Get_Cull_Mode() == ShaderClass::CULL_MODE_ENABLE
        ? Graphics::RHICullMode::Back : Graphics::RHICullMode::None;
    style.front_counter_clockwise = !WW3D::Is_Reflection_Render_Pass();
    style.color_write_mask = shader.Get_Color_Mask() == ShaderClass::COLOR_WRITE_ENABLE
        ? 15 : 0;
    style = Graphics::Resolve_Prop_Style(style, scene);
    if (overrides.color_write_mask>=0) style.color_write_mask=static_cast<std::uint8_t>(overrides.color_write_mask);
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture) continue;
        style.samplers[stage] = Graphics::Resolve_Texture_Sampling(texture->Get_Sampling(),
            Graphics::Get_Texture_Sampling_Settings(), stage == 0);
    }
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
    VertexMaterialClass* material)
{
    if (!material) return;
    for (unsigned stage=0;stage<parameters.uv_transform.size();++stage) {
        auto* mapper=material->Peek_Mapper(stage);
        if (!mapper) continue;
        Matrix4x4 matrix;
        mapper->Calculate_Texture_Matrix(matrix);
        const auto mode=mapper->Get_Coordinate_Mode();
        if (mode.source == Graphics::TextureCoordinateSource::UV) {
            parameters.uv_transform[stage]=Graphics::Make_Affine_Texture_Transform({
                matrix[0][0],matrix[0][1],matrix[0][2],
                matrix[1][0],matrix[1][1],matrix[1][2]});
        } else {
            std::memcpy(parameters.uv_transform[stage].data(),&matrix,sizeof(matrix));
        }
        parameters.uv_sources[stage*2]=static_cast<float>(mode.source);
        parameters.uv_sources[stage*2+1]=mode.projected ? 1.0f : 0.0f;
        if (mapper->Mapper_ID()==TextureMapperClass::MAPPER_ID_BUMPENV) {
            float bump[4];
            static_cast<BumpEnvTextureMapperClass*>(mapper)->Calculate_Bump_Matrix(bump);
            std::memcpy(parameters.bump_matrix.data(),bump,sizeof(bump));
        }
    }
}


