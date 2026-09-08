#pragma once

#include <span>
import Graphics.Scene.Surfaces.Geometry;
import Graphics.Materials.State;
#include "WWMath/matrix4.h"

class TextureClass;

// Translate prelit CPU geometry at the WW3D boundary. Resources and material
// submission are owned by the graphics material renderer.
bool Draw_Graphics_Prelit_Geometry(std::span<const Graphics::SurfaceVertex> vertices,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    Graphics::MaterialState shader, TextureClass* texture, const Matrix4x4* sorting_view = nullptr, bool texture_luminance = false);
