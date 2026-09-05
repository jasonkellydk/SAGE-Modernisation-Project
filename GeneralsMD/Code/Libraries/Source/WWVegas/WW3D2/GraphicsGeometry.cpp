#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "LightEnvironment.h"
#include "VertMaterial.h"
#include "Mapper.h"
#include "GraphicsGeometry.h"
#include "GraphicsMaterial.h"
#include "Texture.h"
#include "WW3D.h"
import Graphics.Scene.Props.TransparentGeometry;
import Graphics.Scene.Props.MaterialPassQueue;
import Graphics.Backends.DX11.Coexistence;
import Graphics.Scene.Shadows.DirectionalRenderer;

namespace {
Graphics::TransparentGeometry transparent_geometry;
Graphics::MaterialPassQueue material_passes;
std::vector<Graphics::RHITextureHandle> material_pass_textures;
std::vector<Graphics::RHITextureHandle> transparent_textures;
std::vector<Graphics::RHITextureHandle> shadow_textures;
}

void Clear_Graphics_Shadow_Geometry()
{
    Graphics::Get_Directional_Shadow_Renderer().Clear_Casters();
    if (auto* device = Graphics::Shared_Frame_Device())
        for (const auto texture : shadow_textures) device->Destroy_Texture(texture);
    shadow_textures.clear();
}

void Flush_Graphics_Material_Passes()
{
    if (auto* device=Graphics::Shared_Frame_Device()) {
        material_passes.Flush(device->Immediate_Command_List());
        for (auto texture : material_pass_textures) device->Destroy_Texture(texture);
    } else material_passes.Clear();
    material_pass_textures.clear();
    if (auto* backend=WW3D::Get_Render_Backend()) backend->Invalidate_Cached_Render_States();
}

void Clear_Graphics_Transparent_Geometry()
{
    Clear_Graphics_Shadow_Geometry();
    transparent_geometry.Clear(Graphics::Get_Prop_Renderer());
    material_passes.Clear();
    if (auto* device=Graphics::Shared_Frame_Device()) {
        for (const auto texture : transparent_textures) device->Destroy_Texture(texture);
        for (const auto texture : material_pass_textures) device->Destroy_Texture(texture);
    }
    transparent_textures.clear(); material_pass_textures.clear();
}

