import Graphics.Materials.State;
#include <array>
#include "rts/profile.h"
#include "../../../../../../engine/graphics/profiling/Tracy.h"
#include <vector>
#include <cstring>
#include "GraphicsMesh.h"
#include "GraphicsMaterial.h"
#include "Mesh.h"
#include "MeshMdl.h"
import Graphics.Materials.ProceduralPass;
import Graphics.Materials.MeshMaterial;
import Graphics.Scene.Props.Material;
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

void Extract_Mesh_Mappers(Graphics::PropParameters& parameters,const Graphics::MeshMaterial* material,
    MeshClass& mesh,bool sorted)
{
    auto* mapper=material ? material->mappings[0].get() : nullptr;
    auto* linear=mapper ? mapper->Linear_Scroll() : nullptr;
    const auto* data=mesh.Get_User_Data();
    if (sorted || !data || *static_cast<const int*>(data)!=RenderObjClass::USER_DATA_MATERIAL_OVERRIDE
        || !linear) {
        Extract_Graphics_Texture_Mappers(parameters,material);
        return;
    }
    const auto saved_offset=linear->offset;
    const auto saved_time=linear->last_time;
    const auto& offset=static_cast<const RenderObjClass::Material_Override*>(data)->customUVOffset;
    linear->offset={offset.X,offset.Y};
    linear->last_time=WW3D::Get_Sync_Time();
    Extract_Graphics_Texture_Mappers(parameters,material);
    linear->offset=saved_offset;
    linear->last_time=saved_time;
}
}

