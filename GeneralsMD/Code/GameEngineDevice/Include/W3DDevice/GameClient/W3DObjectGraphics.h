#pragma once
#include <array>
#include <memory>
#include <span>
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Lighting;
class RenderObjClass;
class RenderInfoClass;
class W3DShroud;

// Translates asset geometry and material state; graphics owns mesh resources,
// material shading, and submission. A cache belongs to its game-side owner.
class W3DObjectGraphics final
{
public:
    W3DObjectGraphics();
    ~W3DObjectGraphics();
    W3DObjectGraphics(const W3DObjectGraphics&) = delete;
    W3DObjectGraphics& operator=(const W3DObjectGraphics&) = delete;
    void Invalidate();
    bool Render(RenderObjClass& object, RenderInfoClass& info,
        const Graphics::PropLighting& lighting, W3DShroud* shroud, bool background = false);
private:
    struct State;
    std::unique_ptr<State> m_state;
};
