module;

#define NOMINMAX

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <dxgi.h>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <windows.h>

export module Graphics.Backends.DX11;

export import Graphics.RHI;

import Graphics.Resources.Pools.ResourcePool;
import Graphics.RHI.Frame;

namespace Graphics
{

template <typename Interface>
class DX11NativeObject final
{
public:
	DX11NativeObject() noexcept = default;
	~DX11NativeObject() noexcept
	{
		Reset();
	}

	DX11NativeObject(const DX11NativeObject &) = delete;
	DX11NativeObject &operator=(const DX11NativeObject &) = delete;

	DX11NativeObject(DX11NativeObject &&other) noexcept
		: m_object(other.m_object)
	{
		other.m_object = nullptr;
	}

	DX11NativeObject &operator=(DX11NativeObject &&other) noexcept
	{
		if (this == &other)
			return *this;

		Reset();
		m_object = other.m_object;
		other.m_object = nullptr;
		return *this;
	}

	void Reset(Interface *object = nullptr) noexcept
	{
		if (m_object != nullptr)
			m_object->Release();
		m_object = object;
	}

	Interface *Get() const noexcept
	{
		return m_object;
	}

private:
	Interface *m_object = nullptr;
};

struct DX11Buffer final
{
	DX11NativeObject<ID3D11Buffer> object;
	DX11NativeObject<ID3D11ShaderResourceView> shader_resource_view;
	RHIBufferUsage usage = RHIBufferUsage::Vertex;
	std::uint32_t byte_size = 0;
};

struct DX11Texture final
{
	DX11NativeObject<ID3D11Texture2D> object;
	DX11NativeObject<ID3D11ShaderResourceView> shader_resource_view;
	DX11NativeObject<ID3D11RenderTargetView> render_target_view;
	DX11NativeObject<ID3D11DepthStencilView> depth_stencil_view;
	DX11NativeObject<ID3D11UnorderedAccessView> unordered_access_view;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	RHITextureFormat format = RHITextureFormat::RGBA8_UNorm;
};

struct DX11Pipeline final
{
	std::uint64_t key = 0;
	DX11NativeObject<ID3D11VertexShader> vertex_shader;
	DX11NativeObject<ID3D11PixelShader> pixel_shader;
	DX11NativeObject<ID3D11InputLayout> input_layout;
	DX11NativeObject<ID3D11DepthStencilState> depth_stencil_state;
	DX11NativeObject<ID3D11BlendState> blend_state;
	DX11NativeObject<ID3D11RasterizerState> rasterizer_state;
	DX11NativeObject<ID3D11SamplerState> sampler_state;
	RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
};

static_assert(std::is_nothrow_move_constructible_v<DX11Buffer>);
static_assert(std::is_nothrow_move_assignable_v<DX11Buffer>);
static_assert(std::is_nothrow_move_constructible_v<DX11Texture>);
static_assert(std::is_nothrow_move_assignable_v<DX11Texture>);
static_assert(std::is_nothrow_move_constructible_v<DX11Pipeline>);
static_assert(std::is_nothrow_move_assignable_v<DX11Pipeline>);

struct DX11DeviceState;

export struct DX11SharedFrameResources final
{
	void *device = nullptr;
	void *context = nullptr;
	void *swap_chain = nullptr;
	void *back_buffer = nullptr;
	void *back_buffer_view = nullptr;
	void *depth_buffer = nullptr;
	void *depth_buffer_view = nullptr;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

class DX11SwapChain final : public SwapChain
{
public:
	explicit DX11SwapChain(DX11DeviceState *state) noexcept
		: m_state(state)
	{
	}

	~DX11SwapChain() noexcept override;

	bool Is_Valid() const noexcept override;
	RHIBackbuffer Backbuffer() const noexcept override;
	RHIDepthTarget Depth_Target() const noexcept override;
	bool Resize(std::uint32_t width, std::uint32_t height) override;
	bool Present() noexcept override;

	bool Create_Targets(std::uint32_t width, std::uint32_t height);
	bool Adopt_Targets(const DX11SharedFrameResources &resources);

private:
	DX11DeviceState *m_state = nullptr;
	RHITextureHandle m_backbuffer{};
	RHITextureHandle m_depth_target{};
	std::uint32_t m_width = 0;
	std::uint32_t m_height = 0;
};

class DX11CommandList final : public CommandList
{
public:
	explicit DX11CommandList(DX11DeviceState *state) noexcept
		: m_state(state)
	{
	}

	bool Bind_Pipeline(RHIPipelineHandle pipeline) noexcept override;
	bool Set_Bindless_Resources(std::span<const RHIBindlessResource> resources) noexcept override;
	bool Set_Render_Targets(RHITextureHandle color_target, RHITextureHandle depth_target) noexcept override;
	bool Set_Color_Target(RHITextureHandle color_target) noexcept override;
	bool Set_Depth_Target(RHITextureHandle depth_target) noexcept override;
	bool Clear(const std::array<float, 4> &color, float depth) noexcept override;
	bool Clear_Depth(float depth) noexcept override;
	bool Copy_Texture(RHITextureHandle source, RHITextureHandle destination) noexcept override;
	bool Set_Viewport(RHIViewport viewport) noexcept override;
	bool Set_Vertex_Buffer(std::uint32_t slot, RHIBufferHandle buffer, std::uint32_t stride, std::uint32_t offset) noexcept override;
	bool Set_Index_Buffer(RHIBufferHandle buffer, RHIIndexFormat format, std::uint32_t offset) noexcept override;
	bool Draw(std::uint32_t vertex_count, std::uint32_t first_vertex, std::uint32_t instance_count, std::uint32_t first_instance) noexcept override;
	bool Draw_Indexed(std::uint32_t index_count, std::uint32_t first_index, std::int32_t base_vertex, std::uint32_t instance_count, std::uint32_t first_instance) noexcept override;
	bool Reset_State() noexcept override;
	void Reset_Frame_State() noexcept;

private:
	bool Is_Ready() const noexcept;
	bool Is_Pipeline_Valid() const noexcept;
	bool Bind_Texture_At_Slot(RHIShaderStage stage, std::uint32_t slot, RHITextureHandle texture) noexcept;
	bool Bind_Buffer_At_Slot(RHIShaderStage stage, std::uint32_t slot, RHIBufferHandle buffer) noexcept;

	DX11DeviceState *m_state = nullptr;
	RHIPipelineHandle m_pipeline{};
	RHITextureHandle m_color_target{};
	RHITextureHandle m_depth_target{};
	std::span<const RHIBindlessResource> m_bindless_resources{};
};

struct DX11DeviceState final
{
	DX11NativeObject<ID3D11Device> device;
	DX11NativeObject<ID3D11DeviceContext> context;
	DX11NativeObject<IDXGISwapChain> native_swap_chain;
	ResourcePool<DX11Buffer, RHIBufferHandle> buffers;
	ResourcePool<DX11Texture, RHITextureHandle> textures;
	ResourcePool<DX11Pipeline, RHIPipelineHandle> pipelines;
	DX11SwapChain swap_chain;
	DX11CommandList command_list;
	std::string shader_directory;
	std::string vertex_shader_name;
	std::string fragment_shader_name;
	bool frame_active = false;
	bool shared_frame = false;
	bool ready_to_present = false;
	bool presented = false;

