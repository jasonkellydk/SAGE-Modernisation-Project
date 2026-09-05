#pragma once

class MeshClass;
class RenderInfoClass;
class MaterialPassClass;

struct GraphicsMeshOverrides
{
    float opacity=1;
    float pass_opacity=1;
    float pass_emissive=1;
    bool shadow_capture=false;
};

// Translate CPU mesh, deformation, and material data at the asset boundary.
// GPU resources and submission are owned by the graphics material renderer.
bool Draw_Graphics_Mesh(MeshClass& mesh,RenderInfoClass& info,const GraphicsMeshOverrides& overrides);
