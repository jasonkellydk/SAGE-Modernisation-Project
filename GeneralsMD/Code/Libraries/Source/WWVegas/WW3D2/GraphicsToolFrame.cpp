#include <filesystem>
#include "GraphicsToolFrame.h"
#include "GraphicsGeometry.h"
#include "WW3D.h"

import Graphics.Backends.DX11.Coexistence;
import Graphics.Frame.SceneRenderers;
import Graphics.Renderer2D;

namespace
{
bool Initialize_Tool_Renderers(Graphics::Device& device)
{
    return Graphics::Initialize_Scene_Renderers(device, std::filesystem::path("GraphicsShaders"));
}

bool Draw_Tool_Overlays(Graphics::Device& device, Graphics::CommandList& commands,
    const Graphics::FrameTargets& targets) noexcept
{
    return Graphics::Get_Renderer2D().Execute(device, commands,
        targets.backbuffer.texture, targets.depth.texture,
        {0,0,targets.backbuffer.width,targets.backbuffer.height,0,1});
}
}

bool Begin_Graphics_Tool_Frame()
{
    auto* backend = WW3D::Get_Render_Backend();
    DX11SharedFrameResources resources{};
    if (backend == nullptr || !backend->Get_Shared_Frame_Resources(resources)) return false;
    if (Graphics::Shared_Frame_Device() == nullptr) {
        if (!Graphics::Graphics_DX11_Initialize_Shared_Frame(resources.device, resources.context,
            resources.swap_chain, resources.back_buffer, resources.back_buffer_view,
            resources.depth_buffer, resources.depth_buffer_view, resources.width, resources.height)
            || !Graphics::Register_Graphics_Phase_Executor(&Initialize_Tool_Renderers, &Draw_Tool_Overlays)) {
            Shutdown_Graphics_Tool_Frame();
            return false;
        }
    } else if (!Graphics::Graphics_DX11_Update_Shared_Frame(resources.device, resources.context,
        resources.swap_chain, resources.back_buffer, resources.back_buffer_view,
        resources.depth_buffer, resources.depth_buffer_view, resources.width, resources.height)) return false;
    if (!Graphics::Graphics_DX11_Begin_Frame()) return false;
    Graphics::Get_Renderer2D().Begin(resources.width, resources.height);
    return true;
}

bool End_Graphics_Tool_Frame()
{
    if (Graphics::Graphics_DX11_Begin_Graphics_Phase()
        && Graphics::Graphics_DX11_End_Frame() && Graphics::Graphics_DX11_Present()) return true;
    Graphics::Graphics_DX11_Abort_Frame();
    return false;
}

void Shutdown_Graphics_Tool_Frame() noexcept
{
    Clear_Graphics_Transparent_Geometry();
    Graphics::Shutdown_Scene_Renderers();
    Graphics::Graphics_DX11_Shutdown_Shared_Frame();
}

void Abort_Graphics_Tool_Frame() noexcept
{
    Graphics::Graphics_DX11_Abort_Frame();
}