	DX11DeviceState() noexcept
		: swap_chain(this),
		  command_list(this)
	{
	}
};

template <typename Interface>
static Interface *Retain(Interface *object) noexcept
{
	if (object != nullptr)
		object->AddRef();

	return object;
}

static DXGI_FORMAT To_DX11_Format(RHITextureFormat format) noexcept
{
	switch (format) {
	case RHITextureFormat::R8_UNorm:
		return DXGI_FORMAT_R8_UNORM;
	case RHITextureFormat::RG8_UNorm:
		return DXGI_FORMAT_R8G8_UNORM;
	case RHITextureFormat::RGBA8_UNorm:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case RHITextureFormat::BGRA8_UNorm:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	case RHITextureFormat::RGBA16_Float:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case RHITextureFormat::RGBA32_Float:
		return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case RHITextureFormat::R32_Float:
		return DXGI_FORMAT_R32_FLOAT;
	case RHITextureFormat::D24_UNorm_S8:
		return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case RHITextureFormat::D32_Float:
		return DXGI_FORMAT_D32_FLOAT;
	}

	return DXGI_FORMAT_UNKNOWN;
}

static std::uint32_t To_DX11_Bind_Flags(RHIBufferUsage usage) noexcept
{
	switch (usage) {
	case RHIBufferUsage::Vertex:
		return D3D11_BIND_VERTEX_BUFFER;
	case RHIBufferUsage::Index:
		return D3D11_BIND_INDEX_BUFFER;
	case RHIBufferUsage::Constant:
		return D3D11_BIND_CONSTANT_BUFFER;
	case RHIBufferUsage::Storage:
		return D3D11_BIND_SHADER_RESOURCE;
	}

	return 0;
}

static D3D11_PRIMITIVE_TOPOLOGY To_DX11_Topology(RHIPrimitiveTopology topology) noexcept
{
	switch (topology) {
	case RHIPrimitiveTopology::TriangleList:
		return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case RHIPrimitiveTopology::PointList:
		return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	}

	return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static D3D11_CULL_MODE To_DX11_Cull_Mode(RHICullMode mode) noexcept
{
	return mode == RHICullMode::None ? D3D11_CULL_NONE : D3D11_CULL_BACK;
}

static bool Create_DX11_Pipeline(
	ID3D11Device *device,
	const RHIPipeline &description,
	std::span<const std::byte> vertex_bytecode,
	std::span<const std::byte> pixel_bytecode,
	DX11Pipeline &pipeline) noexcept
{
	if (device == nullptr || (description.topology != RHIPrimitiveTopology::TriangleList
		&& description.topology != RHIPrimitiveTopology::PointList)
		|| (description.vertex_format != RHIVertexFormat::Position3Color4UV2
			&& description.vertex_format != RHIVertexFormat::Position3Color4UV2ResourceIndex
			&& description.vertex_format != RHIVertexFormat::Position3Color4UV2Skinned))
		return false;
	if (vertex_bytecode.empty() || pixel_bytecode.empty())
		return false;

	ID3D11VertexShader *native_vertex_shader = nullptr;
	if (FAILED(device->CreateVertexShader(vertex_bytecode.data(), vertex_bytecode.size(), nullptr, &native_vertex_shader)))
		return false;
	pipeline.vertex_shader.Reset(native_vertex_shader);

	ID3D11PixelShader *native_pixel_shader = nullptr;
	if (FAILED(device->CreatePixelShader(pixel_bytecode.data(), pixel_bytecode.size(), nullptr, &native_pixel_shader)))
		return false;
	pipeline.pixel_shader.Reset(native_pixel_shader);

	const D3D11_INPUT_ELEMENT_DESC standard_input_elements[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 1, DXGI_FORMAT_R32_UINT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	const D3D11_INPUT_ELEMENT_DESC skinned_input_elements[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	const bool is_skinned = description.vertex_format == RHIVertexFormat::Position3Color4UV2Skinned;
	const D3D11_INPUT_ELEMENT_DESC *input_elements = is_skinned ? skinned_input_elements : standard_input_elements;
	const UINT input_element_count = is_skinned ? 5u : description.vertex_format == RHIVertexFormat::Position3Color4UV2 ? 3u : 4u;
	ID3D11InputLayout *input_layout = nullptr;
	if (FAILED(device->CreateInputLayout(input_elements, input_element_count, vertex_bytecode.data(), vertex_bytecode.size(), &input_layout)))
		return false;
	pipeline.input_layout.Reset(input_layout);

	D3D11_DEPTH_STENCIL_DESC depth_description{};
	depth_description.DepthEnable = description.depth_test ? TRUE : FALSE;
	depth_description.DepthWriteMask = description.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	depth_description.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	ID3D11DepthStencilState *depth_state = nullptr;
	if (FAILED(device->CreateDepthStencilState(&depth_description, &depth_state)))
		return false;
	pipeline.depth_stencil_state.Reset(depth_state);

	D3D11_BLEND_DESC blend_description{};
	D3D11_RENDER_TARGET_BLEND_DESC &render_target_blend = blend_description.RenderTarget[0];
	render_target_blend.BlendEnable = description.blend_mode == RHIBlendMode::Disabled ? FALSE : TRUE;
	switch (description.blend_mode) {
	case RHIBlendMode::Disabled:
	case RHIBlendMode::Additive:
		render_target_blend.SrcBlend = D3D11_BLEND_ONE;
		render_target_blend.DestBlend = description.blend_mode == RHIBlendMode::Additive ? D3D11_BLEND_ONE : D3D11_BLEND_ZERO;
		break;
	case RHIBlendMode::Alpha:
		render_target_blend.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		render_target_blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		break;
	case RHIBlendMode::Multiply:
		render_target_blend.SrcBlend = D3D11_BLEND_ZERO;
		render_target_blend.DestBlend = D3D11_BLEND_SRC_COLOR;
		break;
	}
	render_target_blend.BlendOp = D3D11_BLEND_OP_ADD;
	render_target_blend.SrcBlendAlpha = D3D11_BLEND_ONE;
	render_target_blend.DestBlendAlpha = description.blend_mode == RHIBlendMode::Alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ZERO;
	render_target_blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	render_target_blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	ID3D11BlendState *blend_state = nullptr;
	if (FAILED(device->CreateBlendState(&blend_description, &blend_state)))
		return false;
	pipeline.blend_state.Reset(blend_state);

	D3D11_RASTERIZER_DESC rasterizer_description{};
	rasterizer_description.FillMode = D3D11_FILL_SOLID;
	rasterizer_description.CullMode = To_DX11_Cull_Mode(description.cull_mode);
	rasterizer_description.FrontCounterClockwise = FALSE;
	rasterizer_description.DepthClipEnable = TRUE;
	ID3D11RasterizerState *rasterizer_state = nullptr;
	if (FAILED(device->CreateRasterizerState(&rasterizer_description, &rasterizer_state)))
		return false;
	pipeline.rasterizer_state.Reset(rasterizer_state);

	D3D11_SAMPLER_DESC sampler_description{};
	sampler_description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_description.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler_description.MinLOD = 0.0f;
	sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
	ID3D11SamplerState *sampler_state = nullptr;
	if (FAILED(device->CreateSamplerState(&sampler_description, &sampler_state)))
		return false;
	pipeline.sampler_state.Reset(sampler_state);

	pipeline.key = description.key;
	pipeline.topology = description.topology;
	return true;
}

static bool Has_Texture_Usage(const RHITexture &description, RHITextureUsage usage) noexcept
{
	return (description.usage & static_cast<std::uint32_t>(usage)) != 0;
}

static std::uint32_t To_DX11_Texture_Bind_Flags(const RHITexture &description) noexcept
{
	std::uint32_t flags = 0;
	if (Has_Texture_Usage(description, RHITextureUsage::ShaderResource))
		flags |= D3D11_BIND_SHADER_RESOURCE;
	if (Has_Texture_Usage(description, RHITextureUsage::RenderTarget))
		flags |= D3D11_BIND_RENDER_TARGET;
	if (Has_Texture_Usage(description, RHITextureUsage::DepthStencil))
		flags |= D3D11_BIND_DEPTH_STENCIL;
	if (Has_Texture_Usage(description, RHITextureUsage::UnorderedAccess))
		flags |= D3D11_BIND_UNORDERED_ACCESS;
	return flags;
}

static std::uint32_t To_DX11_Bytes_Per_Pixel(RHITextureFormat format) noexcept
{
	switch (format) {
	case RHITextureFormat::R8_UNorm:
		return 1;
	case RHITextureFormat::RG8_UNorm:
		return 2;
	case RHITextureFormat::RGBA8_UNorm:
	case RHITextureFormat::BGRA8_UNorm:
	case RHITextureFormat::R32_Float:
	case RHITextureFormat::D24_UNorm_S8:
	case RHITextureFormat::D32_Float:
		return 4;
	case RHITextureFormat::RGBA16_Float:
		return 8;
	case RHITextureFormat::RGBA32_Float:
		return 16;
	}

	return 0;
}

export struct DX11DeviceOptions final
{
	bool use_warp = false;
	void *window = nullptr;
	std::uint32_t width = 1280;
	std::uint32_t height = 720;
	const char *shader_directory = nullptr;
	const char *vertex_shader_name = "visual_basic.vso";
	const char *fragment_shader_name = "visual_basic.pso";
	const DX11SharedFrameResources *shared_frame = nullptr;
};

static bool Create_DX11_Device(DX11DeviceState &state, const DX11DeviceOptions &options) noexcept
{
	if (options.shared_frame != nullptr) {
		const DX11SharedFrameResources &resources = *options.shared_frame;
		if (resources.device == nullptr || resources.context == nullptr || resources.swap_chain == nullptr)
			return false;

		state.device.Reset(Retain(static_cast<ID3D11Device *>(resources.device)));
		state.context.Reset(Retain(static_cast<ID3D11DeviceContext *>(resources.context)));
		state.native_swap_chain.Reset(Retain(static_cast<IDXGISwapChain *>(resources.swap_chain)));
		return state.device.Get() != nullptr && state.context.Get() != nullptr && state.native_swap_chain.Get() != nullptr;
	}

	const D3D_FEATURE_LEVEL feature_levels[] = {
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	const D3D_DRIVER_TYPE requested_driver = options.use_warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
	D3D_FEATURE_LEVEL selected_feature_level = D3D_FEATURE_LEVEL_10_0;
	ID3D11Device *native_device = nullptr;
	ID3D11DeviceContext *native_context = nullptr;
	IDXGISwapChain *native_swap_chain = nullptr;
	HRESULT result = E_FAIL;
	if (options.window != nullptr && options.width != 0 && options.height != 0) {
		DXGI_SWAP_CHAIN_DESC swap_chain_description{};
		swap_chain_description.BufferDesc.Width = options.width;
		swap_chain_description.BufferDesc.Height = options.height;
		swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swap_chain_description.BufferDesc.RefreshRate.Numerator = 60;
		swap_chain_description.BufferDesc.RefreshRate.Denominator = 1;
		swap_chain_description.SampleDesc.Count = 1;
		swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swap_chain_description.BufferCount = 2;
		swap_chain_description.OutputWindow = static_cast<HWND>(options.window);
		swap_chain_description.Windowed = TRUE;
		swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			requested_driver,
			nullptr,
			0,
			feature_levels,
			static_cast<UINT>(std::size(feature_levels)),
			D3D11_SDK_VERSION,
			&swap_chain_description,
			&native_swap_chain,
			&native_device,
			&selected_feature_level,
			&native_context);
	} else {
		result = D3D11CreateDevice(
			nullptr,
			requested_driver,
			nullptr,
			0,
			feature_levels,
			static_cast<UINT>(std::size(feature_levels)),
			D3D11_SDK_VERSION,
			&native_device,
			&selected_feature_level,
			&native_context);
	}
	if (FAILED(result) && !options.use_warp) {
		if (native_device != nullptr)
			native_device->Release();
		if (native_context != nullptr)
			native_context->Release();
		if (native_swap_chain != nullptr)
			native_swap_chain->Release();
		native_device = nullptr;
		native_context = nullptr;
		native_swap_chain = nullptr;
		if (options.window != nullptr && options.width != 0 && options.height != 0) {
			DXGI_SWAP_CHAIN_DESC swap_chain_description{};
			swap_chain_description.BufferDesc.Width = options.width;
			swap_chain_description.BufferDesc.Height = options.height;
			swap_chain_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swap_chain_description.BufferDesc.RefreshRate.Numerator = 60;
			swap_chain_description.BufferDesc.RefreshRate.Denominator = 1;
			swap_chain_description.SampleDesc.Count = 1;
			swap_chain_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_description.BufferCount = 2;
			swap_chain_description.OutputWindow = static_cast<HWND>(options.window);
			swap_chain_description.Windowed = TRUE;
			swap_chain_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
			result = D3D11CreateDeviceAndSwapChain(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				0,
				feature_levels,
				static_cast<UINT>(std::size(feature_levels)),
				D3D11_SDK_VERSION,
				&swap_chain_description,
				&native_swap_chain,
				&native_device,
				&selected_feature_level,
				&native_context);
		} else {
			result = D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				0,
				feature_levels,
				static_cast<UINT>(std::size(feature_levels)),
				D3D11_SDK_VERSION,
				&native_device,
				&selected_feature_level,
				&native_context);
		}
	}

	if (SUCCEEDED(result)) {
		state.device.Reset(native_device);
		state.context.Reset(native_context);
		state.native_swap_chain.Reset(native_swap_chain);
	} else {
		if (native_device != nullptr)
			native_device->Release();
		if (native_context != nullptr)
			native_context->Release();
		if (native_swap_chain != nullptr)
			native_swap_chain->Release();
	}
	return SUCCEEDED(result) && state.device.Get() != nullptr && state.context.Get() != nullptr;
}

static bool Load_Shader_Binary(const std::string &directory, const char *name, std::vector<std::byte> &data)
{
	const std::string path = directory.empty() ? std::string(name) : directory + "/" + name;
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return false;

	const std::streampos end = file.tellg();
	if (end <= 0)
		return false;

	data.resize(static_cast<std::size_t>(end));
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
	return file.good() || file.eof();
}

export class DX11Device final : public Device
{
public:
	explicit DX11Device(DX11DeviceOptions options = {});
	~DX11Device() noexcept override = default;

	DX11Device(const DX11Device &) = delete;
	DX11Device &operator=(const DX11Device &) = delete;

	bool Is_Valid() const noexcept override;
	RHIBufferHandle Create_Buffer(const RHIBuffer &description) override;
	RHITextureHandle Create_Texture(const RHITexture &description) override;
	RHIPipelineHandle Create_Pipeline(const RHIPipeline &description) override;
	RHIBufferHandle Create_Buffer_Initialized(const RHIBuffer &description, std::span<const std::byte> initial_data) override;
	RHITextureHandle Create_Texture_Initialized(const RHITexture &description, const RHITextureUpload &initial_data) override;
	RHIPipelineHandle Create_Pipeline(const RHIPipeline &description, RHIShaderBytecode vertex_shader, RHIShaderBytecode fragment_shader) override;
	bool Update_Buffer(RHIBufferHandle buffer, std::uint32_t offset, std::span<const std::byte> data) noexcept override;
	bool Update_Texture(RHITextureHandle texture, const RHITextureUpload &data) noexcept override;
	bool Readback_Texture(RHITextureHandle texture, std::span<std::byte> data, std::uint32_t row_pitch) noexcept override;
	bool Destroy_Buffer(RHIBufferHandle buffer) noexcept override;
	bool Destroy_Texture(RHITextureHandle texture) noexcept override;
	bool Destroy_Pipeline(RHIPipelineHandle pipeline) noexcept override;
	CommandList &Immediate_Command_List() noexcept override;
	SwapChain &Get_Swap_Chain() noexcept override;
	bool Adopt_Shared_Frame(const DX11SharedFrameResources &resources);
	bool Begin_Frame() noexcept override;
	bool End_Frame() noexcept override;

private:
	std::unique_ptr<DX11DeviceState> m_state;
};

DX11SwapChain::~DX11SwapChain() noexcept
{
	if (m_state == nullptr)
		return;

	m_state->textures.Destroy(m_backbuffer);
	m_state->textures.Destroy(m_depth_target);
}

bool DX11SwapChain::Is_Valid() const noexcept
{
	return m_state != nullptr
		&& m_state->device.Get() != nullptr
		&& m_state->context.Get() != nullptr
		&& m_state->native_swap_chain.Get() != nullptr
		&& m_backbuffer.Is_Valid()
		&& m_depth_target.Is_Valid();
}

RHIBackbuffer DX11SwapChain::Backbuffer() const noexcept
{
	return {m_backbuffer, m_width, m_height};
}

RHIDepthTarget DX11SwapChain::Depth_Target() const noexcept
{
	return {m_depth_target, m_width, m_height};
}

bool DX11SwapChain::Create_Targets(std::uint32_t width, std::uint32_t height)
{
	if (m_state == nullptr || m_state->device.Get() == nullptr || m_state->context.Get() == nullptr || m_state->native_swap_chain.Get() == nullptr || width == 0 || height == 0)
		return false;

	ID3D11DeviceContext *context = m_state->context.Get();
	context->OMSetRenderTargets(0, nullptr, nullptr);
	m_state->textures.Destroy(m_backbuffer);
	m_state->textures.Destroy(m_depth_target);
	m_backbuffer = {};
	m_depth_target = {};

	ID3D11Texture2D *native_backbuffer = nullptr;
	if (FAILED(m_state->native_swap_chain.Get()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&native_backbuffer))))
		return false;

	DX11Texture backbuffer;
	backbuffer.object.Reset(native_backbuffer);
	backbuffer.width = width;
	backbuffer.height = height;
	backbuffer.format = RHITextureFormat::RGBA8_UNorm;
	ID3D11RenderTargetView *native_render_target = nullptr;
	if (FAILED(m_state->device.Get()->CreateRenderTargetView(native_backbuffer, nullptr, &native_render_target)))
		return false;
	backbuffer.render_target_view.Reset(native_render_target);
	const RHITextureHandle backbuffer_handle = m_state->textures.Create(std::move(backbuffer));
	if (!backbuffer_handle.Is_Valid())
		return false;

	D3D11_TEXTURE2D_DESC depth_description{};
	depth_description.Width = width;
	depth_description.Height = height;
	depth_description.MipLevels = 1;
	depth_description.ArraySize = 1;
	depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_description.SampleDesc.Count = 1;
	depth_description.Usage = D3D11_USAGE_DEFAULT;
	depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	ID3D11Texture2D *native_depth = nullptr;
	if (FAILED(m_state->device.Get()->CreateTexture2D(&depth_description, nullptr, &native_depth))) {
		m_state->textures.Destroy(backbuffer_handle);
		return false;
	}

	DX11Texture depth;
	depth.object.Reset(native_depth);
	depth.width = width;
	depth.height = height;
	depth.format = RHITextureFormat::D24_UNorm_S8;
	ID3D11DepthStencilView *native_depth_view = nullptr;
	if (FAILED(m_state->device.Get()->CreateDepthStencilView(native_depth, nullptr, &native_depth_view))) {
		m_state->textures.Destroy(backbuffer_handle);
		return false;
	}
	depth.depth_stencil_view.Reset(native_depth_view);
	const RHITextureHandle depth_handle = m_state->textures.Create(std::move(depth));
	if (!depth_handle.Is_Valid()) {
		m_state->textures.Destroy(backbuffer_handle);
		return false;
	}

	m_backbuffer = backbuffer_handle;
	m_depth_target = depth_handle;
	m_width = width;
	m_height = height;
	return true;
}

bool DX11SwapChain::Adopt_Targets(const DX11SharedFrameResources &resources)
{
	if (m_state == nullptr || m_state->device.Get() == nullptr || m_state->context.Get() == nullptr || m_state->native_swap_chain.Get() == nullptr
		|| resources.back_buffer == nullptr || resources.back_buffer_view == nullptr || resources.depth_buffer == nullptr || resources.depth_buffer_view == nullptr
		|| resources.width == 0 || resources.height == 0)
		return false;

	DX11Texture *current_backbuffer = m_state->textures.Resolve(m_backbuffer);
	DX11Texture *current_depth = m_state->textures.Resolve(m_depth_target);
	if (current_backbuffer != nullptr && current_depth != nullptr
		&& current_backbuffer->object.Get() == static_cast<ID3D11Texture2D *>(resources.back_buffer)
		&& current_backbuffer->render_target_view.Get() == static_cast<ID3D11RenderTargetView *>(resources.back_buffer_view)
		&& current_depth->object.Get() == static_cast<ID3D11Texture2D *>(resources.depth_buffer)
		&& current_depth->depth_stencil_view.Get() == static_cast<ID3D11DepthStencilView *>(resources.depth_buffer_view)) {
		m_width = resources.width;
		m_height = resources.height;
		return true;
	}

	DX11Texture backbuffer;
	backbuffer.object.Reset(Retain(static_cast<ID3D11Texture2D *>(resources.back_buffer)));
	backbuffer.render_target_view.Reset(Retain(static_cast<ID3D11RenderTargetView *>(resources.back_buffer_view)));
	backbuffer.width = resources.width;
	backbuffer.height = resources.height;
	backbuffer.format = RHITextureFormat::BGRA8_UNorm;

	DX11Texture depth;
	depth.object.Reset(Retain(static_cast<ID3D11Texture2D *>(resources.depth_buffer)));
	depth.depth_stencil_view.Reset(Retain(static_cast<ID3D11DepthStencilView *>(resources.depth_buffer_view)));
	depth.width = resources.width;
	depth.height = resources.height;
	depth.format = RHITextureFormat::D24_UNorm_S8;

	if (m_backbuffer.Is_Valid() && m_depth_target.Is_Valid() && current_backbuffer != nullptr && current_depth != nullptr) {
		*current_backbuffer = std::move(backbuffer);
		*current_depth = std::move(depth);
	} else {
		m_state->textures.Destroy(m_backbuffer);
		m_state->textures.Destroy(m_depth_target);
		m_backbuffer = m_state->textures.Create(std::move(backbuffer));
		if (!m_backbuffer.Is_Valid())
			return false;
		m_depth_target = m_state->textures.Create(std::move(depth));
		if (!m_depth_target.Is_Valid()) {
			m_state->textures.Destroy(m_backbuffer);
			m_backbuffer = {};
			return false;
		}
	}

	m_width = resources.width;
	m_height = resources.height;
	return true;
}

bool DX11SwapChain::Resize(std::uint32_t width, std::uint32_t height)
{
	if (!Is_Valid() || m_state->frame_active || m_state->shared_frame || width == 0 || height == 0)
		return false;

	m_state->context.Get()->OMSetRenderTargets(0, nullptr, nullptr);
	m_state->textures.Destroy(m_backbuffer);
	m_state->textures.Destroy(m_depth_target);
	m_backbuffer = {};
	m_depth_target = {};
	m_width = 0;
	m_height = 0;
	if (FAILED(m_state->native_swap_chain.Get()->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
		return false;

	return Create_Targets(width, height);
}

bool DX11SwapChain::Present() noexcept
{
	if (!Is_Valid() || m_state->frame_active || !m_state->ready_to_present || m_state->presented)
		return false;

	if (FAILED(m_state->native_swap_chain.Get()->Present(0, 0)))
		return false;

	m_state->ready_to_present = false;
	m_state->presented = true;
	return true;
}

bool DX11CommandList::Is_Ready() const noexcept
{
	return m_state != nullptr && m_state->context.Get() != nullptr;
}

bool DX11CommandList::Is_Pipeline_Valid() const noexcept
{
	return Is_Ready() && m_pipeline.Is_Valid() && m_state->pipelines.Resolve(m_pipeline) != nullptr;
}

bool DX11CommandList::Bind_Pipeline(RHIPipelineHandle pipeline) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Pipeline *resource = m_state->pipelines.Resolve(pipeline);
	if (resource == nullptr || resource->vertex_shader.Get() == nullptr || resource->pixel_shader.Get() == nullptr || resource->input_layout.Get() == nullptr || resource->depth_stencil_state.Get() == nullptr || resource->blend_state.Get() == nullptr || resource->rasterizer_state.Get() == nullptr || resource->sampler_state.Get() == nullptr)
		return false;

	ID3D11DeviceContext *context = m_state->context.Get();
	context->IASetInputLayout(resource->input_layout.Get());
	context->VSSetShader(resource->vertex_shader.Get(), nullptr, 0);
	context->PSSetShader(resource->pixel_shader.Get(), nullptr, 0);
	ID3D11SamplerState *sampler_state = resource->sampler_state.Get();
	context->PSSetSamplers(0, 1, &sampler_state);
	context->OMSetDepthStencilState(resource->depth_stencil_state.Get(), 0);
	context->OMSetBlendState(resource->blend_state.Get(), nullptr, 0xffffffffu);
	context->RSSetState(resource->rasterizer_state.Get());
	context->IASetPrimitiveTopology(To_DX11_Topology(resource->topology));
	m_pipeline = pipeline;
	return true;
}

bool DX11CommandList::Bind_Texture_At_Slot(RHIShaderStage stage, std::uint32_t slot, RHITextureHandle texture) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Texture *resource = m_state->textures.Resolve(texture);
	if (resource == nullptr || resource->shader_resource_view.Get() == nullptr)
		return false;

	ID3D11ShaderResourceView *view = resource->shader_resource_view.Get();
	if (stage == RHIShaderStage::Vertex)
		m_state->context.Get()->VSSetShaderResources(slot, 1, &view);
	else
		m_state->context.Get()->PSSetShaderResources(slot, 1, &view);
	return true;
}

bool DX11CommandList::Bind_Buffer_At_Slot(RHIShaderStage stage, std::uint32_t slot, RHIBufferHandle buffer) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Buffer *resource = m_state->buffers.Resolve(buffer);
	if (resource == nullptr || resource->object.Get() == nullptr)
		return false;

