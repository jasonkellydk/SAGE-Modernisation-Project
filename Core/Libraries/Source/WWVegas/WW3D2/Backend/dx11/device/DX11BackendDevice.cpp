/* DX11 device subsystem. */
#include "Backend/dx11/device/DX11DeviceBackend.h"
#include "Backend/dx11/core/DX11BackendInternals.h"
#include "Backend/dx11/core/DX11BackendRuntime.h"

namespace dx11_backend
{

bool DX11BackendState::Create_Device()
{
	if (device != nullptr)
	{
		return true;
	}
	if (window == nullptr)
	{
		return false;
	}

	DXGI_SWAP_CHAIN_DESC swap_chain_description = {};
	swap_chain_description.BufferCount = 2;
	swap_chain_description.BufferDesc.Width = width;
	swap_chain_description.BufferDesc.Height = height;
	swap_chain_description.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swap_chain_description.BufferDesc.RefreshRate.Numerator = 60;
	swap_chain_description.BufferDesc.RefreshRate.Denominator = 1;
	swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_description.OutputWindow = window;
	swap_chain_description.SampleDesc.Count = 1;
	// Borderless mode is still a windowed DXGI swap chain; SDL owns the
	// border/style transition for the game window. Only the explicit mode is
	// allowed to create an exclusive swap chain.
	swap_chain_description.Windowed = (windowed ||
		fullscreen_mode == RenderBackendFullscreenMode::Borderless) ? TRUE : FALSE;
	swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const D3D_FEATURE_LEVEL feature_levels[] = {
		D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
	const HRESULT result = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		feature_levels, 3, D3D11_SDK_VERSION, &swap_chain_description,
		&swap_chain, &device, &feature_level, &context);
	if (FAILED(result))
	{
		device_status = RenderBackendDeviceStatus::Error;
		return false;
	}

	if (!Create_Render_Targets() || !Create_Constant_Buffers() ||
		!Recreate_Compiled_Shaders()
    )
	{
		Release_Device();
		device_status = RenderBackendDeviceStatus::Error;
		return false;
	}

	(void)feature_level;
	device_status = RenderBackendDeviceStatus::Ready;
	return true;
}

void DX11BackendState::Release_Render_Targets()
{
	if (context != nullptr)
	{
		context->OMSetRenderTargets(0, nullptr, nullptr);
	}
	if (scene_depth_texture != nullptr)
	{
		Release_Com(scene_depth_texture->shader_resource_view);
		Release_Com(scene_depth_texture->resource);
	}
	Release_Com(depth_buffer_view);
	Release_Com(depth_buffer);
	Release_Com(back_buffer_view);
	Release_Com(back_buffer);
	active_render_target_view = nullptr;
	active_depth_stencil_view = nullptr;
	render_to_texture = false;
	active_render_target = nullptr;
	active_depth_target = nullptr;
}

bool DX11BackendState::Create_Render_Targets()
{
	if (device == nullptr || swap_chain == nullptr)
	{
		return false;
	}

	Release_Render_Targets();
	HRESULT result = swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
		reinterpret_cast<void **>(&back_buffer));
	if (FAILED(result) || back_buffer == nullptr)
	{
		return false;
	}
	result = device->CreateRenderTargetView(back_buffer, nullptr, &back_buffer_view);
	if (FAILED(result) || back_buffer_view == nullptr)
	{
		Release_Render_Targets();
		return false;
	}

	D3D11_TEXTURE2D_DESC depth_description = {};
	depth_description.Width = width;
	depth_description.Height = height;
	depth_description.MipLevels = 1;
	depth_description.ArraySize = 1;
	// Keep the depth storage typeless so the same scene depth can be exposed
	// to programmable materials through an R24_UNORM shader-resource view.
	depth_description.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depth_description.SampleDesc.Count = 1;
	depth_description.Usage = D3D11_USAGE_DEFAULT;
	depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	result = device->CreateTexture2D(&depth_description, nullptr, &depth_buffer);
	if (FAILED(result) || depth_buffer == nullptr)
	{
		Release_Render_Targets();
		return false;
	}
	D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_description = {};
	depth_view_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_view_description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depth_view_description.Texture2D.MipSlice = 0;
	result = device->CreateDepthStencilView(depth_buffer,
		&depth_view_description, &depth_buffer_view);
	if (FAILED(result) || depth_buffer_view == nullptr)
	{
		Release_Render_Targets();
		return false;
	}

