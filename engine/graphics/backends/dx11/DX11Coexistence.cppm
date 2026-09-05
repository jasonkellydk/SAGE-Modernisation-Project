module;

#include <cstdint>
#include <memory>
#include <utility>

export module Graphics.Backends.DX11.Coexistence;

export import Graphics.Backends.DX11;
export import Graphics.FrameOwner;

namespace Graphics
{

namespace
{
	std::unique_ptr<DX11Device> g_shared_frame_device;
	FrameOwner g_frame_owner;

	DX11SharedFrameResources Make_Shared_Frame_Resources(
		void *device,
		void *context,
		void *swap_chain,
		void *back_buffer,
		void *back_buffer_view,
		void *depth_buffer,
		void *depth_buffer_view,
		std::uint32_t width,
		std::uint32_t height) noexcept
	{
		return {device, context, swap_chain, back_buffer, back_buffer_view, depth_buffer, depth_buffer_view, width, height};
	}
}

// The integration owner must release imported handles and renderer resources
// before shutting down the shared device.
export Device *Shared_Frame_Device() noexcept
{
    return g_shared_frame_device.get();
}

export RHITextureHandle Import_Shared_Texture(void *texture, void *shader_resource_view)
{
    return g_shared_frame_device != nullptr
        ? g_shared_frame_device->Import_Texture(texture, shader_resource_view) : RHITextureHandle{};
}

export extern "C" bool Graphics_DX11_Initialize_Shared_Frame(
	void *device,
	void *context,
	void *swap_chain,
	void *back_buffer,
	void *back_buffer_view,
	void *depth_buffer,
	void *depth_buffer_view,
	std::uint32_t width,
	std::uint32_t height)
{
	if (device == nullptr || context == nullptr || swap_chain == nullptr || back_buffer == nullptr || back_buffer_view == nullptr
		|| depth_buffer == nullptr || depth_buffer_view == nullptr || width == 0 || height == 0 || g_frame_owner.Phase() != FrameOwnerPhase::Idle)
		return false;

	const DX11SharedFrameResources resources = Make_Shared_Frame_Resources(device, context, swap_chain, back_buffer, back_buffer_view, depth_buffer, depth_buffer_view, width, height);
	if (g_shared_frame_device != nullptr)
		return g_shared_frame_device->Adopt_Shared_Frame(resources);

	DX11DeviceOptions options;
	options.shared_frame = &resources;
	std::unique_ptr<DX11Device> device_instance = std::make_unique<DX11Device>(options);
	if (!device_instance->Is_Valid() || !device_instance->Get_Swap_Chain().Is_Valid())
		return false;

	g_shared_frame_device = std::move(device_instance);
	const RHIBackbuffer backbuffer = g_shared_frame_device->Get_Swap_Chain().Backbuffer();
	const RHIDepthTarget depth = g_shared_frame_device->Get_Swap_Chain().Depth_Target();
	return backbuffer.texture.Is_Valid() && depth.texture.Is_Valid();
}

export extern "C" bool Graphics_DX11_Update_Shared_Frame(
	void *device,
	void *context,
	void *swap_chain,
	void *back_buffer,
	void *back_buffer_view,
	void *depth_buffer,
	void *depth_buffer_view,
	std::uint32_t width,
	std::uint32_t height)
{
	if (g_shared_frame_device == nullptr || g_frame_owner.Phase() != FrameOwnerPhase::Idle)
		return false;

	return g_shared_frame_device->Adopt_Shared_Frame(Make_Shared_Frame_Resources(device, context, swap_chain, back_buffer, back_buffer_view, depth_buffer, depth_buffer_view, width, height));
}

export extern "C" bool Graphics_DX11_Begin_Frame() noexcept
{
	return g_shared_frame_device != nullptr && g_frame_owner.Begin_Frame(*g_shared_frame_device);
}

export extern "C" bool Graphics_DX11_Begin_Graphics_Phase() noexcept
{
	return g_shared_frame_device != nullptr && g_frame_owner.Begin_Graphics_Phase(*g_shared_frame_device);
}

export bool Register_Graphics_Phase_Executor(GraphicsPhaseInitializer initializer, GraphicsPhaseExecutor executor)
{
	if (g_shared_frame_device == nullptr || g_frame_owner.Phase() != FrameOwnerPhase::Idle)
		return false;
	if (initializer != nullptr && !initializer(*g_shared_frame_device))
		return false;

	return g_frame_owner.Set_Graphics_Phase_Executor(executor);
}

export extern "C" bool Graphics_DX11_End_Frame() noexcept
{
	return g_shared_frame_device != nullptr && g_frame_owner.End_Frame(*g_shared_frame_device);
}

export extern "C" bool Graphics_DX11_Present() noexcept
{
	return g_shared_frame_device != nullptr && g_frame_owner.Present(*g_shared_frame_device);
}

export extern "C" void Graphics_DX11_Abort_Frame() noexcept
{
	if (g_shared_frame_device != nullptr)
		g_frame_owner.Abort(*g_shared_frame_device);
}

export extern "C" std::uint32_t Graphics_DX11_Frame_Invalid_Operation_Count() noexcept
{
	return g_frame_owner.Invalid_Operation_Count();
}

export extern "C" void Graphics_DX11_Shutdown_Shared_Frame() noexcept
{
	if (g_shared_frame_device != nullptr)
		g_frame_owner.Abort(*g_shared_frame_device);

	g_frame_owner.Set_Graphics_Phase_Executor(nullptr);
	g_shared_frame_device.reset();
}

}