	ID3D11DeviceContext *context = m_state->context.Get();
	if (resource->usage == RHIBufferUsage::Storage) {
		if (resource->shader_resource_view.Get() == nullptr)
			return false;
		ID3D11ShaderResourceView *view = resource->shader_resource_view.Get();
		if (stage == RHIShaderStage::Vertex)
			context->VSSetShaderResources(slot, 1, &view);
		else
			context->PSSetShaderResources(slot, 1, &view);
		return true;
	}

	if (resource->usage != RHIBufferUsage::Constant)
		return false;
	ID3D11Buffer *native_buffer = resource->object.Get();
	if (stage == RHIShaderStage::Vertex)
		context->VSSetConstantBuffers(slot, 1, &native_buffer);
	else
		context->PSSetConstantBuffers(slot, 1, &native_buffer);
	return true;
}

bool DX11CommandList::Set_Bindless_Resources(std::span<const RHIBindlessResource> resources) noexcept
{
	if (!Is_Ready())
		return false;

	m_bindless_resources = resources;
	std::uint32_t storage_buffer_slot = 127;
	for (const RHIBindlessResource &resource : resources) {
		switch (resource.type) {
		case RHIResourceType::Buffer:
			if (!Bind_Buffer_At_Slot(RHIShaderStage::Vertex, storage_buffer_slot, resource.buffer)
				|| !Bind_Buffer_At_Slot(RHIShaderStage::Fragment, storage_buffer_slot, resource.buffer))
				return false;
			if (storage_buffer_slot == 0)
				return false;
			--storage_buffer_slot;
			break;
		case RHIResourceType::Texture:
			if (resource.index.Get_Index() >= 128 || !Bind_Texture_At_Slot(RHIShaderStage::Fragment, resource.index.Get_Index(), resource.texture))
				return false;
			break;
		case RHIResourceType::Material:
			if (!Bind_Buffer_At_Slot(RHIShaderStage::Vertex, 0, resource.buffer)
				|| !Bind_Buffer_At_Slot(RHIShaderStage::Fragment, 0, resource.buffer))
				return false;
			break;
		case RHIResourceType::Sampler:
		case RHIResourceType::Invalid:
			break;
		}
	}
	return true;
}

bool DX11CommandList::Set_Render_Targets(RHITextureHandle color_target, RHITextureHandle depth_target) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Texture *color = m_state->textures.Resolve(color_target);
	DX11Texture *depth = m_state->textures.Resolve(depth_target);
	if (color == nullptr || color->render_target_view.Get() == nullptr || depth == nullptr || depth->depth_stencil_view.Get() == nullptr)
		return false;

