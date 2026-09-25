#include "W3DDevice/GameClient/W3DGraphicsResources.h"
#include "W3DDevice/GameClient/W3DTextureHandle.h"
#include "W3DDevice/GameClient/W3DCamera.h"
#include "W3DDevice/GameClient/W3DShroud.h"
import Graphics.Frame.Runtime;
import Graphics.Resources.Textures.References;

namespace
{
Graphics::TextureReferences texture_references;
}

Graphics::RHITextureHandle Resolve_Graphics_Texture(W3DTextureHandle *texture)
{
    auto* device = Graphics::Shared_Frame_Device();
    if (device == nullptr || texture == nullptr || !texture->Ensure_Render_Backend_Texture()
        || texture->Is_Missing_Texture()) return {};
    return texture_references.Retain(*device, texture->Peek_Graphics_Texture());
}

void Release_Graphics_Textures() noexcept
{
    texture_references.Clear();
}

Graphics::SurfaceParameters Make_Surface_Parameters(W3DCamera &camera)
{
	Graphics::SurfaceParameters parameters;
	parameters.view_projection = camera.Build_Render_Matrices().view_projection;
	return parameters;
}

Graphics::RHITextureHandle Set_Surface_Shroud(Graphics::SurfaceParameters &parameters, W3DShroud *shroud)
{
    parameters.shroud = 0;
    if (shroud == nullptr || shroud->getCellWidth() <= 0 || shroud->getCellHeight() <= 0
        || shroud->getTextureWidth() <= 0 || shroud->getTextureHeight() <= 0) return {};
    const auto texture = Resolve_Graphics_Texture(shroud->getShroudTexture());
    if (!texture.Is_Valid()) return {};
    const float sx = 1.0f / (shroud->getCellWidth() * shroud->getTextureWidth());
    const float sy = 1.0f / (shroud->getCellHeight() * shroud->getTextureHeight());
    parameters.shroud_projection = {sx, sy, (-shroud->getDrawOriginX() + shroud->getCellWidth()) * sx,
        (-shroud->getDrawOriginY() + shroud->getCellHeight()) * sy};
    parameters.shroud = 1;
    return texture;
}