void Flush_Graphics_Transparent_Geometry()
{
    Flush_Graphics_Material_Passes();
    if (auto* device=Graphics::Shared_Frame_Device())
        transparent_geometry.Flush(Graphics::Get_Prop_Renderer(),device->Immediate_Command_List());
    Clear_Graphics_Transparent_Geometry();
    if (auto* backend=WW3D::Get_Render_Backend()) backend->Invalidate_Cached_Render_States();
}

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
    auto* device = Graphics::Shared_Frame_Device();
    auto* backend = WW3D::Get_Render_Backend();
    if (!device || !backend || vertices.empty() || indices.empty()) return false;
    std::array<Graphics::RHITextureHandle,2> textures{};
    const auto release_textures = [&]() {
        for (auto texture : textures)
            if (texture.Is_Valid()) device->Destroy_Texture(texture);
    };
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture || shader.Get_Texturing()==ShaderClass::TEXTURING_DISABLE) continue;
        void* resource=nullptr;
        void* view=nullptr;
        if (!texture->Ensure_Render_Backend_Texture()
            || !backend->Get_Shared_Texture_Resources(texture->Peek_Render_Backend_Texture(),resource,view)) {
            release_textures();
            return false;
        }
        textures[stage]=Graphics::Import_Shared_Texture(resource,view);
        if (!textures[stage].Is_Valid()) { release_textures(); return false; }
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
    if (shader.Get_Fog_Func()!=ShaderClass::FOG_DISABLE && backend->Is_Fog_Allowed()) {
        const auto fog=backend->Get_Fog_State();
        if (fog.enabled) {
            parameters.fog_state={fog.start,fog.end,1,0};
            std::copy(std::begin(fog.color),std::end(fog.color),parameters.fog_color.begin());
            if (shader.Get_Fog_Func()==ShaderClass::FOG_SCALE_FRAGMENT) parameters.fog_color={0,0,0,1};
            if (shader.Get_Fog_Func()==ShaderClass::FOG_WHITE) parameters.fog_color={1,1,1,1};
        }
    }
    if (overrides.alpha_cutoff>=0 && shader.Get_Alpha_Test()==ShaderClass::ALPHATEST_ENABLE)
        parameters.alpha_cutoff=overrides.alpha_cutoff;
    Graphics::PropStyle style;
    style.depth_bias=static_cast<std::int32_t>(backend->Get_Depth_Bias());
    style.wireframe=backend->Get_Fill_Mode()==RenderBackendFillMode::Wireframe;
    const auto stencil=backend->Get_Stencil_State();
    style.stencil.enabled=stencil.enabled;
    style.stencil.reference=static_cast<std::uint8_t>(stencil.reference);
    style.stencil.read_mask=static_cast<std::uint8_t>(stencil.read_mask);
    style.stencil.write_mask=static_cast<std::uint8_t>(stencil.write_mask);
    style.stencil.front.comparison=static_cast<Graphics::RHIComparison>(stencil.comparison);
    style.stencil.front.fail=static_cast<Graphics::RHIStencilOperation>(stencil.fail);
    style.stencil.front.depth_fail=static_cast<Graphics::RHIStencilOperation>(stencil.depth_fail);
    style.stencil.front.pass=static_cast<Graphics::RHIStencilOperation>(stencil.pass);
    style.stencil.back=style.stencil.front;

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
        ? static_cast<std::uint8_t>(backend->Get_Color_Write_Mask()) : 0;
    if (overrides.color_write_mask>=0) style.color_write_mask=static_cast<std::uint8_t>(overrides.color_write_mask);
    for (unsigned stage=0;stage<2;++stage) {
        auto* texture=source_textures[stage];
        if (!texture) continue;
        style.samplers[stage].linear_filter = texture->Get_Filter().Get_Min_Filter()!=TextureFilterClass::FILTER_TYPE_NONE;
        style.samplers[stage].address[0] = texture->Get_Filter().Get_U_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP
            ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;
        style.samplers[stage].address[1] = texture->Get_Filter().Get_V_Addr_Mode() == TextureFilterClass::TEXTURE_ADDRESS_CLAMP
            ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;
    }
    auto& renderer = Graphics::Get_Prop_Renderer();
    if (overrides.shadow_capture) {
        const bool queued = Graphics::Get_Directional_Shadow_Renderer().Add_Caster(
            vertices,indices,parameters,textures,style);
        if (queued) {
            for (const auto texture : textures) if (texture.Is_Valid()) shadow_textures.push_back(texture);
        } else release_textures();
        return queued;
    }
    if (overrides.deferred_pass) {
        const bool queued=material_passes.Submit(renderer,vertices,indices,style,parameters,textures);
        if (queued) {
            for (auto texture : textures) if (texture.Is_Valid()) material_pass_textures.push_back(texture);
        } else release_textures();
        return queued;
    }
    if (sorting_view) {
        const std::array depth{(*sorting_view)[2][0],(*sorting_view)[2][1],
            (*sorting_view)[2][2],(*sorting_view)[2][3]};
        const bool queued=transparent_geometry.Submit(renderer,vertices,indices,style,parameters,textures,depth);
        if (queued) {
            for (auto texture : textures)
                if (texture.Is_Valid()) transparent_textures.push_back(texture);
        } else release_textures();
        return queued;
    }
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    const bool drawn = renderer.Draw(device->Immediate_Command_List(),mesh,style,parameters,textures);
    renderer.Destroy_Mesh(mesh);
    release_textures();
    backend->Invalidate_Cached_Render_States();
    return drawn;
}