	ID3D11RenderTargetView *color_view = color->render_target_view.Get();
	m_state->context.Get()->OMSetRenderTargets(1, &color_view, depth->depth_stencil_view.Get());
	m_color_target = color_target;
	m_depth_target = depth_target;
	return true;
}

bool DX11CommandList::Set_Color_Target(RHITextureHandle color_target) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Texture *color = m_state->textures.Resolve(color_target);
	if (color == nullptr || color->render_target_view.Get() == nullptr)
		return false;

	ID3D11RenderTargetView *color_view = color->render_target_view.Get();
	m_state->context.Get()->OMSetRenderTargets(1, &color_view, nullptr);
	m_color_target = color_target;
	m_depth_target = {};
	return true;
}

bool DX11CommandList::Set_Depth_Target(RHITextureHandle depth_target) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Texture *depth = m_state->textures.Resolve(depth_target);
	if (depth == nullptr || depth->depth_stencil_view.Get() == nullptr)
		return false;

	m_state->context.Get()->OMSetRenderTargets(0, nullptr, depth->depth_stencil_view.Get());
	m_color_target = {};
	m_depth_target = depth_target;
	return true;
}

bool DX11CommandList::Clear(const std::array<float, 4> &color, float depth) noexcept
{
	if (!Is_Ready() || !m_color_target.Is_Valid() || !m_depth_target.Is_Valid())
		return false;

	DX11Texture *color_target = m_state->textures.Resolve(m_color_target);
	DX11Texture *depth_target = m_state->textures.Resolve(m_depth_target);
	if (color_target == nullptr || color_target->render_target_view.Get() == nullptr || depth_target == nullptr || depth_target->depth_stencil_view.Get() == nullptr)
		return false;

	m_state->context.Get()->ClearRenderTargetView(color_target->render_target_view.Get(), color.data());
	m_state->context.Get()->ClearDepthStencilView(depth_target->depth_stencil_view.Get(), D3D11_CLEAR_DEPTH, depth, 0);
	return true;
}

