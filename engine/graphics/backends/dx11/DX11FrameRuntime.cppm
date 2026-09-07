module;

#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

export module Graphics.Backends.DX11.FrameRuntime;

export import Graphics.Backends.DX11;
export import Graphics.FrameOwner;
import Graphics.Frame.AttachmentBindings;
import Graphics.Frame.ResourceLifecycle;
import Graphics.Resources.Recreation;
import Graphics.Resources.Loading.Queue;

namespace Graphics
{

namespace
{
	std::unique_ptr<DX11Device> g_frame_device;
	FrameOwner g_frame_owner;
	std::optional<DX11DeviceOptions> g_frame_options;
	bool g_exclusive_fullscreen = false;
	bool g_reconfiguring = false;
	bool g_restore_resources = false;

    struct ConfigurationScope final
    {
        ConfigurationScope() { g_reconfiguring = true; }
        ~ConfigurationScope() { g_reconfiguring = false; }
    };

    bool Bind_Frame_Attachments()
    {
        if (!g_frame_device || !g_frame_device->Get_Swap_Chain().Is_Valid()) return false;
        auto& swapchain = g_frame_device->Get_Swap_Chain();
        const auto color = swapchain.Backbuffer();
        return Get_Attachment_Bindings().Initialize(*g_frame_device,
            {color.texture,swapchain.Depth_Target().texture,{0,0,color.width,color.height,0,1}});
    }

    void Release_Device_Resources()
    {
        if (!g_frame_device) return;
        g_frame_owner.Abort(*g_frame_device);
        Get_Resource_Load_Queue().Drain();
        Get_Frame_Resource_Lifecycle().Release();
        Get_Resource_Recreation_Registry().Release();
        Get_Attachment_Bindings().Reset();
        g_frame_owner.Set_Draw_Executor(nullptr);
        g_frame_device.reset();
        g_restore_resources = true;
    }

    bool Create_Frame_Device(const DX11DeviceOptions& options, bool exclusive)
    {
        if (!options.window || !IsWindow(static_cast<HWND>(options.window))) return false;
        auto device = std::make_unique<DX11Device>(options);
        if (!device->Is_Valid() || !device->Get_Swap_Chain().Is_Valid()
            || (exclusive && !device->Set_Exclusive_Fullscreen(true))) return false;
        g_frame_device = std::move(device);
        if (!Bind_Frame_Attachments()) { g_frame_device.reset(); return false; }
        return true;
    }

