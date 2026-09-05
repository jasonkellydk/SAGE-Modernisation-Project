#pragma once
#include <array>
#include "WW3D2/Shader.h"
#include "WWMath/matrix4.h"
class VertexMaterialClass;
class TextureClass;

// A procedural pass describes material data without installing GPU state.
struct GraphicsMaterialPassDescription
{
    ShaderClass shader;
    VertexMaterialClass* material=nullptr;
    std::array<TextureClass*,2> textures{};
    bool world_coordinates=false;
    Matrix4x4 world_texture_transform{true};
    unsigned char color_write_mask=15;
};