bool DX11CommandList::Clear_Depth(float depth) noexcept
{
	if (!Is_Ready() || !m_depth_target.Is_Valid())
		return false;

	DX11Texture *depth_target = m_state->textures.Resolve(m_depth_target);
	if (depth_target == nullptr || depth_target->depth_stencil_view.Get() == nullptr)
		return false;

	m_state->context.Get()->ClearDepthStencilView(depth_target->depth_stencil_view.Get(), D3D11_CLEAR_DEPTH, depth, 0);
	return true;
}

bool DX11CommandList::Copy_Texture(RHITextureHandle source, RHITextureHandle destination) noexcept
{
	if (!Is_Ready() || !source.Is_Valid() || !destination.Is_Valid())
		return false;

	DX11Texture *source_texture = m_state->textures.Resolve(source);
	DX11Texture *destination_texture = m_state->textures.Resolve(destination);
	if (source_texture == nullptr || destination_texture == nullptr
		|| source_texture->object.Get() == nullptr || destination_texture->object.Get() == nullptr
		|| source_texture->width != destination_texture->width || source_texture->height != destination_texture->height
		|| source_texture->format != destination_texture->format)
		return false;

	m_state->context.Get()->CopyResource(destination_texture->object.Get(), source_texture->object.Get());
	return true;
}

