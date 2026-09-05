/* DX11 runtime lifetime and factory. */
#include "Backend/dx11/core/DX11BackendRuntime.h"
#include "Backend/dx11/core/DX11BackendInternals.h"

DX11BackendRuntime::DX11BackendRuntime() : state(new dx11_backend::DX11BackendState())
{
}

DX11BackendRuntime::~DX11BackendRuntime()
{
	Shutdown();
}

DX11BackendRuntime *DX11BackendRuntime::Create(void *window, bool lite)
{
	DX11BackendRuntime *backend = new DX11BackendRuntime();
	if (!backend->Initialize(window, lite))
	{
		delete backend;
		return nullptr;
	}
	return backend;
}

bool DX11BackendRuntime::Get_Shared_Frame_Resources(DX11SharedFrameResources &resources) const noexcept
{
	resources = {};
	if (state == nullptr || !state->initialized || state->device_status != RenderBackendDeviceStatus::Ready || state->device == nullptr
		|| state->context == nullptr || state->swap_chain == nullptr || state->back_buffer == nullptr || state->back_buffer_view == nullptr
		|| state->depth_buffer == nullptr || state->depth_buffer_view == nullptr || state->width == 0 || state->height == 0)
		return false;

	resources.device = state->device;
	resources.context = state->context;
	resources.swap_chain = state->swap_chain;
	resources.back_buffer = state->back_buffer;
	resources.back_buffer_view = state->back_buffer_view;
	resources.depth_buffer = state->depth_buffer;
	resources.depth_buffer_view = state->depth_buffer_view;
	resources.width = state->width;
	resources.height = state->height;
	return true;
}

bool DX11BackendRuntime::Get_Shared_Texture_Resources(RenderBackendTextureHandle texture, void *&resource, void *&view) const noexcept
{
    const dx11_backend::DX11Texture *native = dx11_backend::As_DX11_Texture(texture);
    resource = native != nullptr ? native->resource : nullptr;
    view = native != nullptr ? native->shader_resource_view : nullptr;
    return resource != nullptr && view != nullptr;
}
