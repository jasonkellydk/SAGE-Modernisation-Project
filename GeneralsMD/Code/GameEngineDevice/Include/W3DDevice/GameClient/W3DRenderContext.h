#pragma once

#include <memory>
import Engine.Core.Math.OrientedBox3;
import Graphics.Materials.ProceduralPass;
import Graphics.Scene.DrawContext;
#include "WWLib/ref_ptr.h"

class W3DCamera;
class W3DTextureHandle;
using NativeMaterialPass = Graphics::ProceduralMaterialPass<RefCountPtr<W3DTextureHandle>, Engine::Math::OrientedBox3>;

class W3DRenderContext final : public Graphics::SceneDrawContext<NativeMaterialPass>
{
public:
    explicit W3DRenderContext(W3DCamera &camera) : Camera(camera) {}
    W3DCamera &Camera;
};