bool Draw_Graphics_Mesh(MeshClass& mesh,RenderInfoClass& info,const GraphicsMeshOverrides& overrides)
{
    PROFILER_SECTION_NAME("Graphics.Mesh.ExtractDraw");
    if (mesh.Get_Muzzle_Flash_Designation() != Graphics::MuzzleFlashDesignation::None
        && overrides.shadow_capture) return true;
    auto* model=mesh.Peek_Model();
    if (!model || model->Get_Vertex_Count()==0 || model->Get_Polygon_Count()==0) return true;
    mesh.Validate_Transform();
    Matrix3D world=mesh.Get_Transform();
    const auto* positions=model->Peek_Vertex_Array();
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
        PROFILER_SECTION_NAME("Graphics.Mesh.Deform");
        deformed_positions.resize(model->Get_Vertex_Count());
        deformed_normals.resize(model->Get_Vertex_Count());
        mesh.Get_Deformed_Vertices(deformed_positions.data(),deformed_normals.data());
        positions=deformed_positions.data(); normals=deformed_normals.data();
        world.Make_Identity();
    }
    auto workspace = Graphics::Get_Prop_Extraction_Cache().Acquire();
    std::span<Graphics::PropSourceVertex> source;
    const auto vertex_count = static_cast<std::size_t>(model->Get_Vertex_Count());
    const auto prepare_source = [&] {
        if (!source.empty()) return;
        PROFILER_SECTION_NAME("Graphics.Mesh.TransformVertices");
        source = workspace.Workspace().Prepare_Source(vertex_count, [positions,normals](Graphics::PropSourceVertex& vertex,std::size_t i) {
            vertex.position={positions[i].X,positions[i].Y,positions[i].Z};
            if (normals) vertex.normal={normals[i].X,normals[i].Y,normals[i].Z};
            else vertex.normal={0,0,1};
        });
    };
    // Deformation is per instance. Raw skin output keeps exact-content checks;
    // rigid source storage can prove its version without copying every vertex.
    const auto geometry_revision = skin ? 0 : model->Geometry_Revision();
    // Preserve the owned entry snapshot for untracked/writable geometry and
    // deformation, including nested extractions that reuse temporary storage.
    if (geometry_revision == 0) prepare_source();
    Matrix4x4 view,view_projection;
    const auto& camera_matrices = Graphics::Get_Camera_Matrices();
    {
        GRAPHICS_PROFILE_FOCUS_SCOPE("Graphics.Mesh.ComposeCamera");
        std::copy_n(camera_matrices.view.values.data(), 16, &view[0][0]);
        const auto composed = Graphics::Compose_Matrices(camera_matrices.projection,camera_matrices.view);
        std::copy_n(composed.values.data(), 16, &view_projection[0][0]);
    }
    Graphics::PropParameters context;
    context.normal_in_world_space = normals ? 0.0f : 1.0f;
    for (unsigned row=0;row<3;++row)
        for (unsigned column=0;column<4;++column)
            context.world[row*4+column]=world[row][column];
    context.view = camera_matrices.view.values;
    const auto camera=info.Camera.Get_Position();
    context.camera_position={camera.X,camera.Y,camera.Z,1};
    {
        GRAPHICS_PROFILE_FOCUS_SCOPE("Graphics.Mesh.SetLighting");
        Graphics::Set_Prop_Lighting(context,mesh.Get_Lighting_Environment());
    }
    const bool sorted=model->Get_Flag(MeshGeometryClass::SORT) && WW3D::Is_Sorting_Enabled();
    const bool additional_only=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_ADDITIONAL_PASSES_ONLY)!=0;
    const bool shadow=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_SHADOW_RENDERING)!=0;
    const bool render_base=!additional_only || (shadow && (model->Get_Single_Shader().Get_Alpha_Test()==Graphics::MaterialState::ALPHATEST_ENABLE
        || model->Get_Single_Shader().Get_Src_Blend_Func()==Graphics::MaterialState::SRCBLEND_SRC_ALPHA));
    const bool two_sided=(info.Current_Override_Flags() & RenderInfoClass::RINFO_OVERRIDE_FORCE_TWO_SIDED)!=0;
    auto& batch = workspace.Workspace().Batch();
    // Deformed vertices belong to an instance. Rigid geometry can continue to
    // share its model's prepared batches; skins must not evict one another.
    auto*& mesh_state = skin ? mesh.Graphics_Mesh_State() : model->Graphics_Mesh_State();
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
        const bool uniform_material = !model->Has_Shader_Array(pass) && !model->Has_Material_Array(pass)
            && !model->Has_Texture_Array(pass,0) && !model->Has_Texture_Array(pass,1);
        for (int first=0;first<model->Get_Polygon_Count();) {
            const auto mesh_slot = batch_index++;
            auto shader=model->Get_Shader(first,pass);
            auto* material=model->Peek_Material(polygons[first].I,pass);
            const std::array textures{model->Peek_Texture(first,pass,0),model->Peek_Texture(first,pass,1)};
            int end=uniform_material ? model->Get_Polygon_Count() : first+1;
            while (end<model->Get_Polygon_Count() && model->Get_Shader(end,pass).Get_Bits()==shader.Get_Bits()
                && model->Peek_Material(polygons[end].I,pass)==material
                && model->Peek_Texture(end,pass,0)==textures[0] && model->Peek_Texture(end,pass,1)==textures[1]) ++end;
            GraphicsMaterialDrawOverrides draw_overrides;
            draw_overrides.force_multiply=Graphics::Get_Prop_Draw_Settings().force_multiply
                && shader.Get_Dst_Blend_Func()==Graphics::MaterialState::DSTBLEND_ZERO && overrides.opacity==1;
            if (!sorted && overrides.opacity!=1) {
                if (!mesh.Is_Additive()) {
                    shader.Set_Src_Blend_Func(Graphics::MaterialState::SRCBLEND_SRC_ALPHA);
                    shader.Set_Dst_Blend_Func(Graphics::MaterialState::DSTBLEND_ONE_MINUS_SRC_ALPHA);
                }
                draw_overrides.alpha_cutoff=static_cast<unsigned>(96*overrides.opacity)/255.0f;
            }
            if (two_sided) shader.Set_Cull_Mode(Graphics::MaterialState::CULL_MODE_DISABLE);
            const auto* vertex_material = material ? &material->parameters : nullptr;
            const std::array preparation_state{overrides.opacity,sorted ? 1.0f : 0.0f,
                Graphics::Get_Prop_Draw_Settings().lighting ? 1.0f : 0.0f,mesh.Is_Additive() ? 1.0f : 0.0f};
            const auto channel_bytes = [&](const auto* values) {
                return std::as_bytes(std::span(values,values ? vertex_count : 0));
            };
            const std::array<Graphics::PropPreparationInput,9> preparation_sources{{
                {geometry_revision != 0 ? channel_bytes(positions) : std::as_bytes(source),geometry_revision},
                {geometry_revision != 0 ? channel_bytes(normals) : std::span<const std::byte>{},geometry_revision},
                {std::as_bytes(std::span(polygons+first,static_cast<std::size_t>(end-first))),geometry_revision},
                {channel_bytes(primary)}, {channel_bytes(secondary)},
                {channel_bytes(uv),model->UV_Revision(pass,0)},
                {channel_bytes(secondary_uv),model->UV_Revision(pass,1)},
                {std::as_bytes(std::span(vertex_material,vertex_material ? 1u : 0u))},
                {std::as_bytes(std::span(preparation_state))}}};
            auto prepared_mesh = meshes.Find_Versioned(renderer,mesh_slot,preparation_sources);
            const auto extract = [&](unsigned index) {
                auto vertex=source[index].Make_Vertex();
                if (primary) vertex.color=Unpack_Mesh_Color(primary[index]);
                if (secondary) vertex.secondary_color=Unpack_Mesh_Color(secondary[index]);
                if (uv) vertex.uv={uv[index].X,uv[index].Y};
                if (secondary_uv) vertex.secondary_uv={secondary_uv[index].X,secondary_uv[index].Y};
                if (vertex_material) Graphics::Apply_Prop_Material(vertex,*vertex_material);
                if (!Graphics::Get_Prop_Draw_Settings().lighting) vertex.material_ambient[3]=0;
                if (!sorted && overrides.opacity!=1 && material
                    && (!material->parameters.lighting || material->parameters.diffuse_source==Graphics::PropColorSource::Material)) {
                    vertex.material_diffuse[3]=overrides.opacity;
                    if (mesh.Is_Additive())
                        vertex.material_diffuse[0]=vertex.material_diffuse[1]=vertex.material_diffuse[2]=overrides.opacity;
                }
                return vertex;
            };
            if (!prepared_mesh.Is_Valid()) {
                PROFILER_SECTION_NAME("Graphics.Mesh.BuildMaterialBatch");
                prepare_source();
                if (!batch.Begin(source.size(),static_cast<std::size_t>(end-first)*3)) return false;
                for (int polygon=first;polygon<end;++polygon)
                    for (unsigned corner=0;corner<3;++corner)
                        if (!batch.Append(polygons[polygon][corner],extract)) return false;
            }
            auto parameters=context;
            Extract_Mesh_Mappers(parameters,material,mesh,sorted);
            draw_overrides.shadow_capture = overrides.shadow_capture;
            draw_overrides.muzzle_flash = mesh.Get_Muzzle_Flash_Designation();
            if (draw_overrides.muzzle_flash != Graphics::MuzzleFlashDesignation::None)
                parameters.opacity = overrides.opacity;
            if (overrides.shadow_capture) {
                // Emissive/additive and modulation layers do not form an
                // opaque silhouette. Alpha-blended geometry uses its cutout.
                if (shader.Get_Dst_Blend_Func() == Graphics::MaterialState::DSTBLEND_ONE
                    || shader.Get_Src_Blend_Func() == Graphics::MaterialState::SRCBLEND_ZERO) {
                    first=end;
                    continue;
                }
                if (shader.Get_Src_Blend_Func() == Graphics::MaterialState::SRCBLEND_SRC_ALPHA) {
                    shader.Set_Alpha_Test(Graphics::MaterialState::ALPHATEST_ENABLE);
                    draw_overrides.alpha_cutoff = 96.0f/255.0f;
                }
            }
            if (!prepared_mesh.Is_Valid())
                prepared_mesh = meshes.Publish_Versioned(renderer,mesh_slot,preparation_sources,batch.Vertices(),batch.Indices());
            draw_overrides.mesh = prepared_mesh;
            if (!draw_overrides.mesh.Is_Valid()) return false;
            const auto* geometry = renderer.Mesh_Geometry(prepared_mesh);
            if (!Draw_Graphics_Material_Geometry(geometry->Vertices(),geometry->Indices(),view_projection,shader,textures,
                parameters,sorted ? &view : nullptr,draw_overrides)) success=false;
            first=end;
        }
    }
    for (int pass_index=0;!overrides.shadow_capture && pass_index<info.Additional_Pass_Count();++pass_index) {
        auto* pass=info.Peek_Additional_Pass(pass_index);
        if (mesh.Is_Translucent() && !pass->enabled_on_translucent) continue;
        NativeMaterialPass::Description description;
        if (!pass->Describe(description)) { success=false; continue; }
        SimpleDynVecClass<uint32> selected;
        if (!skin && pass->cull_bounds) {
            Matrix3D inverse;
            mesh.Get_Transform().Get_Orthogonal_Inverse(inverse);
            OBBoxClass local_box;
            OBBoxClass::Transform(inverse,*pass->cull_bounds,&local_box);
            Vector3 direction;
            local_box.Basis.Get_Z_Vector(&direction);
            direction=-direction;
            if (model->Has_Cull_Tree()) model->Generate_Rigid_APT(local_box,direction,selected);
            else model->Generate_Rigid_APT(direction,selected);
        } else {
            for (int i=0;i<model->Get_Polygon_Count();++i) selected.Add(i);
        }
        prepare_source();
        if (!batch.Begin(source.size(),static_cast<std::size_t>(selected.Count())*3)) return false;
        const auto* vertex_material = description.material ? &description.material->parameters : nullptr;
        const auto extract = [&](unsigned index) {
            auto vertex=source[index].Make_Vertex();
            if (const auto* primary=model->Get_DCG_Array(0)) vertex.color=Unpack_Mesh_Color(primary[index]);
            if (const auto* secondary=model->Get_DIG_Array(0)) vertex.secondary_color=Unpack_Mesh_Color(secondary[index]);
            if (const auto* uv=model->Get_UV_Array(0,0)) vertex.uv={uv[index].X,uv[index].Y};
            if (const auto* uv=model->Get_UV_Array(0,1)) vertex.secondary_uv={uv[index].X,uv[index].Y};
            if (vertex_material) Graphics::Apply_Prop_Material(vertex,*vertex_material);
            if (description.material) {
                if (overrides.pass_opacity!=1 && (!description.material->parameters.lighting
                    || description.material->parameters.diffuse_source==Graphics::PropColorSource::Material))
                    vertex.material_diffuse[3]=overrides.pass_opacity;
                if (description.material->parameters.emissive_source==Graphics::PropColorSource::Material)
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
            std::memcpy(parameters.uv_transform[0].data(),description.world_texture_transform.data(),
                sizeof(description.world_texture_transform));
        } else Extract_Graphics_Texture_Mappers(parameters,description.material);
        if (two_sided) description.shader.Set_Cull_Mode(Graphics::MaterialState::CULL_MODE_DISABLE);
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
