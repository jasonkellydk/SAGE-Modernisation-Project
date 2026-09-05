#include "MeshRenderer.h"
#include "WW3D2/GraphicsMaterial.h"
#include "WW3D2/GraphicsGeometry.h"
#include "DecalMsh.h"
#include "WW3D.h"

MeshRendererClass TheMeshRenderer;

void MeshRendererClass::Init() { Invalidate(); }
void MeshRendererClass::Shutdown() { Invalidate(true); m_camera=nullptr; }
void MeshRendererClass::Invalidate(bool)
{
    m_decals=nullptr;
    Clear_Graphics_Transparent_Geometry();
}
void MeshRendererClass::Add_To_Render_List(DecalMeshClass* decal)
{
    WWASSERT(decal);
    decal->Set_Next_Visible(m_decals);
    m_decals=decal;
}
void MeshRendererClass::Flush()
{
    if (!m_camera) return;
    auto* backend=WW3D::Get_Render_Backend();
    const auto saved_bias=backend->Get_Depth_Bias();
    if (m_decals) {
        backend->Set_Depth_Bias(8);
        auto* decal=m_decals;
        m_decals=nullptr;
        while (decal) {
            auto* next=decal->Peek_Next_Visible();
            decal->Render();
            decal=next;
        }
        backend->Set_Depth_Bias(saved_bias);
    }
    Flush_Graphics_Material_Passes();
}
