#include <unordered_map>
#include "W3DDevice/GameClient/W3DGraphicsResources.h"
#include "WW3D2/Texture.h"
#include "WW3D2/Camera.h"
#include "WW3D2/WW3D.h"
#include "W3DDevice/GameClient/W3DShroud.h"
import Graphics.Backends.DX11.Coexistence;

namespace
{
Graphics::Device *texture_device = nullptr;
std::unordered_map<void *, Graphics::RHITextureHandle> imported_textures;
}

Graphics::RHITextureHandle Resolve_Graphics_Texture(TextureBaseClass *texture)
{
    auto *device = Graphics::Shared_Frame_Device();
    auto *backend = WW3D::Get_Render_Backend();
    if (device == nullptr || backend == nullptr || texture == nullptr
        || !texture->Ensure_Render_Backend_Texture() || texture->Is_Missing_Texture()) return {};
    if (texture_device != nullptr && texture_device != device) return {};
    texture_device = device;
    void *resource = nullptr;
    void *view = nullptr;
    if (!backend->Get_Shared_Texture_Resources(texture->Peek_Render_Backend_Texture(), resource, view)) return {};
    const auto found = imported_textures.find(resource);
    if (found != imported_textures.end()) return found->second;
    const auto handle = Graphics::Import_Shared_Texture(resource, view);
    if (handle.Is_Valid()) imported_textures.emplace(resource, handle);
    return handle;
}

void Release_Graphics_Textures() noexcept
{
    if (texture_device != nullptr)
        for (const auto &entry : imported_textures) texture_device->Destroy_Texture(entry.second);
    imported_textures.clear();
    texture_device = nullptr;
}

Graphics::SurfaceParameters Make_Surface_Parameters(CameraClass &camera)
{
    Matrix3D view;
    Matrix4x4 projection;
    camera.Get_View_Matrix(&view);
    camera.Get_Backend_Projection_Matrix(&projection);
    const Matrix4x4 transform = projection * Matrix4x4(view);
    Graphics::SurfaceParameters parameters;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            parameters.view_projection[row * 4 + column] = transform[row][column];
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