bool DX11CommandList::Set_Viewport(RHIViewport viewport) noexcept
{
	if (!Is_Ready() || viewport.width == 0 || viewport.height == 0)
		return false;

	D3D11_VIEWPORT native_viewport{};
	native_viewport.TopLeftX = static_cast<float>(viewport.x);
	native_viewport.TopLeftY = static_cast<float>(viewport.y);
	native_viewport.Width = static_cast<float>(viewport.width);
	native_viewport.Height = static_cast<float>(viewport.height);
	native_viewport.MinDepth = viewport.min_depth;
	native_viewport.MaxDepth = viewport.max_depth;
	m_state->context.Get()->RSSetViewports(1, &native_viewport);
	return true;
}

bool DX11CommandList::Set_Vertex_Buffer(std::uint32_t slot, RHIBufferHandle buffer, std::uint32_t stride, std::uint32_t offset) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Buffer *resource = m_state->buffers.Resolve(buffer);
	if (resource == nullptr || resource->object.Get() == nullptr || stride == 0)
		return false;

	ID3D11Buffer *native_buffer = resource->object.Get();
	const UINT native_stride = stride;
	const UINT native_offset = offset;
	m_state->context.Get()->IASetVertexBuffers(slot, 1, &native_buffer, &native_stride, &native_offset);
	return true;
}

bool DX11CommandList::Set_Index_Buffer(RHIBufferHandle buffer, RHIIndexFormat format, std::uint32_t offset) noexcept
{
	if (!Is_Ready())
		return false;

	DX11Buffer *resource = m_state->buffers.Resolve(buffer);
	if (resource == nullptr || resource->object.Get() == nullptr)
		return false;

	const DXGI_FORMAT native_format = format == RHIIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	m_state->context.Get()->IASetIndexBuffer(resource->object.Get(), native_format, offset);
	return true;
}

bool DX11CommandList::Draw(std::uint32_t vertex_count, std::uint32_t first_vertex, std::uint32_t instance_count, std::uint32_t first_instance) noexcept
{
	if (!Is_Pipeline_Valid() || vertex_count == 0 || instance_count == 0)
		return false;

	if (instance_count == 1 && first_instance == 0)
		m_state->context.Get()->Draw(vertex_count, first_vertex);
	else
		m_state->context.Get()->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
	return true;
}

bool DX11CommandList::Draw_Indexed(std::uint32_t index_count, std::uint32_t first_index, std::int32_t base_vertex, std::uint32_t instance_count, std::uint32_t first_instance) noexcept
{
	if (!Is_Pipeline_Valid() || index_count == 0 || instance_count == 0)
		return false;

	if (instance_count == 1 && first_instance == 0)
		m_state->context.Get()->DrawIndexed(index_count, first_index, base_vertex);
	else
		m_state->context.Get()->DrawIndexedInstanced(index_count, instance_count, first_index, base_vertex, first_instance);
	return true;
}

void DX11CommandList::Reset_Frame_State() noexcept
{
	m_pipeline = {};
	m_color_target = {};
	m_depth_target = {};
	m_bindless_resources = {};
}

bool DX11CommandList::Reset_State() noexcept
{
	if (!Is_Ready())
		return false;

	Reset_Frame_State();
	return true;
}

DX11Device::DX11Device(DX11DeviceOptions options)
	: m_state(std::make_unique<DX11DeviceState>())
{
	m_state->shader_directory = options.shader_directory != nullptr ? options.shader_directory : "";
	m_state->vertex_shader_name = options.vertex_shader_name != nullptr ? options.vertex_shader_name : "";
	m_state->fragment_shader_name = options.fragment_shader_name != nullptr ? options.fragment_shader_name : "";
	if (!Create_DX11_Device(*m_state, options))
		return;

	if (options.shared_frame != nullptr) {
		m_state->shared_frame = true;
		if (!m_state->swap_chain.Adopt_Targets(*options.shared_frame))
			m_state->native_swap_chain.Reset();
	} else if (m_state->native_swap_chain.Get() != nullptr && !m_state->swap_chain.Create_Targets(options.width, options.height))
		m_state->native_swap_chain.Reset();
}

bool DX11Device::Is_Valid() const noexcept
{
	return m_state != nullptr && m_state->device.Get() != nullptr && m_state->context.Get() != nullptr;
}

RHIBufferHandle DX11Device::Create_Buffer(const RHIBuffer &description)
{
	return Create_Buffer_Initialized(description, {});
}