std::optional<Graphics::PropMaterial> Describe_Graphics_Vertex_Material(VertexMaterialClass* material)
{
    if (material == nullptr) return std::nullopt;
    Graphics::PropMaterial description;
    Vector3 diffuse,ambient,emissive,specular;
    material->Get_Diffuse(&diffuse); material->Get_Ambient(&ambient);
    material->Get_Emissive(&emissive); material->Get_Specular(&specular);
    description.diffuse = {diffuse.X,diffuse.Y,diffuse.Z};
    description.ambient = {ambient.X,ambient.Y,ambient.Z};
    description.emissive = {emissive.X,emissive.Y,emissive.Z};
    description.specular = {specular.X,specular.Y,specular.Z};
    description.opacity = material->Get_Opacity();
    description.shininess = material->Get_Shininess();
    description.lighting = material->Get_Lighting();
    const auto source = [](VertexMaterialClass::ColorSourceType value) {
        if (value == VertexMaterialClass::COLOR1) return Graphics::PropColorSource::PrimaryColor;
        if (value == VertexMaterialClass::COLOR2) return Graphics::PropColorSource::SecondaryColor;
        return Graphics::PropColorSource::Material;
    };
    description.diffuse_source = source(material->Get_Diffuse_Color_Source());
    description.ambient_source = source(material->Get_Ambient_Color_Source());
    description.emissive_source = source(material->Get_Emissive_Color_Source());
    return description;
}

void Extract_Graphics_Texture_Mappers(Graphics::PropParameters& parameters,
    VertexMaterialClass* material)
{
    if (!material) return;
    for (unsigned stage=0;stage<2;++stage) {
        auto* mapper=material->Peek_Mapper(stage);
        if (!mapper) continue;
        Matrix4x4 matrix;
        mapper->Calculate_Texture_Matrix(matrix);
        std::memcpy(parameters.uv_transform[stage].data(),&matrix,sizeof(matrix));
        const int id=mapper->Mapper_ID();
        if (id==TextureMapperClass::MAPPER_ID_BUMPENV) {
            float bump[4];
            static_cast<BumpEnvTextureMapperClass*>(mapper)->Calculate_Bump_Matrix(bump);
            std::memcpy(parameters.bump_matrix.data(),bump,sizeof(bump));
        }
        unsigned source=0;
        switch (id) {
        case TextureMapperClass::MAPPER_ID_CLASSIC_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_WS_CLASSIC_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_GRID_CLASSIC_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_GRID_WS_CLASSIC_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_EDGE: source=2; break;
        case TextureMapperClass::MAPPER_ID_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_WS_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_GRID_ENVIRONMENT:
        case TextureMapperClass::MAPPER_ID_GRID_WS_ENVIRONMENT: source=3; break;
        case TextureMapperClass::MAPPER_ID_SCREEN: source=1; parameters.uv_sources[stage*2+1]=1; break;
        }
        parameters.uv_sources[stage*2]=static_cast<float>(source);
    }
}

void Extract_Graphics_Lighting(Graphics::PropParameters& parameters,
    const LightEnvironmentClass* environment)
{
    if (!environment) return;
    parameters.light_direction={}; parameters.light_diffuse={}; parameters.light_specular={};
    parameters.light_position={}; parameters.light_attenuation={};
    parameters.light_ambient={}; parameters.light_spot={};
    const auto& ambient=environment->Get_Equivalent_Ambient();
    parameters.scene_ambient={ambient.X,ambient.Y,ambient.Z,0};
    const int count=std::min(environment->Get_Light_Count(),4);
    for (int i=0;i<count;++i) {
        const bool point=environment->isPointLight(i);
        const auto& diffuse=point ? environment->getPointDiffuse(i) : environment->Get_Light_Diffuse(i);
        const auto& direction=environment->Get_Light_Direction(i);
        parameters.light_direction[i]={direction.X,direction.Y,direction.Z,1};
        parameters.light_diffuse[i]={diffuse.X,diffuse.Y,diffuse.Z,0};
        if (point) {
            const auto& position=environment->getPointCenter(i);
            const auto& local_ambient=environment->getPointAmbient(i);
            const float outer=environment->getPointOrad(i);
            const float inner=environment->getPointIrad(i);
            parameters.light_position[i]={position.X,position.Y,position.Z,1};
            parameters.light_ambient[i]={local_ambient.X,local_ambient.Y,local_ambient.Z,0};
            parameters.light_attenuation[i]={1,
                std::abs(inner-outer)<0.00001f || inner<=0.00001f ? 0 : 0.1f/inner,
                outer>0.00001f ? 8/(outer*outer) : 0,outer};
        } else if (i==0) parameters.light_specular[i]={1,1,1,0};
    }
}
