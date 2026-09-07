import Graphics.Frame.AttachmentBindings;
import Graphics.Scene.Props.Submission;
#include "W3DDevice/GameClient/W3DDirectionalShadows.h"
#include "rts/profile.h"
#include "Common/GlobalData.h"
#include "Common/DrawModule.h"
#include "GameClient/Shadow.h"
#include "WW3D2/Camera.h"
#include "WW3D2/GraphicsMesh.h"
#include "WW3D2/GraphicsMaterial.h"
#include "WW3D2/HLOD.h"
#include "WW3D2/Mesh.h"
#include "WW3D2/RInfo.h"
#include "WW3D2/WW3D.h"
#include <algorithm>

import Graphics.Backends.DX11.FrameRuntime;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Graphics.Scene.Lighting.Environment;

namespace
{
class DirectionalShadow;
DirectionalShadow* first_shadow = nullptr;

class DirectionalShadow final : public Shadow
{
public:
    explicit DirectionalShadow(RenderObjClass* object) : object(object)
    {
        m_type = SHADOW_VOLUME;
        m_isEnabled = TRUE;
        m_isInvisibleEnabled = FALSE;
        m_x = m_y = m_z = 0;
        setSize(0,0);
        next = first_shadow;
        if (next != nullptr) next->previous = this;
        first_shadow = this;
    }

    void release() override
    {
        if (previous != nullptr) previous->next = next;
        else first_shadow = next;
        if (next != nullptr) next->previous = previous;
        delete this;
    }

#if defined(RTS_DEBUG)
    void getRenderCost(RenderCost& cost) const override
    {
        if (m_isEnabled && !m_isInvisibleEnabled) cost.addShadowDrawCalls(draw_count);
    }
#endif

    RenderObjClass* object;
    DirectionalShadow* next = nullptr;
    DirectionalShadow* previous = nullptr;
    int draw_count = 0;
};

bool Collect_Object(RenderObjClass& object,RenderInfoClass& info,int& draw_count)
{
    // Visibility in the main camera is deliberately not a caster filter:
    // offscreen geometry can project a shadow into the camera frustum.
    // Composite flags are the union of child flags. An attached additive
    // effect must not suppress the opaque parent or its other children.
    // Material extraction filters individual additive batches.
    if (!object.Is_Not_Hidden_At_All()) return true;
    object.Validate_Transform();
    if (object.Class_ID() == RenderObjClass::CLASSID_MESH) {
        GraphicsMeshOverrides overrides;
        overrides.shadow_capture = true;
        ++draw_count;
        return Draw_Graphics_Mesh(static_cast<MeshClass&>(object),info,overrides);
    }
    if (object.Class_ID() == RenderObjClass::CLASSID_HLOD) {
        auto& hierarchy = static_cast<HLodClass&>(object);
        object.Update_Sub_Object_Transforms();
        const int lod = hierarchy.Get_LOD_Level();
        for (int index=0;index<hierarchy.Get_Lod_Model_Count(lod);++index) {
            auto* child = hierarchy.Peek_Lod_Model(lod,index);
            if (child != nullptr && !Collect_Object(*child,info,draw_count)) return false;
        }
        for (int index=0;index<hierarchy.Get_Additional_Model_Count();++index) {
            auto* child = hierarchy.Peek_Additional_Model(index);
            if (child != nullptr && !Collect_Object(*child,info,draw_count)) return false;
        }
    }
    return true;
}
}

Shadow* Create_Directional_Shadow(RenderObjClass* object)
{
    return object != nullptr ? new DirectionalShadow(object) : nullptr;
}

void Reset_Directional_Shadows()
{
    Graphics::Get_Prop_Submission().Clear_Shadows();
    while (first_shadow != nullptr) first_shadow->release();
    auto& environment = Graphics::Get_Environment_Lighting();
    environment.parameters.shadow_options[0] = 0;
    environment.shadow_textures = {};
}

bool Collect_Directional_Shadow_Casters(RenderInfoClass& info)
{
    PROFILER_SECTION_NAME("Graphics.Shadows.Collect");
    Graphics::Get_Prop_Submission().Clear_Shadows();
    Graphics::Get_Environment_Lighting().parameters.shadow_options[0] = 0;
    for (auto* shadow=first_shadow;shadow!=nullptr;shadow=shadow->next) {
        shadow->draw_count = 0;
        if (!shadow->isRenderEnabled() || shadow->isInvisibleEnabled()) continue;
        if (!Collect_Object(*shadow->object,info,shadow->draw_count)) return false;
        shadow->draw_count *= 4;
    }
    return true;
}

bool Render_Directional_Shadow_Maps(RenderInfoClass& info)
{
    auto* device = Graphics::Shared_Frame_Device();

    if (device == nullptr || TheGlobalData == nullptr) return false;
    Matrix3D view_matrix;
    Matrix4x4 projection;
    info.Camera.Get_View_Matrix(&view_matrix);
    info.Camera.Get_Backend_Projection_Matrix(&projection);
    Graphics::View view;
    view.view_matrix = Graphics::Matrix4x4::Identity();
    for (int row=0;row<4;++row)
        for (int column=0;column<4;++column) {
            view.projection_matrix.values[row*4+column] = projection[row][column];
            if (row<3) view.view_matrix.values[row*4+column] = view_matrix[row][column];
        }
    Graphics::ShadowSettings settings;
    info.Camera.Get_Clip_Planes(settings.near_clip,settings.far_clip);
    settings.map_size = 2048;
    settings.depth_padding = 400;
    const auto& direction = TheGlobalData->m_terrainLightPos[0];
    Graphics::RenderLight light;
    light.type = Graphics::RenderLightType::Directional;
    light.flags = Graphics::RenderLightFlags::Enabled;
    light.direction = {direction.x,direction.y,direction.z};
    Graphics::RHIViewport viewport;
    viewport = Graphics::Get_Attachment_Bindings().Current().viewport;
    const auto saved_target=Graphics::Get_Attachment_Bindings().Capture();
    const auto color = device->Get_Swap_Chain().Backbuffer();
    const auto depth = device->Get_Swap_Chain().Depth_Target();
    const bool rendered = Graphics::Get_Directional_Shadow_Renderer().Render(
        device->Immediate_Command_List(),view,light,settings,color.texture,depth.texture,
        viewport);
    Graphics::Get_Attachment_Bindings().Restore(saved_target);
    Graphics::Get_Prop_Submission().Clear_Shadows();
    return rendered;
}