RHIBufferHandle DX11Device::Create_Buffer_Initialized(const RHIBuffer &description, std::span<const std::byte> initial_data)
{
	if (!Is_Valid() || description.byte_size == 0)
		return {};
	if (!initial_data.empty() && (initial_data.size() != description.byte_size || initial_data.size() > std::numeric_limits<std::uint32_t>::max()))
		return {};

	const std::uint32_t bind_flags = To_DX11_Bind_Flags(description.usage);
	if (bind_flags == 0)
		return {};
	if (description.usage == RHIBufferUsage::Storage && (description.stride == 0 || description.byte_size % description.stride != 0))
		return {};

	D3D11_BUFFER_DESC native_description{};
	if (description.usage == RHIBufferUsage::Constant && description.byte_size > std::numeric_limits<std::uint32_t>::max() - 15u)
		return {};
	native_description.ByteWidth = description.usage == RHIBufferUsage::Constant
		? (description.byte_size + 15u) & ~15u
		: description.byte_size;
	native_description.Usage = D3D11_USAGE_DEFAULT;
	native_description.BindFlags = bind_flags;
	if (description.usage == RHIBufferUsage::Storage) {
		native_description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		native_description.StructureByteStride = description.stride;
	}
	DX11Buffer resource;
	resource.usage = description.usage;
	resource.byte_size = native_description.ByteWidth;
	ID3D11Buffer *native_buffer = nullptr;
	D3D11_SUBRESOURCE_DATA native_data{};
	native_data.pSysMem = initial_data.data();
	if (FAILED(m_state->device.Get()->CreateBuffer(&native_description, initial_data.empty() ? nullptr : &native_data, &native_buffer)))
		return {};
	resource.object.Reset(native_buffer);
	if (description.usage == RHIBufferUsage::Storage) {
		D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
		view_description.Format = DXGI_FORMAT_UNKNOWN;
		view_description.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		view_description.BufferEx.FirstElement = 0;
		view_description.BufferEx.NumElements = description.byte_size / description.stride;
		ID3D11ShaderResourceView *native_view = nullptr;
		if (FAILED(m_state->device.Get()->CreateShaderResourceView(native_buffer, &view_description, &native_view)))
			return {};
		resource.shader_resource_view.Reset(native_view);
	}
	return m_state->buffers.Create(std::move(resource));
}

RHITextureHandle DX11Device::Create_Texture(const RHITexture &description)
{
	return Create_Texture_Initialized(description, {});
}

RHITextureHandle DX11Device::Create_Texture_Initialized(const RHITexture &description, const RHITextureUpload &initial_data)
{
	if (!Is_Valid() || description.width == 0 || description.height == 0 || description.depth != 1 || description.mip_count == 0)
		return {};

	const DXGI_FORMAT native_format = To_DX11_Format(description.format);
	const std::uint32_t bind_flags = To_DX11_Texture_Bind_Flags(description);
	const std::uint32_t bytes_per_pixel = To_DX11_Bytes_Per_Pixel(description.format);
	if (native_format == DXGI_FORMAT_UNKNOWN || bind_flags == 0 || bytes_per_pixel == 0)
		return {};
	if (!initial_data.data.empty() && description.mip_count != 1)
		return {};
	const std::uint64_t minimum_row_pitch = static_cast<std::uint64_t>(description.width) * bytes_per_pixel;
	const std::uint32_t row_pitch = initial_data.row_pitch == 0 ? static_cast<std::uint32_t>(minimum_row_pitch) : initial_data.row_pitch;
	if (!initial_data.data.empty() && (minimum_row_pitch > std::numeric_limits<std::uint32_t>::max() || row_pitch < minimum_row_pitch || initial_data.data.size() < static_cast<std::uint64_t>(row_pitch) * description.height))
		return {};
	const bool sampled_depth = description.format == RHITextureFormat::D32_Float
		&& Has_Texture_Usage(description, RHITextureUsage::ShaderResource)
		&& Has_Texture_Usage(description, RHITextureUsage::DepthStencil);

	D3D11_TEXTURE2D_DESC native_description{};
	native_description.Width = description.width;
	native_description.Height = description.height;
	native_description.MipLevels = description.mip_count;
	native_description.ArraySize = 1;
	native_description.Format = sampled_depth ? DXGI_FORMAT_R32_TYPELESS : native_format;
	native_description.SampleDesc.Count = 1;
	native_description.Usage = D3D11_USAGE_DEFAULT;
	native_description.BindFlags = bind_flags;

	DX11Texture resource;
	resource.width = description.width;
	resource.height = description.height;
	resource.format = description.format;
	ID3D11Texture2D *native_texture = nullptr;
	D3D11_SUBRESOURCE_DATA native_data{};
	native_data.pSysMem = initial_data.data.data();
	native_data.SysMemPitch = row_pitch;
	if (FAILED(m_state->device.Get()->CreateTexture2D(&native_description, initial_data.data.empty() ? nullptr : &native_data, &native_texture)))
		return {};
	resource.object.Reset(native_texture);

	if (Has_Texture_Usage(description, RHITextureUsage::ShaderResource)) {
		D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};
		const D3D11_SHADER_RESOURCE_VIEW_DESC *view_description_pointer = nullptr;
		if (sampled_depth) {
			view_description.Format = DXGI_FORMAT_R32_FLOAT;
			view_description.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view_description.Texture2D.MostDetailedMip = 0;
			view_description.Texture2D.MipLevels = description.mip_count;
			view_description_pointer = &view_description;
		}
		ID3D11ShaderResourceView *native_view = nullptr;
		if (FAILED(m_state->device.Get()->CreateShaderResourceView(native_texture, view_description_pointer, &native_view)))
			return {};
		resource.shader_resource_view.Reset(native_view);
	}
	if (Has_Texture_Usage(description, RHITextureUsage::RenderTarget)) {
		ID3D11RenderTargetView *native_view = nullptr;
		if (FAILED(m_state->device.Get()->CreateRenderTargetView(native_texture, nullptr, &native_view)))
			return {};
		resource.render_target_view.Reset(native_view);
	}
	if (Has_Texture_Usage(description, RHITextureUsage::DepthStencil)) {
		D3D11_DEPTH_STENCIL_VIEW_DESC view_description{};
		const D3D11_DEPTH_STENCIL_VIEW_DESC *view_description_pointer = nullptr;
		if (sampled_depth) {
			view_description.Format = DXGI_FORMAT_D32_FLOAT;
			view_description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			view_description.Texture2D.MipSlice = 0;
			view_description_pointer = &view_description;
		}
		ID3D11DepthStencilView *native_view = nullptr;
		if (FAILED(m_state->device.Get()->CreateDepthStencilView(native_texture, view_description_pointer, &native_view)))
			return {};
		resource.depth_stencil_view.Reset(native_view);
	}
	if (Has_Texture_Usage(description, RHITextureUsage::UnorderedAccess)) {
		ID3D11UnorderedAccessView *native_view = nullptr;
		if (FAILED(m_state->device.Get()->CreateUnorderedAccessView(native_texture, nullptr, &native_view)))
			return {};
		resource.unordered_access_view.Reset(native_view);
	}

	return m_state->textures.Create(std::move(resource));
}

