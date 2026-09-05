#include <array>
#include <span>
#include <vector>
#include <cstring>
#include <algorithm>
#include "W3DDevice/GameClient/W3DObjectGraphics.h"
#include "W3DDevice/GameClient/W3DGraphicsResources.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "WW3D2/Mesh.h"
#include "WW3D2/MeshMdl.h"
#include "WW3D2/HLOD.h"
#include "WW3D2/Mapper.h"
#include "WW3D2/VertMaterial.h"
#include "WW3D2/Texture.h"
#include "WW3D2/Camera.h"
#include "WW3D2/RInfo.h"
#include "WW3D2/Scene.h"
#include "WW3D2/WW3D.h"
import Graphics.Backends.DX11.Coexistence;

namespace {
std::array<float,4> Unpack_Color(unsigned packed)
{
    return {((packed>>16)&255)/255.0f,((packed>>8)&255)/255.0f,(packed&255)/255.0f,((packed>>24)&255)/255.0f};
}
std::array<float,3> RGB(const Vector3& color) { return {color.X,color.Y,color.Z}; }
Graphics::RHIBlendFactor Source_Blend(ShaderClass::SrcBlendFuncType value)
{
    switch(value) {
    case ShaderClass::SRCBLEND_ZERO: return Graphics::RHIBlendFactor::Zero;
    case ShaderClass::SRCBLEND_ONE: return Graphics::RHIBlendFactor::One;
    case ShaderClass::SRCBLEND_SRC_ALPHA: return Graphics::RHIBlendFactor::SourceAlpha;
    default: return Graphics::RHIBlendFactor::InverseSourceAlpha;
    }
}
Graphics::RHIBlendFactor Destination_Blend(ShaderClass::DstBlendFuncType value)
{
    switch(value) {
    case ShaderClass::DSTBLEND_ZERO: return Graphics::RHIBlendFactor::Zero;
    case ShaderClass::DSTBLEND_ONE: return Graphics::RHIBlendFactor::One;
    case ShaderClass::DSTBLEND_SRC_COLOR: return Graphics::RHIBlendFactor::SourceColor;
    case ShaderClass::DSTBLEND_ONE_MINUS_SRC_COLOR: return Graphics::RHIBlendFactor::InverseSourceColor;
    case ShaderClass::DSTBLEND_SRC_ALPHA: return Graphics::RHIBlendFactor::SourceAlpha;
    default: return Graphics::RHIBlendFactor::InverseSourceAlpha;
    }
}
struct Batch
{
    Graphics::PropMeshHandle mesh;
    Graphics::PropStyle style;
    Graphics::PropParameters parameters;
    TextureClass* texture[2]{};
    TextureMapperClass* mapper[2]{};
    VertexMaterialClass* material = nullptr;
    bool fog = false;
};
}

struct W3DObjectGraphics::State
{
    std::vector<Batch> batches;
    bool dirty = true;
    bool animated = false;
    bool background = false;
    Matrix3D transform{true};
    Vector3 camera{0,0,0};
    Graphics::PropLighting lighting{};
    int lod = -1;

    ~State() { Clear(); }
    void Clear()
    {
        for (const auto& batch : batches) Graphics::Get_Prop_Renderer().Destroy_Mesh(batch.mesh);
        batches.clear();
    }

