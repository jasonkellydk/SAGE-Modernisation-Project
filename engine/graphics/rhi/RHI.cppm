module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

export module Graphics.RHI;

export import Graphics.Resources.Handles.ResourceHandle;

namespace Graphics
{

export struct RHIBufferHandleTag
{
};

export using RHIBufferHandle = ResourceHandle<RHIBufferHandleTag>;

export struct RHITextureHandleTag
{
};

export using RHITextureHandle = ResourceHandle<RHITextureHandleTag>;

export using RHIPipelineHandle = PipelineHandle;

export enum class RHIShaderStage : std::uint8_t
{
	Vertex,
	Fragment
};

export enum class RHIBufferUsage : std::uint8_t
{
	Vertex,
	Index,
	Constant,
	Storage
};

export struct RHIBuffer final
{
	std::uint32_t byte_size = 0;
	RHIBufferUsage usage = RHIBufferUsage::Vertex;
	std::uint32_t stride = 0;
};

export enum class RHITextureFormat : std::uint8_t
{
	R8_UNorm,
	RG8_UNorm,
	RGBA8_UNorm,
	BGRA8_UNorm,
	RGBA16_Float,
	RGBA32_Float,
	R32_Float,
	D24_UNorm_S8,
	D32_Float
};

export enum class RHITextureUsage : std::uint32_t
{
	ShaderResource = 1u << 0,
	RenderTarget = 1u << 1,
	DepthStencil = 1u << 2,
	UnorderedAccess = 1u << 3
};

export struct RHITexture final
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t mip_count = 1;
	RHITextureFormat format = RHITextureFormat::RGBA8_UNorm;
	std::uint32_t usage = static_cast<std::uint32_t>(RHITextureUsage::ShaderResource);
	std::uint32_t depth = 1;
};

export struct RHITextureUpload final
{
	std::span<const std::byte> data{};
	std::uint32_t row_pitch = 0;
};

export enum class RHIPrimitiveTopology : std::uint8_t
{
	TriangleList,
	PointList
};

export enum class RHIVertexFormat : std::uint8_t
{
	Position3Color4UV2,
	Position3Color4UV2ResourceIndex,
	Position3Color4UV2Skinned
};

export enum class RHIBlendMode : std::uint8_t
{
	Disabled,
	Alpha,
	Additive,
	Multiply
};

export enum class RHICullMode : std::uint8_t
{
	Back,
	None
};

export struct RHIPipeline final
{
	std::uint64_t key = 0;
	bool depth_test = true;
	bool depth_write = true;
	RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
	RHIVertexFormat vertex_format = RHIVertexFormat::Position3Color4UV2;
	RHIBlendMode blend_mode = RHIBlendMode::Disabled;
	RHICullMode cull_mode = RHICullMode::Back;
};

export struct RHIShaderBytecode final
{
	std::span<const std::byte> data{};
};

export struct RHIViewport final
{
	std::uint32_t x = 0;
	std::uint32_t y = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	float min_depth = 0.0f;
	float max_depth = 1.0f;
};

export enum class RHIIndexFormat : std::uint8_t
{
	UInt16,
	UInt32
};

export enum class RHIResourceType : std::uint8_t
{
	Invalid,
	Buffer,
	Texture,
	Sampler,
	Material
};

export struct RHIBindlessResource final
{
	ResourceIndex index{};
	RHIResourceType type = RHIResourceType::Invalid;
	RHIBufferHandle buffer{};
	RHITextureHandle texture{};
};

export struct RHIBackbuffer final
{
	RHITextureHandle texture{};
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

export struct RHIDepthTarget final
{
	RHITextureHandle texture{};
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

export class SwapChain
{
public:
	virtual ~SwapChain() noexcept = default;

	virtual bool Is_Valid() const noexcept = 0;
	virtual RHIBackbuffer Backbuffer() const noexcept = 0;
	virtual RHIDepthTarget Depth_Target() const noexcept = 0;
	virtual bool Resize(std::uint32_t width, std::uint32_t height) = 0;
	virtual bool Present() noexcept = 0;
};

export class CommandList
{
public:
	virtual ~CommandList() noexcept = default;