bool DX11Device::Update_Buffer(RHIBufferHandle buffer, std::uint32_t offset, std::span<const std::byte> data) noexcept
{
	if (!Is_Valid() || data.empty() || data.size() > std::numeric_limits<std::uint32_t>::max())
		return false;

	DX11Buffer *resource = m_state->buffers.Resolve(buffer);
	if (resource == nullptr || resource->object.Get() == nullptr || offset > resource->byte_size || data.size() > resource->byte_size - offset)
		return false;

	D3D11_BOX destination_box{};
	destination_box.left = offset;
	destination_box.right = offset + static_cast<UINT>(data.size());
	destination_box.top = 0;
	destination_box.bottom = 1;
	destination_box.front = 0;
	destination_box.back = 1;
	m_state->context.Get()->UpdateSubresource(resource->object.Get(), 0, &destination_box, data.data(), 0, 0);
	return true;
}

bool DX11Device::Update_Texture(RHITextureHandle texture, const RHITextureUpload &data) noexcept
{
	if (!Is_Valid() || data.data.empty())
		return false;

	DX11Texture *resource = m_state->textures.Resolve(texture);
	if (resource == nullptr || resource->object.Get() == nullptr)
		return false;

	const std::uint32_t bytes_per_pixel = To_DX11_Bytes_Per_Pixel(resource->format);
	const std::uint64_t minimum_row_pitch = static_cast<std::uint64_t>(resource->width) * bytes_per_pixel;
	const std::uint32_t row_pitch = data.row_pitch == 0 ? static_cast<std::uint32_t>(minimum_row_pitch) : data.row_pitch;
	if (bytes_per_pixel == 0 || minimum_row_pitch > std::numeric_limits<std::uint32_t>::max() || row_pitch < minimum_row_pitch || data.data.size() < static_cast<std::uint64_t>(row_pitch) * resource->height)
		return false;

	m_state->context.Get()->UpdateSubresource(resource->object.Get(), 0, nullptr, data.data.data(), row_pitch, 0);
	return true;
}

bool DX11Device::Readback_Texture(RHITextureHandle texture, std::span<std::byte> data, std::uint32_t row_pitch) noexcept
{
	if (!Is_Valid() || data.empty() || row_pitch == 0)
		return false;

	DX11Texture *resource = m_state->textures.Resolve(texture);
	if (resource == nullptr || resource->object.Get() == nullptr || resource->format != RHITextureFormat::RGBA8_UNorm)
		return false;

	D3D11_TEXTURE2D_DESC source_description{};
	resource->object.Get()->GetDesc(&source_description);
	D3D11_TEXTURE2D_DESC staging_description = source_description;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	staging_description.MiscFlags = 0;

	ID3D11Texture2D *native_staging = nullptr;
	if (FAILED(m_state->device.Get()->CreateTexture2D(&staging_description, nullptr, &native_staging)))
		return false;

	DX11NativeObject<ID3D11Texture2D> staging;
	staging.Reset(native_staging);
	m_state->context.Get()->CopyResource(staging.Get(), resource->object.Get());

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(m_state->context.Get()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
		return false;

	const std::uint64_t required_size = static_cast<std::uint64_t>(row_pitch) * resource->height;
	const bool valid = row_pitch >= resource->width * 4u && required_size <= data.size();
	if (valid) {
		for (std::uint32_t row = 0; row < resource->height; ++row)
			std::memcpy(data.data() + static_cast<std::size_t>(row) * row_pitch, static_cast<const std::byte *>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, static_cast<std::size_t>(resource->width) * 4u);
	}

	m_state->context.Get()->Unmap(staging.Get(), 0);
	return valid;
}

RHIPipelineHandle DX11Device::Create_Pipeline(const RHIPipeline &description)
{
	if (!Is_Valid())
		return {};

	std::vector<std::byte> vertex_shader;
	std::vector<std::byte> pixel_shader;
	if (!Load_Shader_Binary(m_state->shader_directory, m_state->vertex_shader_name.c_str(), vertex_shader)
		|| !Load_Shader_Binary(m_state->shader_directory, m_state->fragment_shader_name.c_str(), pixel_shader))
		return {};

	return Create_Pipeline(description, {vertex_shader}, {pixel_shader});
}

RHIPipelineHandle DX11Device::Create_Pipeline(const RHIPipeline &description, RHIShaderBytecode vertex_shader, RHIShaderBytecode fragment_shader)
{
	if (!Is_Valid())
		return {};

	DX11Pipeline pipeline;
	if (!Create_DX11_Pipeline(m_state->device.Get(), description, vertex_shader.data, fragment_shader.data, pipeline))
		return {};

	return m_state->pipelines.Create(std::move(pipeline));
}

bool DX11Device::Destroy_Buffer(RHIBufferHandle buffer) noexcept
{
	return m_state != nullptr && m_state->buffers.Destroy(buffer);
}

bool DX11Device::Destroy_Texture(RHITextureHandle texture) noexcept
{
	return m_state != nullptr && m_state->textures.Destroy(texture);
}

bool DX11Device::Destroy_Pipeline(RHIPipelineHandle pipeline) noexcept
{
	return m_state != nullptr && m_state->pipelines.Destroy(pipeline);
}

CommandList &DX11Device::Immediate_Command_List() noexcept
{
	return m_state->command_list;
}

SwapChain &DX11Device::Get_Swap_Chain() noexcept
{
	return m_state->swap_chain;
}

bool DX11Device::Adopt_Shared_Frame(const DX11SharedFrameResources &resources)
{
	if (!Is_Valid() || !m_state->shared_frame || resources.device != m_state->device.Get() || resources.context != m_state->context.Get() || resources.swap_chain != m_state->native_swap_chain.Get() || m_state->frame_active)
		return false;

	return m_state->swap_chain.Adopt_Targets(resources);
}

bool DX11Device::Begin_Frame() noexcept
{
	if (!Is_Valid() || m_state->frame_active || !m_state->swap_chain.Is_Valid())
		return false;

	m_state->command_list.Reset_Frame_State();
	const RHIBackbuffer backbuffer = m_state->swap_chain.Backbuffer();
	const RHIDepthTarget depth_target = m_state->swap_chain.Depth_Target();
	if (!m_state->command_list.Set_Render_Targets(backbuffer.texture, depth_target.texture))
		return false;

	m_state->frame_active = true;
	m_state->ready_to_present = false;
	m_state->presented = false;
	return true;
}

bool DX11Device::End_Frame() noexcept
{
	if (!Is_Valid() || !m_state->frame_active)
		return false;

	m_state->context.Get()->OMSetRenderTargets(0, nullptr, nullptr);
	m_state->frame_active = false;
	m_state->ready_to_present = true;
	m_state->presented = false;
	return true;
}

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

export extern "C" bool Graphics_DX11_Begin_Modern_Phase() noexcept
{
	return g_shared_frame_device != nullptr && g_frame_owner.Begin_Modern_Phase(*g_shared_frame_device);
}

export bool Register_Modern_Phase_Executor(ModernPhaseInitializer initializer, ModernPhaseExecutor executor)
{
	if (g_shared_frame_device == nullptr || g_frame_owner.Phase() != FrameOwnerPhase::Idle)
		return false;
	if (initializer != nullptr && !initializer(*g_shared_frame_device))
		return false;

	return g_frame_owner.Set_Modern_Phase_Executor(executor);
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

	g_frame_owner.Set_Modern_Phase_Executor(nullptr);
	g_shared_frame_device.reset();
}

}