    bool Extract(RenderObjClass& object, RenderInfoClass& info)
    {
        if (object.Is_Hidden()) return true;
        object.Validate_Transform();
        if (object.Class_ID() == RenderObjClass::CLASSID_HLOD) {
            animated = true;
            auto& hierarchy = static_cast<HLodClass&>(object);
            hierarchy.Get_Bone_Transform(0);
            const int level = hierarchy.Get_LOD_Level();
            for (int i=0;i<hierarchy.Get_Lod_Model_Count(level);++i)
                if (!Extract(*hierarchy.Peek_Lod_Model(level,i),info)) return false;
            for (int i=0;i<hierarchy.Get_Additional_Model_Count();++i)
                if (!Extract(*hierarchy.Peek_Additional_Model(i),info)) return false;
            return true;
        }
        if (object.Class_ID() != RenderObjClass::CLASSID_MESH) {
            for (int i=0;i<object.Get_Num_Sub_Objects();++i) {
                RenderObjClass* child = object.Get_Sub_Object(i);
                const bool success = child == nullptr || Extract(*child,info);
                REF_PTR_RELEASE(child);
                if (!success) return false;
            }
            return true;
        }
        auto& mesh = static_cast<MeshClass&>(object);
        MeshModelClass* model = mesh.Peek_Model();
        if (!model || model->Get_Vertex_Count()==0) return true;
        const auto* positions = model->Get_Vertex_Array();
        const auto* normals = model->Get_Vertex_Normal_Array();
        const auto* polygons = model->Get_Polygon_Array();
        Matrix3D world = object.Get_Transform();
        std::vector<Vector3> deformed_positions;
        std::vector<Vector3> deformed_normals;
        if (model->Get_Flag(MeshGeometryClass::ALIGNED)) {
            animated = true;
            Vector3 direction;
            info.Camera.Get_Transform().Get_Z_Vector(&direction);
            const Vector3 position=world.Get_Translation();
            world.Obj_Look_At(position,position+direction,0.0f);
        } else if (model->Get_Flag(MeshGeometryClass::ORIENTED)) {
            animated = true;
            const Vector3 position=world.Get_Translation();
            world.Obj_Look_At(position,info.Camera.Get_Position(),0.0f);
        } else if (model->Get_Flag(MeshGeometryClass::SKIN)) {
            animated = true;
            deformed_positions.resize(model->Get_Vertex_Count());
            deformed_normals.resize(model->Get_Vertex_Count());
            mesh.Get_Deformed_Vertices(deformed_positions.data(),deformed_normals.data());
            positions = deformed_positions.data();
            normals = deformed_normals.data();
            world.Make_Identity();
        }
        for (int pass=0;pass<model->Get_Pass_Count();++pass) {
            std::vector<Graphics::PropVertex> vertices;
            std::vector<std::uint32_t> indices;
            Batch batch;
            ShaderClass previous;
            bool pending = false;
            const auto flush = [&]() {
                if (indices.empty()) return true;
                batch.mesh = Graphics::Get_Prop_Renderer().Create_Mesh(vertices,indices);
                if (!batch.mesh.Is_Valid()) return false;
                batches.push_back(batch);
                vertices.clear(); indices.clear();
                return true;
            };
            for (int polygon=0;polygon<model->Get_Polygon_Count();++polygon) {
                const ShaderClass shader = model->Get_Shader(polygon,pass);
                TextureClass* first = model->Peek_Texture(polygon,pass,0);
                TextureClass* second = model->Peek_Texture(polygon,pass,1);
                auto* polygon_material = model->Peek_Material(polygons[polygon].I,pass);
                if (!pending || previous != shader || first != batch.texture[0] || second != batch.texture[1]
                    || polygon_material != batch.material) {
                    if (!flush()) return false;
                    batch = {};
                    previous = shader;
                    pending = true;
                    batch.texture[0] = first; batch.texture[1] = second;
                    batch.material = polygon_material;
                    batch.fog = shader.Get_Fog_Func()!=ShaderClass::FOG_DISABLE;
                    for (unsigned stage=0;stage<2;++stage) batch.mapper[stage] = polygon_material ? polygon_material->Peek_Mapper(stage) : nullptr;
                    batch.style.depth_write = !background && shader.Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_ENABLE;
                    batch.style.depth_test = !background;
                    batch.style.color_write_mask = shader.Get_Color_Mask()==ShaderClass::COLOR_WRITE_ENABLE ? 15 : 0;
                    batch.style.depth_comparison = static_cast<Graphics::RHIComparison>(shader.Get_Depth_Compare());
                    batch.style.cull = background || shader.Get_Cull_Mode()==ShaderClass::CULL_MODE_DISABLE
                        ? Graphics::RHICullMode::None : Graphics::RHICullMode::Back;
                    batch.style.front_counter_clockwise = true;
                    for (unsigned stage=0;stage<2;++stage) {
                        if (!batch.texture[stage]) continue;
                        auto& sampler = batch.style.samplers[stage];
                        const auto& filter = batch.texture[stage]->Get_Filter();
                        sampler.address[0] = background || filter.Get_U_Addr_Mode()==TextureFilterClass::TEXTURE_ADDRESS_CLAMP
                            ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;
                        sampler.address[1] = background || filter.Get_V_Addr_Mode()==TextureFilterClass::TEXTURE_ADDRESS_CLAMP
                            ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;
                        sampler.linear_filter = filter.Get_Min_Filter()!=TextureFilterClass::FILTER_TYPE_NONE;
                    }
                    batch.style.source_blend = Source_Blend(shader.Get_Src_Blend_Func());
                    batch.style.destination_blend = Destination_Blend(shader.Get_Dst_Blend_Func());
                    batch.parameters.textured = first && shader.Get_Texturing()!=ShaderClass::TEXTURING_DISABLE ? 1.0f : 0.0f;
                    batch.parameters.secondary_texture = second && shader.Get_Texturing()!=ShaderClass::TEXTURING_DISABLE ? 1.0f : 0.0f;
                    batch.parameters.primary_gradient = float(shader.Get_Primary_Gradient());
                    batch.parameters.secondary_gradient = float(shader.Get_Secondary_Gradient());
                    batch.parameters.detail_color = float(shader.Get_Post_Detail_Color_Func());
                    batch.parameters.detail_alpha = float(shader.Get_Post_Detail_Alpha_Func());
                    batch.parameters.alpha_cutoff = shader.Get_Alpha_Test()!=ShaderClass::ALPHATEST_DISABLE ? 96.0f/255 : 0;
                }
                const int corners[3] = {polygons[polygon].I,polygons[polygon].J,polygons[polygon].K};
                for (int index : corners) {
                    Graphics::PropVertex vertex;
                    Vector3 position, normal(0,0,1);
                    Matrix3D::Transform_Vector(world,positions[index],&position);
                    if (normals) Matrix3D::Rotate_Vector(world,normals[index],&normal);
                    vertex.position = RGB(position);
                    vertex.normal = RGB(normal);
                    auto* material = model->Peek_Material(index,pass);
                    const unsigned* dcg = model->Get_DCG_Array(pass);
                    const unsigned* dig = model->Get_DIG_Array(pass);
                    const auto primary = dcg ? Unpack_Color(dcg[index]) : std::array<float,4>{1,1,1,1};
                    const auto secondary = dig ? Unpack_Color(dig[index]) : std::array<float,4>{0,0,0,0};
                    vertex.color = primary;
                    vertex.secondary_color = secondary;
                    if (material) {
                        Vector3 diffuse,ambient,emissive,specular;
                        material->Get_Diffuse(&diffuse); material->Get_Ambient(&ambient);
                        material->Get_Emissive(&emissive); material->Get_Specular(&specular);
                        const auto select = [&](VertexMaterialClass::ColorSourceType source,const Vector3& value) {
                            if (source==VertexMaterialClass::COLOR1) return std::array<float,3>{primary[0],primary[1],primary[2]};
                            if (source==VertexMaterialClass::COLOR2) return std::array<float,3>{secondary[0],secondary[1],secondary[2]};
                            return RGB(value);
                        };
                        const bool lit = material->Get_Lighting() && !background;
                        const auto diffuse_color = lit ? select(material->Get_Diffuse_Color_Source(),diffuse) : RGB(diffuse);
                        vertex.material_diffuse = {diffuse_color[0],diffuse_color[1],diffuse_color[2],material->Get_Opacity()};
                        if (lit && material->Get_Diffuse_Color_Source()==VertexMaterialClass::COLOR1) vertex.material_diffuse[3] = primary[3];
                        if (lit && material->Get_Diffuse_Color_Source()==VertexMaterialClass::COLOR2) vertex.material_diffuse[3] = secondary[3];
                        const auto ambient_color = select(material->Get_Ambient_Color_Source(),ambient);
                        const auto emissive_color = select(material->Get_Emissive_Color_Source(),emissive);
                        vertex.material_ambient = {ambient_color[0],ambient_color[1],ambient_color[2],lit ? 1.0f : 0.0f};
                        vertex.material_emissive = {emissive_color[0],emissive_color[1],emissive_color[2],0};
                        vertex.material_specular = {specular.X,specular.Y,specular.Z,material->Get_Shininess()};
                    }
                    for (int stage=0;stage<2;++stage) {
                        const Vector2* uv = model->Get_UV_Array(pass,stage);
                        std::array<float,2> mapped = uv ? std::array<float,2>{uv[index].X,uv[index].Y} : std::array<float,2>{};
                        if (stage==0) vertex.uv = mapped; else vertex.secondary_uv = mapped;
                    }
                    indices.push_back(static_cast<std::uint32_t>(vertices.size()));
                    vertices.push_back(vertex);
                }
            }
            if (!flush()) return false;
        }
        return true;
    }
};