	virtual bool Reset_State() noexcept
	{
		return true;
	}

	virtual bool Bind_Pipeline(RHIPipelineHandle pipeline) noexcept = 0;
	virtual bool Set_Bindless_Resources(std::span<const RHIBindlessResource> resources) noexcept = 0;
	virtual bool Set_Render_Targets(RHITextureHandle color_target, RHITextureHandle depth_target) noexcept = 0;
	virtual bool Set_Color_Target(RHITextureHandle) noexcept
	{
		return false;
	}
	virtual bool Set_Depth_Target(RHITextureHandle depth_target) noexcept = 0;
	virtual bool Clear(const std::array<float, 4> &color, float depth) noexcept = 0;
	virtual bool Clear_Depth(float depth) noexcept = 0;
	virtual bool Copy_Texture(RHITextureHandle source, RHITextureHandle destination) noexcept
	{
		(void)source;
		(void)destination;
		return false;
	}
	virtual bool Set_Viewport(RHIViewport viewport) noexcept = 0;
	virtual bool Set_Vertex_Buffer(std::uint32_t slot, RHIBufferHandle buffer, std::uint32_t stride, std::uint32_t offset) noexcept = 0;
	virtual bool Set_Index_Buffer(RHIBufferHandle buffer, RHIIndexFormat format, std::uint32_t offset) noexcept = 0;
	virtual bool Draw(std::uint32_t vertex_count, std::uint32_t first_vertex = 0, std::uint32_t instance_count = 1, std::uint32_t first_instance = 0) noexcept = 0;
	virtual bool Draw_Indexed(std::uint32_t index_count, std::uint32_t first_index = 0, std::int32_t base_vertex = 0, std::uint32_t instance_count = 1, std::uint32_t first_instance = 0) noexcept = 0;
};

export class Device
{
public:
	virtual ~Device() noexcept = default;

	virtual bool Is_Valid() const noexcept = 0;
	virtual RHIBufferHandle Create_Buffer(const RHIBuffer &description) = 0;
	virtual RHITextureHandle Create_Texture(const RHITexture &description) = 0;
	virtual RHIPipelineHandle Create_Pipeline(const RHIPipeline &description) = 0;
	virtual RHIPipelineHandle Create_Pipeline(const RHIPipeline &description, RHIShaderBytecode vertex_shader, RHIShaderBytecode fragment_shader)
	{
		return vertex_shader.data.empty() && fragment_shader.data.empty() ? Create_Pipeline(description) : RHIPipelineHandle{};
	}
	virtual RHIBufferHandle Create_Buffer_Initialized(const RHIBuffer &description, std::span<const std::byte> initial_data)
	{
		return initial_data.empty() ? Create_Buffer(description) : RHIBufferHandle{};
	}
	virtual RHITextureHandle Create_Texture_Initialized(const RHITexture &description, const RHITextureUpload &initial_data)
	{
		return initial_data.data.empty() ? Create_Texture(description) : RHITextureHandle{};
	}
	virtual bool Update_Buffer(RHIBufferHandle, std::uint32_t, std::span<const std::byte>) noexcept
	{
		return false;
	}
	virtual bool Update_Texture(RHITextureHandle, const RHITextureUpload &) noexcept
	{
		return false;
	}
	virtual bool Readback_Texture(RHITextureHandle, std::span<std::byte>, std::uint32_t) noexcept
	{
		return false;
	}
	virtual bool Destroy_Buffer(RHIBufferHandle buffer) noexcept = 0;
	virtual bool Destroy_Texture(RHITextureHandle texture) noexcept = 0;
	virtual bool Destroy_Pipeline(RHIPipelineHandle pipeline) noexcept = 0;
	virtual CommandList &Immediate_Command_List() noexcept = 0;
	virtual SwapChain &Get_Swap_Chain() noexcept = 0;
	virtual bool Begin_Frame() noexcept = 0;
	virtual bool End_Frame() noexcept = 0;
};

}
