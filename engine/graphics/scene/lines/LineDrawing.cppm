module;
#include <array>
export module Graphics.Scene.Lines.Drawing;
export import Graphics.Scene.Surfaces.Renderer;

namespace Graphics {
// Navigation overlays use camera-space joined geometry and additive color.
// They remain visible across terrain while leaving the scene depth untouched.
export bool Draw_Navigation_Line(SurfaceRenderer& renderer, CommandList& commands,
    SurfaceMeshHandle mesh, const std::array<float,16>& projection, RHITextureHandle texture)
{
    SurfaceParameters parameters;
    parameters.view_projection = projection;
    parameters.textured = texture.Is_Valid() ? 1.0f : 0.0f;
    SurfaceStyle style;
    style.blend = RHIBlendMode::Additive;
    style.depth_comparison = RHIComparison::Always;
    const std::array<RHITextureHandle,4> textures{texture,{},{},{}};
    return renderer.Draw(commands,mesh,style,parameters,textures);
}
}