W3DObjectGraphics::W3DObjectGraphics() : m_state(std::make_unique<State>()) {}
W3DObjectGraphics::~W3DObjectGraphics() = default;
void W3DObjectGraphics::Invalidate() { m_state->dirty = true; }
bool W3DObjectGraphics::Render(RenderObjClass& object, RenderInfoClass& info,
    const Graphics::PropLighting& lighting, W3DShroud* shroud, bool background)
{
    auto* device = Graphics::Shared_Frame_Device();
    if (!device) return false;
    const auto& transform = object.Get_Transform();
    const auto camera = info.Camera.Get_Position();
    if (m_state->dirty || m_state->animated || m_state->background!=background
        || m_state->lod!=object.Get_LOD_Level()
        || std::memcmp(&m_state->transform,&transform,sizeof(transform))!=0) {
        m_state->Clear();
        m_state->transform = transform; m_state->camera = camera; m_state->lighting = lighting;
        m_state->background = background; m_state->lod = object.Get_LOD_Level();
        m_state->animated = false;
        if (!m_state->Extract(object,info)) { m_state->dirty=true; return false; }
        m_state->dirty = false;
    }
    auto surface = Make_Surface_Parameters(info.Camera);
    const auto shroud_texture = Set_Surface_Shroud(surface,shroud);
    auto& renderer = Graphics::Get_Prop_Renderer();
    for (const auto& batch : m_state->batches) {
        auto parameters = batch.parameters;
        parameters.view_projection = surface.view_projection;
        parameters.shroud_projection = surface.shroud_projection;
        parameters.camera_position = {camera.X,camera.Y,camera.Z,1};
        WW3D::Get_Render_Backend()->Get_Transform(RenderBackendTransform::View,parameters.view.data());
        if (batch.fog && !background && WW3D::Get_Render_Backend()->Is_Fog_Enabled()) {
            SceneClass* scene = object.Peek_Scene();
            if (!scene && info.Camera.Peek_Scene()) scene = info.Camera.Peek_Scene();
            if (!scene && TheTerrainRenderObject) scene = TheTerrainRenderObject->Peek_Scene();
            if (scene) {
                scene->Get_Fog_Range(&parameters.fog_state[0],&parameters.fog_state[1]);
                parameters.fog_state[2] = 1;
                const auto& fog = scene->Get_Fog_Color();
                parameters.fog_color = {fog.X,fog.Y,fog.Z,1};
            }
        }
        for (unsigned stage=0;stage<2;++stage) {
            auto* mapper = batch.mapper[stage];
            if (!mapper) continue;
            Matrix4x4 matrix;
            mapper->Calculate_Texture_Matrix(matrix);
            std::memcpy(parameters.uv_transform[stage].data(),&matrix,sizeof(matrix));
            const int id = mapper->Mapper_ID();
            if (id == TextureMapperClass::MAPPER_ID_BUMPENV) {
                float bump[4];
                static_cast<BumpEnvTextureMapperClass*>(mapper)->Calculate_Bump_Matrix(bump);
                std::copy(std::begin(bump),std::end(bump),parameters.bump_matrix.begin());
            }
            unsigned source = 0;
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
            parameters.uv_sources[stage*2] = float(source);
        }

        parameters.scene_ambient = {lighting.ambient[0],lighting.ambient[1],lighting.ambient[2],0};
        for (unsigned i=0;i<4;++i) {
            const auto& source = lighting.lights[i];
            const auto& direction = source.direction;
            const float enabled = direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2] > 0.000001f ? 1.0f : 0.0f;
            parameters.light_direction[i] = {direction[0],direction[1],direction[2],enabled};
            parameters.light_diffuse[i] = {source.diffuse[0],source.diffuse[1],source.diffuse[2],0};
            parameters.light_specular[i] = {source.specular[0],source.specular[1],source.specular[2],0};
        }
        const std::array<Graphics::RHITextureHandle,4> textures{
            Resolve_Graphics_Texture(batch.texture[0]),Resolve_Graphics_Texture(batch.texture[1]),{},shroud_texture};
        auto style=batch.style;
        if (style.cull!=Graphics::RHICullMode::None)
            style.front_counter_clockwise=!WW3D::Is_Reflection_Render_Pass();
        if (!Graphics::Draw_Prop(renderer,device->Immediate_Command_List(),batch.mesh,style,
            parameters,textures,shroud_texture.Is_Valid() && !background)) return false;
    }
    WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
    return true;
}
