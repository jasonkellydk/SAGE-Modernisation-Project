#include <array>
#include "rts/profile.h"
#include <vector>
#include <cstring>
#include "GraphicsMesh.h"
#include "GraphicsMaterial.h"
#include "GraphicsMaterialPass.h"
#include "Mesh.h"
#include "MeshMdl.h"
#include "MatPass.h"
#include "VertMaterial.h"
#include "Mapper.h"
#include "Camera.h"
#include "RInfo.h"
#include "WW3D.h"
#include <algorithm>
import Graphics.Scene.Views.CameraMatrices;
import Graphics.Scene.Props.Extraction;
import Graphics.Scene.Props.MeshSet;
import Graphics.Scene.Props.Submission;

class GraphicsMeshState final {
public:
    Graphics::PropMeshSet base;
    Graphics::PropMeshSet additional;
};

void Release_Graphics_Mesh_State(GraphicsMeshState*& state)
{
    delete state;
    state = nullptr;
}

namespace {
std::array<float,4> Unpack_Mesh_Color(unsigned color)
{
    return {((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,
        (color&255)/255.0f,((color>>24)&255)/255.0f};
}

void Extract_Mesh_Mappers(Graphics::PropParameters& parameters,VertexMaterialClass* material,
    MeshClass& mesh,bool sorted)
{
    auto* mapper=material ? material->Peek_Mapper() : nullptr;
    const auto* data=mesh.Get_User_Data();
    if (sorted || !data || *static_cast<const int*>(data)!=RenderObjClass::USER_DATA_MATERIAL_OVERRIDE
        || !mapper || mapper->Mapper_ID()!=TextureMapperClass::MAPPER_ID_LINEAR_OFFSET) {
        Extract_Graphics_Texture_Mappers(parameters,material);
        return;
    }
    auto& linear=*static_cast<LinearOffsetTextureMapperClass*>(mapper);
    Vector2 saved_offset;
    linear.Get_Current_UV_Offset(saved_offset);
    const auto saved_time=linear.Get_LastUsedSyncTime();
    linear.Set_Current_UV_Offset(static_cast<const RenderObjClass::Material_Override*>(data)->customUVOffset);
    linear.Set_LastUsedSyncTime(WW3D::Get_Sync_Time());
    Extract_Graphics_Texture_Mappers(parameters,material);
    linear.Set_Current_UV_Offset(saved_offset);
    linear.Set_LastUsedSyncTime(saved_time);
}
}

bool Draw_Graphics_Mesh(MeshClass& mesh,RenderInfoClass& info,const GraphicsMeshOverrides& overrides)
{
    PROFILER_SECTION_NAME("Graphics.Mesh.ExtractDraw");
    auto* model=mesh.Peek_Model();
    if (!model || model->Get_Vertex_Count()==0 || model->Get_Polygon_Count()==0) return true;
    mesh.Validate_Transform();
    Matrix3D world=mesh.Get_Transform();
    const auto* positions=model->Get_Vertex_Array();
    const auto* normals=model->Get_Vertex_Normal_Array();
    const auto* polygons=model->Get_Polygon_Array();
    std::vector<Vector3> deformed_positions,deformed_normals;
    const bool skin=model->Get_Flag(MeshGeometryClass::SKIN)!=0;
    if (model->Get_Flag(MeshGeometryClass::ALIGNED)) {
        Vector3 direction;
        info.Camera.Get_Transform().Get_Z_Vector(&direction);
        const auto position=world.Get_Translation();
        world.Obj_Look_At(position,position+direction,0);
    } else if (model->Get_Flag(MeshGeometryClass::ORIENTED)) {
        const auto position=world.Get_Translation();
        world.Obj_Look_At(position,info.Camera.Get_Position(),0);
    } else if (skin) {
        deformed_positions.resize(model->Get_Vertex_Count());
        deformed_normals.resize(model->Get_Vertex_Count());
        mesh.Get_Deformed_Vertices(deformed_positions.data(),deformed_normals.data());
        positions=deformed_positions.data(); normals=deformed_normals.data();
        world.Make_Identity();
    }
    auto workspace = Graphics::Get_Prop_Extraction_Cache().Acquire();
    std::span<Graphics::PropSourceVertex> source;
    {
        PROFILER_SECTION_NAME("Graphics.Mesh.TransformVertices");
        source = workspace.Workspace().Prepare_Source(model->Get_Vertex_Count());
        for (int i=0;i<model->Get_Vertex_Count();++i) {
            source[i].position={positions[i].X,positions[i].Y,positions[i].Z};
            if (normals) source[i].normal={normals[i].X,normals[i].Y,normals[i].Z};
        }
    }
    Matrix4x4 view,view_projection;
    const auto& camera_matrices = Graphics::Get_Camera_Matrices();
    std::copy_n(camera_matrices.view.values.data(), 16, &view[0][0]);
    const auto composed = Graphics::Compose_Matrices(camera_matrices.projection,camera_matrices.view);
    std::copy_n(composed.values.data(), 16, &view_projection[0][0]);
    Graphics::PropParameters context;
    context.normal_in_world_space = normals ? 0.0f : 1.0f;
    for (unsigned row=0;row<3;++row)
        for (unsigned column=0;column<4;++column)
            context.world[row*4+column]=world[row][column];
    context.view = camera_matrices.view.values;
    const auto camera=info.Camera.Get_Position();
    context.camera_position={camera.X,camera.Y,camera.Z,1};
    Graphics::Set_Prop_Lighting(context,mesh.Get_Lighting_Environment());
    const bool sorted=model->Get_Flag(MeshGeometryClass::SORT) && WW3D::Is_Sorting_Enabled();
    const bool additional_only=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY)!=0;
    const bool shadow=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING)!=0;
    const bool render_base=!additional_only || (shadow && (model->Get_Single_Shader().Get_Alpha_Test()==ShaderClass::ALPHATEST_ENABLE
        || model->Get_Single_Shader().Get_Src_Blend_Func()==ShaderClass::SRCBLEND_SRC_ALPHA));
    const bool two_sided=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_FORCE_TWO_SIDED)!=0;
    auto& batch = workspace.Workspace().Batch();
    auto*& mesh_state = model->Graphics_Mesh_State();
    if (mesh_state == nullptr) mesh_state = new GraphicsMeshState;
    auto& meshes = mesh_state->base;
    auto& renderer = Graphics::Get_Prop_Renderer();
    std::size_t batch_index = 0;
    bool success=true;
    for (int pass=0;render_base && pass<model->Get_Pass_Count()
        && (!overrides.shadow_capture || pass==0);++pass) {
        const auto* primary=model->Get_DCG_Array(pass);
        const auto* secondary=model->Get_DIG_Array(pass);
        const auto* uv=model->Get_UV_Array(pass,0);
        const auto* secondary_uv=model->Get_UV_Array(pass,1);
        for (int first=0;first<model->Get_Polygon_Count();) {
            const auto mesh_slot = batch_index++;
            auto shader=model->Get_Shader(first,pass);
            auto* material=model->Peek_Material(polygons[first].I,pass);
            const std::array textures{model->Peek_Texture(first,pass,0),model->Peek_Texture(first,pass,1)};
            int end=first+1;
            while (end<model->Get_Polygon_Count() && model->Get_Shader(end,pass).Get_Bits()==shader.Get_Bits()
                && model->Peek_Material(polygons[end].I,pass)==material
                && model->Peek_Texture(end,pass,0)==textures[0] && model->Peek_Texture(end,pass,1)==textures[1]) ++end;
            GraphicsMaterialDrawOverrides draw_overrides;
            draw_overrides.force_multiply=Graphics::Get_Prop_Draw_Settings().force_multiply
                && shader.Get_Dst_Blend_Func()==ShaderClass::DSTBLEND_ZERO && overrides.opacity==1;
            if (!sorted && overrides.opacity!=1) {
                if (!mesh.Is_Additive()) {
                    shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
                    shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
                }
                draw_overrides.alpha_cutoff=static_cast<unsigned>(96*overrides.opacity)/255.0f;
            }
            if (two_sided) shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
            if (!batch.Begin(source.size(),static_cast<std::size_t>(end-first)*3)) return false;
            const auto* vertex_material = material ? &material->Get_Material_Parameters() : nullptr;
            const auto extract = [&](unsigned index) {
                auto vertex=source[index].Make_Vertex();
                if (primary) vertex.color=Unpack_Mesh_Color(primary[index]);
                if (secondary) vertex.secondary_color=Unpack_Mesh_Color(secondary[index]);
                if (uv) vertex.uv={uv[index].X,uv[index].Y};
                if (secondary_uv) vertex.secondary_uv={secondary_uv[index].X,secondary_uv[index].Y};
                if (vertex_material) Graphics::Apply_Prop_Material(vertex,*vertex_material);
                if (!Graphics::Get_Prop_Draw_Settings().lighting) vertex.material_ambient[3]=0;
                if (!sorted && overrides.opacity!=1 && material
                    && (!material->Get_Lighting() || material->Get_Diffuse_Color_Source()==VertexMaterialClass::MATERIAL)) {
                    vertex.material_diffuse[3]=overrides.opacity;
                    if (mesh.Is_Additive())
                        vertex.material_diffuse[0]=vertex.material_diffuse[1]=vertex.material_diffuse[2]=overrides.opacity;
                }
                return vertex;
            };
            {
                PROFILER_SECTION_NAME("Graphics.Mesh.BuildMaterialBatch");
                for (int polygon=first;polygon<end;++polygon)
                    for (unsigned corner=0;corner<3;++corner)
                        if (!batch.Append(polygons[polygon][corner],extract)) return false;
            }
            auto parameters=context;
            Extract_Mesh_Mappers(parameters,material,mesh,sorted);
            draw_overrides.shadow_capture = overrides.shadow_capture;
            if (overrides.shadow_capture) {
                // Emissive/additive and modulation layers do not form an
                // opaque silhouette. Alpha-blended geometry uses its cutout.
                if (shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ONE
                    || shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ZERO) {
                    first=end;
                    continue;
                }
                if (shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA) {
                    shader.Set_Alpha_Test(ShaderClass::ALPHATEST_ENABLE);
                    draw_overrides.alpha_cutoff = 96.0f/255.0f;
                }
            }
            draw_overrides.mesh = meshes.Synchronize(renderer,mesh_slot,batch.Vertices(),batch.Indices());
            if (!draw_overrides.mesh.Is_Valid()) return false;
            if (!Draw_Graphics_Material_Geometry(batch.Vertices(),batch.Indices(),view_projection,shader,textures,
                parameters,sorted ? &view : nullptr,draw_overrides)) success=false;
            first=end;
        }
    }
    for (int pass_index=0;!overrides.shadow_capture && pass_index<info.Additional_Pass_Count();++pass_index) {
        auto* pass=info.Peek_Additional_Pass(pass_index);
        if (mesh.Is_Translucent() && !pass->Is_Enabled_On_Translucent_Meshes()) continue;
        GraphicsMaterialPassDescription description;
        if (!pass->Describe_Graphics_Pass(description)) { success=false; continue; }
        SimpleDynVecClass<uint32> selected;
        if (!skin && pass->Get_Cull_Volume() && MaterialPassClass::Is_Per_Polygon_Culling_Enabled()) {
            Matrix3D inverse;
            mesh.Get_Transform().Get_Orthogonal_Inverse(inverse);
            OBBoxClass local_box;
            OBBoxClass::Transform(inverse,*pass->Get_Cull_Volume(),&local_box);
            Vector3 direction;
            local_box.Basis.Get_Z_Vector(&direction);
            direction=-direction;
            if (model->Has_Cull_Tree()) model->Generate_Rigid_APT(local_box,direction,selected);
            else model->Generate_Rigid_APT(direction,selected);
        } else {
            for (int i=0;i<model->Get_Polygon_Count();++i) selected.Add(i);
        }
        if (!batch.Begin(source.size(),static_cast<std::size_t>(selected.Count())*3)) return false;
        const auto* vertex_material = description.material ? &description.material->Get_Material_Parameters() : nullptr;
        const auto extract = [&](unsigned index) {
            auto vertex=source[index].Make_Vertex();
            if (const auto* primary=model->Get_DCG_Array(0)) vertex.color=Unpack_Mesh_Color(primary[index]);
            if (const auto* secondary=model->Get_DIG_Array(0)) vertex.secondary_color=Unpack_Mesh_Color(secondary[index]);
            if (const auto* uv=model->Get_UV_Array(0,0)) vertex.uv={uv[index].X,uv[index].Y};
            if (const auto* uv=model->Get_UV_Array(0,1)) vertex.secondary_uv={uv[index].X,uv[index].Y};
            if (vertex_material) Graphics::Apply_Prop_Material(vertex,*vertex_material);
            if (description.material) {
                if (overrides.pass_opacity!=1 && (!description.material->Get_Lighting()
                    || description.material->Get_Diffuse_Color_Source()==VertexMaterialClass::MATERIAL))
                    vertex.material_diffuse[3]=overrides.pass_opacity;
                if (description.material->Get_Emissive_Color_Source()==VertexMaterialClass::MATERIAL)
                    for (unsigned c=0;c<3;++c) vertex.material_emissive[c]*=overrides.pass_emissive;
            }
            return vertex;
        };
        for (int i=0;i<selected.Count();++i)
            for (unsigned corner=0;corner<3;++corner)
                if (!batch.Append(polygons[selected[i]][corner],extract)) return false;
        if (batch.Indices().empty()) continue;
        auto parameters=context;
        if (description.world_coordinates) {
            parameters.uv_sources[0]=4;
            std::memcpy(parameters.uv_transform[0].data(),&description.world_texture_transform,
                sizeof(description.world_texture_transform));
        } else Extract_Graphics_Texture_Mappers(parameters,description.material);
        if (two_sided) description.shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
        GraphicsMaterialDrawOverrides draw_overrides;
        draw_overrides.color_write_mask=description.color_write_mask;
        draw_overrides.deferred_pass=additional_only;
        draw_overrides.mesh = mesh_state->additional.Synchronize(
            renderer,pass_index,batch.Vertices(),batch.Indices());
        if (!draw_overrides.mesh.Is_Valid()) return false;
        if (!Draw_Graphics_Material_Geometry(batch.Vertices(),batch.Indices(),view_projection,description.shader,
            description.textures,parameters,sorted ? &view : nullptr,draw_overrides)) success=false;
    }
    return success;
}
