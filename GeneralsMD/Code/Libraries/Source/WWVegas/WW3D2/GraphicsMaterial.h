#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>
import Graphics.Materials.State;
#include "WWMath/matrix4.h"
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Material;
import Graphics.Scene.MuzzleFlash;
class TextureClass;

struct GraphicsMaterialDrawOverrides
{
    float alpha_cutoff=-1;
    int color_write_mask=-1;
    bool deferred_pass=false;
    bool force_multiply=false;
    bool shadow_capture=false;
    bool decal_pass=false;
    Graphics::MuzzleFlashDesignation muzzle_flash=Graphics::MuzzleFlashDesignation::None;
    Graphics::PropMeshHandle mesh{};
};


// Material vertices are extracted by the asset adapter. Graphics owns their
// upload and submission, including resources retained for transparent sorting.
bool Draw_Graphics_Material_Geometry(std::span<const Graphics::PropVertex> vertices,
    std::span<const unsigned> indices, const Matrix4x4& transform,
    Graphics::MaterialState shader, std::array<TextureClass*,2> textures,
    Graphics::PropParameters parameters, const Matrix4x4* sorting_view = nullptr,
    GraphicsMaterialDrawOverrides overrides = {});

import Graphics.Materials.MeshMaterial;
void Extract_Graphics_Texture_Mappers(Graphics::PropParameters& parameters,
    const Graphics::MeshMaterial* material);

import Graphics.Scene.Props.LightingParameters;