	if (scene_depth_texture == nullptr)
	{
		scene_depth_texture = new DX11Texture();
	}
	scene_depth_texture->width = width;
	scene_depth_texture->height = height;
	scene_depth_texture->depth = 1;
	scene_depth_texture->levels = 1;
	scene_depth_texture->format = WW3D_FORMAT_A8R8G8B8;
	scene_depth_texture->depth_format = WW3D_ZFORMAT_UNKNOWN;
	scene_depth_texture->kind = RenderBackendTextureKind::Texture2D;
	scene_depth_texture->native_format = DXGI_FORMAT_R24G8_TYPELESS;
	scene_depth_texture->render_target = false;
	D3D11_TEXTURE2D_DESC scene_depth_description = {};
	scene_depth_description.Width = width;
	scene_depth_description.Height = height;
	scene_depth_description.MipLevels = 1;
	scene_depth_description.ArraySize = 1;
	scene_depth_description.Format = DXGI_FORMAT_R24G8_TYPELESS;
	scene_depth_description.SampleDesc.Count = 1;
	scene_depth_description.Usage = D3D11_USAGE_DEFAULT;
	scene_depth_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	result = device->CreateTexture2D(&scene_depth_description, nullptr,
		&scene_depth_texture->resource);
	if (FAILED(result) || scene_depth_texture->resource == nullptr)
	{
		Release_Render_Targets();
		return false;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC scene_depth_view_description = {};
	scene_depth_view_description.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	scene_depth_view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	scene_depth_view_description.Texture2D.MostDetailedMip = 0;
	scene_depth_view_description.Texture2D.MipLevels = 1;
	result = device->CreateShaderResourceView(scene_depth_texture->resource,
		&scene_depth_view_description, &scene_depth_texture->shader_resource_view);
	if (FAILED(result) || scene_depth_texture->shader_resource_view == nullptr)
	{
		Release_Render_Targets();
		return false;
	}

	active_render_target_view = back_buffer_view;
	active_depth_stencil_view = depth_buffer_view;
	render_to_texture = false;
	active_render_target = nullptr;
	active_depth_target = nullptr;
	viewport = {0, 0, width, height, 0.0f, 1.0f};
	return true;
}

bool DX11BackendState::Create_Constant_Buffers()
{
	return Create_Constant_Buffer(device, sizeof(DX11VertexConstantData),
		&vertex_constant_buffer) &&
		Create_Constant_Buffer(device, sizeof(DX11PixelConstantData), &pixel_constant_buffer);
}

void DX11BackendState::Release_Pipeline_States()
{
	Release_Active_Input_Layout();
	for (ID3D11InputLayout *&layout : default_input_layouts)
	{
		Release_Com(layout);
	}
	Release_Com(blend_state);
	Release_Com(depth_state);
	Release_Com(rasterizer_state);
	for (ID3D11SamplerState *& sampler : samplers)
	{
		Release_Com(sampler);
	}
	cached_blend_description = {};
	cached_depth_description = {};
	cached_raster_description = {};
	for (D3D11_SAMPLER_DESC &description : cached_sampler_descriptions)
	{
		description = {};
	}
	blend_description_valid = false;
	depth_description_valid = false;
	raster_description_valid = false;
	sampler_description_valid.fill(false);
	native_state_valid = false;
	native_state_dirty = true;
	shader_bindings_valid = false;
	constant_buffers_bound = false;
	constant_state_dirty = true;
	uploaded_vertex_constants_valid = false;
	uploaded_pixel_constants_valid = false;
	applied_blend_state = nullptr;
	applied_depth_state = nullptr;
	applied_rasterizer_state = nullptr;
	applied_stencil_reference = 0;
	applied_shader_resources.fill(nullptr);
	applied_samplers.fill(nullptr);
	bound_input_layout = nullptr;
	bound_vertex_shader = nullptr;
	bound_pixel_shader = nullptr;
}

void DX11BackendState::Release_Registered_Textures()
{
	// TextureBaseClass owns the opaque handle. Clearing it through the neutral
	// API releases the D3D11 views as well as the resource and keeps the engine
	// texture object valid for recreation after ResizeBuffers/device loss.
	for (const DX11TextureRegistration &registration : registered_textures)
	{
		if (registration.texture != nullptr)
		{
			registration.texture->Set_Render_Backend_Texture(0);
		}
	}
	textures.fill(nullptr);
	direct_texture_overrides.fill(nullptr);
	direct_texture_override_valid.fill(false);
	texture_kinds.fill(RenderBackendTextureKind::Texture2D);
	Invalidate_Default_Pixel_Shader_Selection();
}

void DX11BackendState::Recreate_Registered_Textures()
{
	for (const DX11TextureRegistration &registration : registered_textures)
	{
		if (registration.texture != nullptr &&
			registration.texture->Peek_Render_Backend_Texture() == 0)
		{
			// Procedural/default-pool textures recreate synchronously. Managed
			// file textures are either recreated by their loader or lazily when
			// first applied, matching the WW3D texture contract.
			registration.texture->Ensure_Render_Backend_Texture();
		}
	}
}

void DX11BackendState::Release_Device()
{
	applied_render_state_valid = false;
	direct_vertex_binding_override = false;
	direct_index_binding_override = false;
	delete immediate_vertex_buffer;
	immediate_vertex_buffer = nullptr;
	immediate_vertex_capacity_bytes = 0;
	Release_Registered_Textures();
	Release_Render_Targets();
	Release_Pipeline_States();
	for (DX11VertexShader *shader : vertex_shaders)
	{
		Release_Com(shader->shader);
	}
	for (DX11PixelShader *shader : pixel_shaders)
	{
		Release_Com(shader->shader);
	}
	active_vertex_shader = nullptr;
	active_pixel_shader = nullptr;
	default_vertex_shaders.fill(nullptr);
	default_pixel_shaders.fill(nullptr);
	Release_Com(vertex_constant_buffer);
	Release_Com(pixel_constant_buffer);
	Release_Com(swap_chain);
	Release_Com(context);
	Release_Com(device);
	device_status = RenderBackendDeviceStatus::Error;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Initialize(void *window, bool lite)
{
	DX11BackendState &impl = this->State();
	if (impl.initialized)
	{
		return true;
	}

	impl.window = reinterpret_cast<HWND>(window);
	impl.lite = lite;
	impl.windowed = true;
	impl.fullscreen_mode = RenderBackendFullscreenMode::Borderless;
	if (impl.window != nullptr)
	{
		RECT client_rect{};
		if (::GetClientRect(impl.window, &client_rect) != FALSE &&
			client_rect.right > client_rect.left && client_rect.bottom > client_rect.top)
		{
			impl.width = static_cast<unsigned>(client_rect.right - client_rect.left);
			impl.height = static_cast<unsigned>(client_rect.bottom - client_rect.top);
		}
	}
	impl.viewport = {0, 0, impl.width, impl.height, 0.0f, 1.0f};
	Render2DClass::Set_Screen_Resolution(RectClass(0, 0,
		static_cast<int>(impl.width), static_cast<int>(impl.height)));
	impl.device_desc.Set_Device_Name("Direct3D 11");
	impl.device_desc.Set_Driver_Name("D3D11");
	impl.device_desc.Set_Driver_Version("Shader Model 5.0 / Slang DXBC");
	impl.device_desc.Reset_Resolution_List();
	impl.device_desc.Add_Resolution(static_cast<int>(impl.width), static_cast<int>(impl.height), static_cast<int>(impl.bits));
	impl.device_status = RenderBackendDeviceStatus::Error;
	impl.initialized = true;
	return lite || impl.window != nullptr;
}

template <typename Host>
void DX11DeviceBackend<Host>::Shutdown()
{
	if (!this->State().initialized)
	{
		return;
	}
	DX11BackendState &impl = this->State();
	if (impl.cleanup_hook != nullptr)
	{
		impl.cleanup_hook->ReleaseResources();
	}
	impl.Release_Device();
	impl.scene_active = false;
	impl.render_to_texture = false;
	impl.device_status = RenderBackendDeviceStatus::Error;
	impl.initialized = false;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Initted() const
{
	return this->State().initialized;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Render_To_Texture() const
{
	return this->State().render_to_texture;
}

template <typename Host>
void DX11DeviceBackend<Host>::Get_Render_Target(RenderBackendRenderTargetState & target) const
{
	target.color = this->State().active_render_target;
	target.depth = this->State().active_depth_target;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Has_Stencil() const
{
	return this->State().depth_buffer_view != nullptr;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_TnL() const
{
	return false;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_DXTC() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_NPatches() const
{
	return false;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Bump_Envmap() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Bump_Envmap_Luminance() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Z_Bias() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Anisotropic_Filtering() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Modulate_Alpha_Add_Color() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Dot3() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Point_Sprites() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Cubemaps() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Color_Write_Mask() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Texture_Operation(RenderBackendTextureOperation operation) const
{
	// The Slang fixed-function compatibility shader implements the complete
	// WW3D texture-operation set, including the two bump-environment modes.
	(void)operation;
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Texture_Filter(RenderBackendTextureFilterType type, RenderBackendTextureFilter filter) const
{
	(void)type;
	return filter != RenderBackendTextureFilter::None || type == RenderBackendTextureFilterType::MipMap;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Fog_Allowed() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Fog_Enabled() const
{
	return this->State().fog_enabled;
}

template <typename Host>
unsigned DX11DeviceBackend<Host>::Get_Fog_Color() const
{
	return this->Pack_Color(Vector4(this->State().fog_color[0],
		this->State().fog_color[1], this->State().fog_color[2], this->State().fog_color[3]));
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Texture_Format(WW3DFormat format) const
{
	return Is_Natively_Supported_Format(format);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Render_To_Texture_Format(WW3DFormat format) const
{
	return format == WW3D_FORMAT_UNKNOWN || Is_Natively_Supported_Format(format);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Supports_Depth_Stencil_Format(WW3DZFormat format) const
{
	return format == WW3D_ZFORMAT_D16 || format == WW3D_ZFORMAT_D16_LOCKABLE ||
		format == WW3D_ZFORMAT_D24S8 || format == WW3D_ZFORMAT_D32;
}

template <typename Host>
WW3DFormat DX11DeviceBackend<Host>::Get_Back_Buffer_Format() const
{
	return WW3D_FORMAT_A8R8G8B8;
}

template <typename Host>
SurfaceClass * DX11DeviceBackend<Host>::Get_Back_Buffer_Surface()
{
	DX11BackendState &impl = this->State();
	if (impl.device == nullptr || impl.back_buffer == nullptr)
	{
		return nullptr;
	}
	D3D11_TEXTURE2D_DESC description = {};
	impl.back_buffer->GetDesc(&description);
	D3D11_TEXTURE2D_DESC staging_description = description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	staging_description.MiscFlags = 0;
	ID3D11Texture2D *staging = nullptr;
	if (FAILED(impl.device->CreateTexture2D(&staging_description, nullptr, &staging)))
	{
		return nullptr;
	}
	impl.context->CopyResource(staging, impl.back_buffer);
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(impl.context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
	{
		Release_Com(staging);
		return nullptr;
	}
	DX11Surface *surface = new DX11Surface(description.Width, description.Height,
		WW3D_FORMAT_A8R8G8B8);
	for (unsigned y = 0; y < description.Height; ++y)
	{
		std::memcpy(surface->pixels.data() + y * surface->pitch,
			static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch,
			std::min<unsigned>(surface->pitch, mapped.RowPitch));
	}
	impl.context->Unmap(staging, 0);
	Release_Com(staging);
	return new SurfaceClass(surface);
}

template <typename Host>
RenderBackendDeviceStatus DX11DeviceBackend<Host>::Get_Device_Status() const
{
	return this->State().device_status;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Device_Ready() const
{
	return this->State().device_status == RenderBackendDeviceStatus::Ready;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Render_Thread() const
{
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Get_Adapter_Info(RenderBackendAdapterInfo & info) const
{
	info = RenderBackendAdapterInfo();
	if (this->State().device == nullptr)
	{
		return false;
	}
	IDXGIDevice *dxgi_device = nullptr;
	IDXGIAdapter *adapter = nullptr;
	DXGI_ADAPTER_DESC adapter_description = {};
	bool success = SUCCEEDED(this->State().device->QueryInterface(
		__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_device))) &&
		SUCCEEDED(dxgi_device->GetAdapter(&adapter)) &&
		SUCCEEDED(adapter->GetDesc(&adapter_description));
	if (success)
	{
		info.vendor_id = adapter_description.VendorId;
		info.device_id = adapter_description.DeviceId;
	}
	Release_Com(adapter);
	Release_Com(dxgi_device);
	return success;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Get_Texture_Limits(RenderBackendTextureLimits & limits) const
{
	limits = RenderBackendTextureLimits();
	limits.max_width = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	limits.max_height = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	limits.max_volume_extent = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
	limits.max_aspect_ratio = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	return true;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Max_Textures_Per_Pass() const
{
	return MAX_TEXTURE_STAGES;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Pixel_Shader_Major_Version() const
{
	return 5;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Pixel_Shader_Minor_Version() const
{
	return 0;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_3DFX_Voodoo3() const
{
	return false;
}

template <typename Host>
unsigned DX11DeviceBackend<Host>::Pack_Color(const Vector4 & color) const
{
	const unsigned r = static_cast<unsigned>(Clamp_Float(color[0], 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned g = static_cast<unsigned>(Clamp_Float(color[1], 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned b = static_cast<unsigned>(Clamp_Float(color[2], 0.0f, 1.0f) * 255.0f + 0.5f);
	const unsigned a = static_cast<unsigned>(Clamp_Float(color[3], 0.0f, 1.0f) * 255.0f + 0.5f);
	return (a << 24) | (r << 16) | (g << 8) | b;
}

template <typename Host>
unsigned DX11DeviceBackend<Host>::Pack_Color(const Vector3 & color, float alpha) const
{
	return Pack_Color(Vector4(color[0], color[1], color[2], alpha));
}

template <typename Host>
unsigned DX11DeviceBackend<Host>::Pack_Color_Clamped(const Vector4 & color) const
{
	return Pack_Color(color);
}

template <typename Host>
Vector4 DX11DeviceBackend<Host>::Unpack_Color(unsigned color) const
{
	return Color_From_Packed(color);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Triangle_Draw_Enabled() const
{
	return this->State().triangle_draw_enabled;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Triangle_Draw_Enabled(bool enable)
{
	this->State().triangle_draw_enabled = enable;
}

template <typename Host>
RenderBackendDebugSettings & DX11DeviceBackend<Host>::Get_Debug_Settings()
{
	return this->State().debug_settings;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Cleanup_Hook(RenderBackendCleanupHook * hook)
{
	this->State().cleanup_hook = hook;
}

template <typename Host>
void DX11DeviceBackend<Host>::Invalidate_Renderer_Caches()
{
	this->Backend().Invalidate_Cached_Render_States();
	ShaderClass::Invalidate();
}

template <typename Host>
RenderBackendFont * DX11DeviceBackend<Host>::Create_Font(int height, const char * face_name, bool bold, int width)
{
	if (this->State().device == nullptr || face_name == nullptr)
	{
		return nullptr;
	}
	DX11Font *font = new DX11Font();
	font->height = height;
	font->width = width > 0 ? width : height;
	font->bold = bold;
	font->face = face_name;
	if (!font->Initialize_Glyph_Font(height, face_name, bold, width))
	{
		delete font;
		return nullptr;
	}
	return font;
}

template <typename Host>
void DX11DeviceBackend<Host>::Release_Font(RenderBackendFont * font)
{
	delete static_cast<DX11Font *>(font);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Get_Font_Metrics(RenderBackendFont * font, RenderBackendFontMetrics & metrics) const
{
	const DX11Font *dx11_font = static_cast<const DX11Font *>(font);
	if (dx11_font == nullptr || dx11_font->glyph_height <= 0)
	{
		return false;
	}
	metrics.height = dx11_font->glyph_height;
	metrics.ascent = dx11_font->glyph_ascent;
	metrics.overhang = dx11_font->glyph_overhang;
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Get_Font_Glyph(RenderBackendFont * font, unsigned int character, RenderBackendFontGlyph & glyph)
{
	DX11Font *dx11_font = static_cast<DX11Font *>(font);
	if (dx11_font == nullptr || dx11_font->glyph_dc == nullptr ||
		dx11_font->glyph_bitmap_bits == nullptr || dx11_font->glyph_height <= 0)
	{
		return false;
	}

	RECT glyph_rect = {0, 0, dx11_font->glyph_bitmap_width, dx11_font->glyph_bitmap_height};
	std::memset(dx11_font->glyph_bitmap_bits, 0,
		static_cast<std::size_t>(dx11_font->glyph_bitmap_pitch) *
		dx11_font->glyph_bitmap_height);

	const WCHAR wide_character = static_cast<WCHAR>(character);
	if (!ExtTextOutW(dx11_font->glyph_dc, 0, 0, ETO_OPAQUE, &glyph_rect,
		&wide_character, 1, nullptr))
	{
		return false;
	}

	SIZE character_size = {};
	if (!GetTextExtentPoint32W(dx11_font->glyph_dc, &wide_character, 1, &character_size))
	{
		return false;
	}

	const unsigned glyph_width = static_cast<unsigned>(std::max(0, std::min(
		static_cast<int>(character_size.cx), dx11_font->glyph_bitmap_width)));
	const unsigned glyph_height = static_cast<unsigned>(std::min(
		dx11_font->glyph_height, dx11_font->glyph_bitmap_height));
	if (glyph_width == 0 || glyph_height == 0)
	{
		return false;
	}

	dx11_font->glyph_pixels.assign(static_cast<std::size_t>(glyph_width) * glyph_height, 0);
	for (unsigned row = 0; row < glyph_height; ++row)
	{
		const unsigned char *source = dx11_font->glyph_bitmap_bits +
			static_cast<std::size_t>(row) * dx11_font->glyph_bitmap_pitch;
		unsigned char *destination = dx11_font->glyph_pixels.data() +
			static_cast<std::size_t>(row) * glyph_width;
		for (unsigned column = 0; column < glyph_width; ++column)
		{
			destination[column] = source[column * 3];
		}
	}

	glyph.width = glyph_width;
	glyph.height = glyph_height;
	glyph.pitch = glyph_width;
	glyph.pixels = dx11_font->glyph_pixels.data();
	return true;
}

template <typename Host>
void DX11DeviceBackend<Host>::Draw_Font(RenderBackendFont * font, const char * text, unsigned text_length, const RenderBackendRect & rect, unsigned flags, unsigned color)
{
	
}

template <typename Host>
bool DX11DeviceBackend<Host>::Initialize_Browser(const char * bad_page_url, const char * loading_page_url, const char * mouse_filename, const char * mouse_busy_filename)
{
	return false;
}

template <typename Host>
void DX11DeviceBackend<Host>::Shutdown_Browser()
{
	
}

template <typename Host>
void DX11DeviceBackend<Host>::Update_Browser()
{
	
}

template <typename Host>
void DX11DeviceBackend<Host>::Render_Browser(int backbuffer_index)
{
	
}

template <typename Host>
void DX11DeviceBackend<Host>::Create_Browser(const char * browser_name, const char * url, int x, int y, int width, int height, int update_ticks, unsigned options, void * game_dispatch)
{
	
}

template <typename Host>
void DX11DeviceBackend<Host>::Destroy_Browser(const char * browser_name)
{
	
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Browser_Open(const char * browser_name) const
{
	return false;
}

template <typename Host>
void DX11DeviceBackend<Host>::Navigate_Browser(const char * browser_name, const char * url)
{
	
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Render_Device(const char * device_name, int width, int height, int bits, int windowed, bool resize_window)
{
	(void)device_name;
	return Set_Render_Device(0, width, height, bits, windowed, resize_window, false, true);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Render_Device(int device, int width, int height, int bits, int windowed, bool resize_window, bool reset_device, bool restore_assets)
{
	(void)device;
	DX11BackendState &impl = this->State();
	if (!impl.initialized)
	{
		return false;
	}
	const bool had_device = impl.device != nullptr;
	const unsigned old_width = impl.width;
	const unsigned old_height = impl.height;
	const unsigned old_bits = impl.bits;
	const bool old_windowed = impl.windowed;
	if (width > 0) impl.width = static_cast<unsigned>(width);
	if (height > 0) impl.height = static_cast<unsigned>(height);
	if (bits > 0) impl.bits = static_cast<unsigned>(bits);
	if (windowed >= 0) impl.windowed = windowed != 0;
	Render2DClass::Set_Screen_Resolution(RectClass(0, 0,
		static_cast<int>(impl.width), static_cast<int>(impl.height)));
	const bool resolution_changed = old_width != impl.width || old_height != impl.height ||
		old_bits != impl.bits || old_windowed != impl.windowed;
	if (resize_window && impl.window != nullptr && impl.windowed && resolution_changed)
	{
		RECT client_rect = {0, 0, static_cast<LONG>(impl.width), static_cast<LONG>(impl.height)};
		const LONG_PTR style = GetWindowLongPtr(impl.window, GWL_STYLE);
		const LONG_PTR extended_style = GetWindowLongPtr(impl.window, GWL_EXSTYLE);
		if (AdjustWindowRectEx(&client_rect, static_cast<DWORD>(style), FALSE,
			static_cast<DWORD>(extended_style)) != FALSE)
		{
			SetWindowPos(impl.window, nullptr, 0, 0,
				client_rect.right - client_rect.left,
				client_rect.bottom - client_rect.top,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
	impl.device_desc.Reset_Resolution_List();
	impl.device_desc.Add_Resolution(static_cast<int>(impl.width), static_cast<int>(impl.height), static_cast<int>(impl.bits));
	if (!had_device)
	{
		if (!impl.Create_Device())
		{
			return false;
		}
		impl.device_status = RenderBackendDeviceStatus::Ready;
		return true;
	}
	if (!reset_device && !resolution_changed)
	{
		return impl.device_status == RenderBackendDeviceStatus::Ready;
	}

	return Reset_Device(restore_assets);
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Fullscreen_Mode(RenderBackendFullscreenMode mode)
{
	if (this->State().fullscreen_mode == mode)
	{
		return;
	}
	this->State().fullscreen_mode = mode;
	if (this->State().device != nullptr && this->State().device_status == RenderBackendDeviceStatus::Ready)
	{
		Reset_Device(false);
	}
}

template <typename Host>
RenderBackendFullscreenMode DX11DeviceBackend<Host>::Get_Fullscreen_Mode() const
{
	return this->State().fullscreen_mode;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Any_Render_Device()
{
	return Set_Render_Device(0, static_cast<int>(this->State().width),
		static_cast<int>(this->State().height), static_cast<int>(this->State().bits),
		this->State().windowed ? 1 : 0, false, false, true);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Next_Render_Device()
{
	return false;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Is_Windowed() const
{
	return this->State().windowed;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Toggle_Windowed()
{
	this->State().windowed = !this->State().windowed;
	return Reset_Device(false);
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Render_Device() const
{
	return 0;
}

template <typename Host>
const RenderDeviceDescClass & DX11DeviceBackend<Host>::Get_Render_Device_Desc(int device) const
{
	(void)device;
	return this->State().device_desc;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Render_Device_Count() const
{
	return 1;
}

template <typename Host>
const char * DX11DeviceBackend<Host>::Get_Render_Device_Name(int device) const
{
	(void)device;
	return this->State().device_desc.Get_Device_Name();
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Device_Resolution(int width, int height, int bits, int windowed, bool resize_window)
{
	return Set_Render_Device(0, width, height, bits, windowed, resize_window, true, true);
}

template <typename Host>
void DX11DeviceBackend<Host>::Get_Device_Resolution(int & width, int & height, int & bits, bool & windowed) const
{
	width = static_cast<int>(this->State().width);
	height = static_cast<int>(this->State().height);
	bits = static_cast<int>(this->State().bits);
	windowed = this->State().windowed;
}

template <typename Host>
void DX11DeviceBackend<Host>::Get_Render_Target_Resolution(int & width, int & height, int & bits, bool & windowed) const
{
	Get_Device_Resolution(width, height, bits, windowed);
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Device_Resolution_Width() const
{
	return static_cast<int>(this->State().width);
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Device_Resolution_Height() const
{
	return static_cast<int>(this->State().height);
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Swap_Interval(int swap)
{
	(void)swap;
	// GeneralsMD is intentionally running without presentation throttling.
	// Keep this forced at zero even if a legacy engine setting requests a
	// positive interval.
	this->State().swap_interval = 0;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Swap_Interval() const
{
	return this->State().swap_interval;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Reset_Device(bool reload_assets)
{
	DX11BackendState &impl = this->State();
	if (impl.device == nullptr || impl.swap_chain == nullptr)
	{
		if (!impl.Create_Device())
		{
			return false;
		}
		return true;
	}

	const HRESULT device_reason = impl.device->GetDeviceRemovedReason();
	bool device_removed = FAILED(device_reason);
	const bool exclusive_fullscreen = !impl.windowed &&
		impl.fullscreen_mode == RenderBackendFullscreenMode::Exclusive;
	if (!device_removed)
	{
		// ResizeBuffers invalidates only the swap-chain views.  Unlike the DX9
		// reset contract, a DX11 resize does not invalidate scene resources, so
		// leave terrain atlases, vertex buffers, and explicit shader programs
		// resident.  Releasing them here creates a resource-lifetime window in
		// which the terrain can render once and then fall back after the reset.
		impl.context->ClearState();
		impl.Release_Render_Targets();
		if (exclusive_fullscreen)
		{
			// DXGI requires an exclusive swap chain to leave fullscreen mode before
			// its buffers can be resized.
			impl.swap_chain->SetFullscreenState(FALSE, nullptr);
		}

		const HRESULT result = impl.swap_chain->ResizeBuffers(
			2, impl.width, impl.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
		bool reset_succeeded = SUCCEEDED(result) && impl.Create_Render_Targets();
		if (!reset_succeeded)
		{
			const HRESULT resize_reason = impl.device->GetDeviceRemovedReason();
			device_removed = FAILED(resize_reason);
			if (!device_removed)
			{
				impl.device_status = RenderBackendDeviceStatus::NeedsReset;
				return false;
			}
		}
		else
		{
			if (exclusive_fullscreen &&
				FAILED(impl.swap_chain->SetFullscreenState(TRUE, nullptr)))
			{
				impl.device_status = RenderBackendDeviceStatus::NeedsReset;
				return false;
			}

			// ClearState also unbinds the still-valid scene resources. Force the
			// neutral state replay to bind them again, but keep their ownership and
			// contents intact across a normal swap-chain resize.
			impl.applied_render_state_valid = false;
			impl.native_state_valid = false;
			impl.native_state_dirty = true;
			impl.shader_bindings_valid = false;
			impl.constant_buffers_bound = false;
			impl.constant_state_dirty = true;
			impl.device_status = RenderBackendDeviceStatus::Ready;
			(void)reload_assets;
			this->Backend().Apply_Render_State_Changes();
			return true;
		}
	}

	// A removed device cannot be repaired with ResizeBuffers. Tear down the
	// native device and rebuild it from the precompiled shader bytecode. This is
	// the only path that uses the legacy cleanup hook and releases registered
	// scene resources; normal DX11 resizing above never touches them.
	if (impl.cleanup_hook != nullptr)
	{
		impl.cleanup_hook->ReleaseResources();
	}
	impl.Release_Device();
	const bool reset_succeeded = impl.Create_Device();

	if (!reset_succeeded)
	{
		impl.device_status = RenderBackendDeviceStatus::NeedsReset;
		return false;
	}
	if (exclusive_fullscreen &&
		FAILED(impl.swap_chain->SetFullscreenState(TRUE, nullptr)))
	{
		impl.device_status = RenderBackendDeviceStatus::NeedsReset;
		return false;
	}
	impl.Release_Active_Input_Layout();
	impl.active_explicit_layout = nullptr;
	impl.has_explicit_layout = false;
	impl.index_offset = 0;
	impl.base_vertex_offset = 0;
	impl.render_to_texture = false;
	impl.vertex_constants.fill(Vector4());
	impl.pixel_constants.fill(Vector4());
	impl.device_status = RenderBackendDeviceStatus::Ready;
	// A true device-loss recovery releases default-pool resources above and must
	// reacquire them before the next draw. The normal resize path returned above
	// without touching those resources.
	(void)reload_assets;
	if (impl.cleanup_hook != nullptr)
	{
		impl.cleanup_hook->ReAcquireResources();
	}
	impl.Recreate_Registered_Textures();
	this->Backend().Apply_Render_State_Changes();
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Registry_Save_Render_Device(const char * sub_key)
{
	if (sub_key == nullptr || sub_key[0] == '\0')
	{
		return false;
	}
	int width = 0;
	int height = 0;
	int depth = 0;
	bool windowed = true;
	Get_Device_Resolution(width, height, depth, windowed);
	return Registry_Save_Render_Device(sub_key, Get_Render_Device(), width, height,
		depth, windowed, Get_Texture_Bitdepth());
}

template <typename Host>
bool DX11DeviceBackend<Host>::Registry_Save_Render_Device(const char * sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth)
{
	if (sub_key == nullptr || sub_key[0] == '\0' || device != 0)
	{
		return false;
	}
	RegistryClass registry(sub_key);
	if (!registry.Is_Valid())
	{
		return false;
	}
	registry.Set_String(kRegistryDeviceName, Get_Render_Device_Name(device));
	registry.Set_Int(kRegistryWidth, width);
	registry.Set_Int(kRegistryHeight, height);
	registry.Set_Int(kRegistryDepth, depth);
	registry.Set_Int(kRegistryWindowed, windowed ? 1 : 0);
	registry.Set_Int(kRegistryTextureDepth, texture_depth);
	return true;
}

template <typename Host>
bool DX11DeviceBackend<Host>::Registry_Load_Render_Device(const char * sub_key, bool resize_window)
{
	char device_name[200] = {};
	int width = -1;
	int height = -1;
	int depth = -1;
	int windowed = -1;
	int texture_depth = -1;
	if (!Registry_Load_Render_Device(sub_key, device_name, sizeof(device_name), width,
		height, depth, windowed, texture_depth) || device_name[0] == '\0')
	{
		return Set_Any_Render_Device();
	}

	if (texture_depth == 16 || texture_depth == 32)
	{
		Set_Texture_Bitdepth(texture_depth);
	}
	else
	{
		Set_Texture_Bitdepth(16);
	}
	if (Set_Render_Device(device_name, width, height, depth, windowed, resize_window))
	{
		return true;
	}
	// DX11 always exposes a 32-bit swap chain, so a failed stored mode is
	// recoverable by selecting the active adapter's current window size.
	return Set_Any_Render_Device();
}

template <typename Host>
bool DX11DeviceBackend<Host>::Registry_Load_Render_Device(const char * sub_key, char * device, int device_len, int & width, int & height, int & depth, int & windowed, int & texture_depth)
{
	if (device == nullptr || device_len <= 0 || sub_key == nullptr || sub_key[0] == '\0')
	{
		return false;
	}
	device[0] = '\0';
	width = -1;
	height = -1;
	depth = -1;
	windowed = -1;
	texture_depth = -1;
	RegistryClass registry(sub_key, false);
	if (!registry.Is_Valid())
	{
		return false;
	}
	registry.Get_String(kRegistryDeviceName, device, device_len, "");
	width = registry.Get_Int(kRegistryWidth, -1);
	height = registry.Get_Int(kRegistryHeight, -1);
	depth = registry.Get_Int(kRegistryDepth, -1);
	windowed = registry.Get_Int(kRegistryWindowed, -1);
	texture_depth = registry.Get_Int(kRegistryTextureDepth, -1);
	return true;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Texture_Bitdepth(int depth)
{
	this->State().texture_bitdepth = depth;
}

template <typename Host>
int DX11DeviceBackend<Host>::Get_Texture_Bitdepth() const
{
	return this->State().texture_bitdepth;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Multisample_Mode(RenderBackendMultisampleMode mode)
{
	this->State().multisample_mode = mode;
}

template <typename Host>
RenderBackendMultisampleMode DX11DeviceBackend<Host>::Get_Multisample_Mode() const
{
	return this->State().multisample_mode;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Gamma(float gamma, float bright, float contrast, bool calibrate, bool uselimit)
{
	(void)gamma;
	(void)bright;
	(void)contrast;
	(void)calibrate;
	(void)uselimit;
}

template <typename Host>
void DX11DeviceBackend<Host>::Begin_Scene()
{
	DX11BackendState &impl = this->State();
	if (impl.device_status != RenderBackendDeviceStatus::Ready || impl.context == nullptr)
	{
		return;
	}

	impl.scene_active = true;
	ID3D11RenderTargetView *render_view = impl.active_render_target_view;
	ID3D11DepthStencilView *depth_view = impl.active_depth_stencil_view;
	if (impl.render_to_texture)
	{
		// Set_Render_Target has already installed the target views. Do not
		// overwrite them when a scene is started for an off-screen pass.
		return;
	}
	impl.context->OMSetRenderTargets(1, &render_view, depth_view);
	D3D11_VIEWPORT native_viewport = {};
	native_viewport.TopLeftX = static_cast<float>(impl.viewport.x);
	native_viewport.TopLeftY = static_cast<float>(impl.viewport.y);
	native_viewport.Width = static_cast<float>(impl.viewport.width);
	native_viewport.Height = static_cast<float>(impl.viewport.height);
	native_viewport.MinDepth = impl.viewport.min_z;
	native_viewport.MaxDepth = impl.viewport.max_z;
	impl.context->RSSetViewports(1, &native_viewport);
}

template <typename Host>
void DX11DeviceBackend<Host>::End_Scene(bool flip_frame)
{
	DX11BackendState &impl = this->State();
	if (!impl.scene_active)
	{
		return;
	}

	this->Backend().Apply_Render_State_Changes();
	if (flip_frame && impl.swap_chain != nullptr && !impl.render_to_texture)
	{
		const HRESULT result = impl.swap_chain->Present(0, 0);
		if (SUCCEEDED(result))
		{
			impl.device_status = RenderBackendDeviceStatus::Ready;
		}
		else if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
		{
			impl.device_status = RenderBackendDeviceStatus::NeedsReset;
		}
		else
		{
			impl.device_status = RenderBackendDeviceStatus::NeedsReset;
		}
	}

	impl.scene_active = false;
	this->Backend().Set_Vertex_Buffer(static_cast<RenderBackendVertexBuffer *>(nullptr), 0, 0, 0);
	this->Backend().Set_Index_Buffer(static_cast<RenderBackendIndexBuffer *>(nullptr));
	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage)
	{
		this->Backend().Set_Texture_Resource(stage, nullptr);
	}
	impl.direct_vertex_binding_override = false;
	impl.direct_index_binding_override = false;
	impl.applied_render_state_valid = false;
}

template <typename Host>
void DX11DeviceBackend<Host>::Flip_To_Primary()
{
	DX11BackendState &impl = this->State();
	if (impl.swap_chain != nullptr && !impl.render_to_texture)
	{
		if (SUCCEEDED(impl.swap_chain->Present(0, 0)))
		{
		}
		else
		{
			impl.device_status = RenderBackendDeviceStatus::NeedsReset;
		}
	}
}

template <typename Host>
void DX11DeviceBackend<Host>::Clear(bool clear_color, bool clear_z_stencil, const Vector3 & color, float dest_alpha, float z, unsigned int stencil)
{
	DX11BackendState &impl = this->State();
	if (impl.context == nullptr)
	{
		return;
	}

	const float clear_value[4] = {color[0], color[1], color[2], dest_alpha};
	if (clear_color)
	{
		ID3D11RenderTargetView *view = impl.active_render_target_view;
		if (view != nullptr)
		{
			impl.context->ClearRenderTargetView(view, clear_value);
		}
	}
	if (clear_z_stencil && impl.active_depth_stencil_view != nullptr)
	{
		const UINT flags = D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL;
		impl.context->ClearDepthStencilView(impl.active_depth_stencil_view, flags, z,
			static_cast<UINT8>(stencil));
	}
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Viewport(const RenderBackendViewport & viewport)
{
	this->State().viewport = viewport;
	this->State().Mark_Constant_State_Dirty();
	if (this->State().context != nullptr)
	{
		D3D11_VIEWPORT native_viewport = {};
		native_viewport.TopLeftX = static_cast<float>(viewport.x);
		native_viewport.TopLeftY = static_cast<float>(viewport.y);
		native_viewport.Width = static_cast<float>(viewport.width);
		native_viewport.Height = static_cast<float>(viewport.height);
		native_viewport.MinDepth = viewport.min_z;
		native_viewport.MaxDepth = viewport.max_z;
		this->State().context->RSSetViewports(1, &native_viewport);
	}
}

template <typename Host>
bool DX11DeviceBackend<Host>::Get_Viewport(RenderBackendViewport & viewport) const
{
	viewport = this->State().viewport;
	return true;
}

template <typename Host>
void DX11DeviceBackend<Host>::Show_Cursor(bool show)
{
	if (this->State().cursor_visible != show)
	{
		this->State().cursor_visible = show;
		// SDL hides the cursor for hardware WW3D cursors, while the old DX9
		// backend used the device cursor counter. Keep the Win32 display count
		// balanced when the backend owns that cursor state.
		if (show)
		{
			for (unsigned attempt = 0; attempt < 128 && ::ShowCursor(TRUE) < 0; ++attempt)
			{
			}
		}
		else
		{
			for (unsigned attempt = 0; attempt < 128 && ::ShowCursor(FALSE) >= 0; ++attempt)
			{
			}
		}
	}
	::SetCursor(this->State().cursor_visible ? this->State().cursor : nullptr);
}

template <typename Host>
bool DX11DeviceBackend<Host>::Set_Cursor_Properties(int hotspot_x, int hotspot_y, SurfaceClass * surface)
{
	if (surface == nullptr)
	{
		return false;
	}

	DX11Surface *source = As_DX11_Surface(surface->Get_Render_Backend_Surface());
	if (source == nullptr || source->width == 0 || source->height == 0 ||
		Is_Compressed_Format(source->format))
	{
		return false;
	}

	BITMAPINFO bitmap_info = {};
	bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info.bmiHeader.biWidth = static_cast<LONG>(source->width);
	bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(source->height);
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;
	void *color_bits = nullptr;
	HBITMAP color_bitmap = CreateDIBSection(nullptr, &bitmap_info, DIB_RGB_COLORS,
		&color_bits, nullptr, 0);
	if (color_bitmap == nullptr || color_bits == nullptr)
	{
		if (color_bitmap != nullptr)
		{
			DeleteObject(color_bitmap);
		}
		return false;
	}

	const unsigned source_bpp = Format_Bytes_Per_Pixel(source->format);
	unsigned char *destination_bits = static_cast<unsigned char *>(color_bits);
	for (unsigned y = 0; y < source->height; ++y)
	{
		const unsigned char *source_row = source->pixels.data() +
			static_cast<std::size_t>(y) * source->pitch;
		unsigned char *destination_row = destination_bits +
			static_cast<std::size_t>(y) * source->width * 4u;
		for (unsigned x = 0; x < source->width; ++x)
		{
			const unsigned char *source_pixel = source_row +
				static_cast<std::size_t>(x) * source_bpp;
			BitmapHandlerClass::Copy_Pixel(destination_row + x * 4u,
				WW3D_FORMAT_A8R8G8B8, source_pixel, source->format, nullptr, 0);
		}
	}

	HBITMAP mask_bitmap = CreateBitmap(static_cast<int>(source->width),
		static_cast<int>(source->height), 1, 1, nullptr);
	if (mask_bitmap == nullptr)
	{
		DeleteObject(color_bitmap);
		return false;
	}
	ICONINFO icon_info = {};
	icon_info.fIcon = FALSE;
	icon_info.xHotspot = static_cast<DWORD>(std::clamp(hotspot_x, 0,
		static_cast<int>(source->width) - 1));
	icon_info.yHotspot = static_cast<DWORD>(std::clamp(hotspot_y, 0,
		static_cast<int>(source->height) - 1));
	icon_info.hbmMask = mask_bitmap;
	icon_info.hbmColor = color_bitmap;
	HCURSOR new_cursor = reinterpret_cast<HCURSOR>(CreateIconIndirect(&icon_info));
	DeleteObject(mask_bitmap);
	DeleteObject(color_bitmap);
	if (new_cursor == nullptr)
	{
		return false;
	}

	if (this->State().cursor != nullptr)
	{
		DestroyCursor(this->State().cursor);
	}
	this->State().cursor = new_cursor;
	::SetCursor(this->State().cursor_visible ? this->State().cursor : nullptr);
	return true;
}

template <typename Host>
void DX11DeviceBackend<Host>::Set_Cursor_Position(int x, int y)
{
	::SetCursorPos(x, y);
}

template class DX11DeviceBackend<DX11BackendRuntime>;
}