    void Restore_Device_Resources()
    {
        if (!g_restore_resources) return;
        Get_Frame_Resource_Lifecycle().Reacquire();
        Get_Resource_Recreation_Registry().Recreate();
        g_restore_resources = false;
    }
}

// Release imported handles and renderer resources before shutting down the device.
export Device *Shared_Frame_Device() noexcept
{
    return g_frame_device.get();
}

// The application supplies the native window; this owner creates and retains
// the device, swap chain, and their frame targets.
export bool Initialize_Frame_Device(const DX11DeviceOptions& options)
{
    if (g_reconfiguring || g_frame_device != nullptr || options.window == nullptr
        || g_frame_owner.Phase() != FrameOwnerPhase::Idle) return false;
    ConfigurationScope scope;
    if (!Create_Frame_Device(options,false)) return false;
    g_frame_options = options;
    g_exclusive_fullscreen = false;
    Restore_Device_Resources();
    return true;
}

export bool Frame_Device_Ready() noexcept
{
    return g_frame_device && g_frame_device->Get_Status() == RHIDeviceStatus::Ready
        && g_frame_device->Get_Swap_Chain().Is_Valid()
        && Get_Attachment_Bindings().Default().color.Is_Valid();
}

// Explicit recreation also lets applications recover from platform changes.
// Registered owners release while the previous device is alive, and rebuild
// only after the replacement device and its default attachments are ready.
export bool Recreate_Frame_Device()
{
    if (g_reconfiguring || !g_frame_options || g_frame_owner.Phase() != FrameOwnerPhase::Idle
        || Get_Attachment_Bindings().Offscreen()) return false;
    ConfigurationScope scope;
    Release_Device_Resources();
    if (!Create_Frame_Device(*g_frame_options,g_exclusive_fullscreen)) return false;
    Restore_Device_Resources();
    return true;
}

export bool Resize_Frame_Device(std::uint32_t width, std::uint32_t height, bool exclusive_fullscreen)
{
    if (g_reconfiguring || !g_frame_options || g_frame_owner.Phase() != FrameOwnerPhase::Idle
        || Get_Attachment_Bindings().Offscreen() || width == 0 || height == 0) return false;
    g_frame_options->width = width;
    g_frame_options->height = height;
    g_exclusive_fullscreen = exclusive_fullscreen;
    if (!g_frame_device || g_frame_device->Get_Status() != RHIDeviceStatus::Ready)
        return Recreate_Frame_Device();
    ConfigurationScope scope;
    if (!g_frame_device->Set_Exclusive_Fullscreen(false)) return false;
    Get_Attachment_Bindings().Reset();
    const bool resized = g_frame_device->Get_Swap_Chain().Resize(width,height);
    // Retained external attachments can reject resize. Rebind the surviving
    // targets so the current frame size remains drawable until they are freed.
    const bool bound = Bind_Frame_Attachments();
    const bool fullscreen = !exclusive_fullscreen || g_frame_device->Set_Exclusive_Fullscreen(true);
    return resized && bound && fullscreen;
}

export bool Recover_Frame_Device()
{
    return g_frame_options && Resize_Frame_Device(g_frame_options->width,
        g_frame_options->height,g_exclusive_fullscreen);
}

export bool Set_Frame_Exclusive_Fullscreen(bool fullscreen) noexcept
{
    if (g_reconfiguring || !g_frame_device || g_frame_owner.Phase() != FrameOwnerPhase::Idle
        || !g_frame_device->Set_Exclusive_Fullscreen(fullscreen)) return false;
    g_exclusive_fullscreen = fullscreen;
    return true;
}

export void Detach_Frame_Draw_Executor() noexcept
{
    if (g_frame_device != nullptr) g_frame_owner.Abort(*g_frame_device);
    g_frame_owner.Set_Draw_Executor(nullptr);
}

export extern "C" bool Graphics_DX11_Begin_Frame() noexcept
{
    if (g_reconfiguring) return false;
    if (!Frame_Device_Ready() && !Recover_Frame_Device()) return false;
	return g_frame_owner.Begin_Frame(*g_frame_device);
}

export extern "C" bool Graphics_DX11_Execute_Queued_Draws() noexcept
{
	return g_frame_device != nullptr && g_frame_owner.Execute_Queued_Draws(*g_frame_device);
}

export bool Register_Frame_Draw_Executor(FrameRendererInitializer initializer, FrameDrawExecutor executor)
{
	if (g_frame_device == nullptr || g_frame_owner.Phase() != FrameOwnerPhase::Idle)
		return false;
	if (initializer != nullptr && !initializer(*g_frame_device))
		return false;

	return g_frame_owner.Set_Draw_Executor(executor);
}

export extern "C" bool Graphics_DX11_End_Frame() noexcept
{
	return g_frame_device != nullptr && g_frame_owner.End_Frame(*g_frame_device);
}

export extern "C" bool Graphics_DX11_Present() noexcept
{
	return g_frame_device != nullptr && g_frame_owner.Present(*g_frame_device);
}

export extern "C" void Graphics_DX11_Abort_Frame() noexcept
{
	if (g_frame_device != nullptr)
		g_frame_owner.Abort(*g_frame_device);
}

export extern "C" std::uint32_t Graphics_DX11_Frame_Invalid_Operation_Count() noexcept
{
	return g_frame_owner.Invalid_Operation_Count();
}

export extern "C" void Graphics_DX11_Shutdown_Shared_Frame() noexcept
{
    if (g_reconfiguring) return;
    ConfigurationScope scope;
    Release_Device_Resources();
    g_frame_owner.Set_Draw_Executor(nullptr);
    g_frame_options.reset();
    g_restore_resources = false;
    g_exclusive_fullscreen = false;
}

}
