#pragma once
#include <array>
#include <optional>
#include <span>
#include "WW3D2/Shader.h"
#include "WWMath/matrix4.h"
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Material;
class TextureClass;

struct GraphicsMaterialDrawOverrides
{
    float alpha_cutoff=-1;
    int color_write_mask=-1;
    bool deferred_pass=false;
    bool force_multiply=false;
    bool shadow_capture=false;
};

void Flush_Graphics_Material_Passes();
void Clear_Graphics_Shadow_Geometry();

// Material vertices are extracted by the asset adapter. Graphics owns their
// upload and submission, including resources retained for transparent sorting.
bool Draw_Graphics_Material_Geometry(std::span<const Graphics::PropVertex> vertices,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    ShaderClass shader, std::array<TextureClass*,2> textures,
    Graphics::PropParameters parameters, const Matrix4x4* sorting_view = nullptr,
    GraphicsMaterialDrawOverrides overrides = {});

class VertexMaterialClass;
// Translate authored values; graphics owns color-source and opacity evaluation.
std::optional<Graphics::PropMaterial> Describe_Graphics_Vertex_Material(VertexMaterialClass* material);
void Extract_Graphics_Texture_Mappers(Graphics::PropParameters& parameters,
    VertexMaterialClass* material);

class LightEnvironmentClass;
void Extract_Graphics_Lighting(Graphics::PropParameters& parameters,
    const LightEnvironmentClass* environment);
