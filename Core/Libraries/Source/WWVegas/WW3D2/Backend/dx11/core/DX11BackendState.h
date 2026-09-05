/*
** Private DX11 state storage and native helpers.
** This header is only included by the DX11 subsystem translation units.
*/
#pragma once

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Backend/dx11/resources/DX11Resources.h"
#include "BitmapHandler.h"
#include "IndexBuffer.h"
#include "Light.h"
#include "LightEnvironment.h"
#include "RDDesc.h"
#include "RenderState.h"
#include "Render2D.h"
#include "Shader.h"
#include "SurfaceClass.h"
#include "Texture.h"
#include "TextureLoader.h"
#include "VertMaterial.h"
#include "VertexBuffer.h"
#include "WW3DFormat.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"
#include "WWMath/vector3.h"
#include "WWMath/vector4.h"
#include "WWLib/WWFILE.h"
#include "WWLib/ffactory.h"
#include "WWLib/registry.h"

namespace dx11_backend
{
	constexpr unsigned kDefaultWidth = 640;
	constexpr unsigned kDefaultHeight = 480;
	constexpr unsigned kDefaultBits = 32;
	constexpr unsigned kMaxConstantRegisters = MAX_VERTEX_SHADER_CONSTANTS;
	constexpr char kRegistryDeviceName[] = "RenderDeviceName";
	constexpr char kRegistryWidth[] = "RenderDeviceWidth";
	constexpr char kRegistryHeight[] = "RenderDeviceHeight";
	constexpr char kRegistryDepth[] = "RenderDeviceDepth";
	constexpr char kRegistryWindowed[] = "RenderDeviceWindowed";
	constexpr char kRegistryTextureDepth[] = "RenderDeviceTextureDepth";

	template <typename T>
	inline void Release_Com(T *& object)
	{
		if (object != nullptr)
		{
			object->Release();
			object = nullptr;
		}
	}

	inline bool Create_Render_Target_Depth_Stencil(ID3D11Device *device, unsigned width,
		unsigned height, DX11Texture *texture)
	{
		D3D11_TEXTURE2D_DESC description = {};
		description.Width = width;
		description.Height = height;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (FAILED(device->CreateTexture2D(&description, nullptr,
			&texture->render_target_depth_resource)))
		{
			return false;
		}
		if (FAILED(device->CreateDepthStencilView(texture->render_target_depth_resource,
			nullptr, &texture->render_target_depth_stencil_view)))
		{
			return false;
		}
		return true;
	}

	inline bool Load_Precompiled_DXBC(const char *filename, std::vector<unsigned char> &bytecode)
	{
		bytecode.clear();
		if (filename == nullptr)
		{
			return false;
		}

		// Generated engine shaders are deployed as loose files next to the game
		// assets. Prefer the engine factory so archives and normal game paths keep
		// working, then fall back to the backend's loose-file deployment path.
		if (_TheFileFactory != nullptr)
		{
			file_auto_ptr file(_TheFileFactory, filename);
			if (file.get() != nullptr && file->Is_Available())
			{
				const int size = file->Size();
				if (size >= 4)
				{
					bytecode.resize(static_cast<std::size_t>(size));
					if (file->Read(bytecode.data(), size) == size &&
						std::memcmp(bytecode.data(), "DXBC", 4) == 0)
					{
						return true;
					}
					bytecode.clear();
				}
			}
		}

		std::ifstream loose_file(filename, std::ios::in | std::ios::binary | std::ios::ate);
		if (!loose_file.is_open())
		{
			return false;
		}
		const std::streamoff size = loose_file.tellg();
		if (size < 4 || size > static_cast<std::streamoff>(std::numeric_limits<int>::max()))
		{
			return false;
		}
		bytecode.resize(static_cast<std::size_t>(size));
		loose_file.seekg(0, std::ios::beg);
		loose_file.read(reinterpret_cast<char *>(bytecode.data()), size);
		if (!loose_file || std::memcmp(bytecode.data(), "DXBC", 4) != 0)
		{
			bytecode.clear();
			return false;
		}
		return true;
	}

	inline unsigned Mip_Level_Count(unsigned width, unsigned height, unsigned requested)
	{
		if (requested != 0)
		{
			return requested;
		}

		unsigned count = 1;
		while (width > 1 || height > 1)
		{
			width = std::max(1u, width / 2);
			height = std::max(1u, height / 2);
			++count;
		}
		return count;
	}

	inline unsigned Mip_Level_Count(unsigned width, unsigned height, unsigned depth, unsigned requested)
	{
		if (requested != 0)
		{
			return requested;
		}

		unsigned count = 1;
		while (width > 1 || height > 1 || depth > 1)
		{
			width = std::max(1u, width / 2);
			height = std::max(1u, height / 2);
			depth = std::max(1u, depth / 2);
			++count;
		}
		return count;
	}

	inline float Clamp_Float(float value, float minimum, float maximum)
	{
		return std::max(minimum, std::min(maximum, value));
	}

	inline Vector4 Color_From_Packed(unsigned color)
	{
		return Vector4(
			static_cast<float>((color >> 16) & 0xff) / 255.0f,
			static_cast<float>((color >> 8) & 0xff) / 255.0f,
			static_cast<float>(color & 0xff) / 255.0f,
			static_cast<float>((color >> 24) & 0xff) / 255.0f);
	}

	inline D3D11_COMPARISON_FUNC To_D3D_Compare(RenderBackendCompareFunction function)
	{
		return static_cast<D3D11_COMPARISON_FUNC>(static_cast<unsigned>(function) + 1u);
	}

	inline D3D11_STENCIL_OP To_D3D_Stencil(RenderBackendStencilOperation operation)
	{
		switch (operation)
		{
		case RenderBackendStencilOperation::Zero: return D3D11_STENCIL_OP_ZERO;
		case RenderBackendStencilOperation::Replace: return D3D11_STENCIL_OP_REPLACE;
		case RenderBackendStencilOperation::IncrementSaturate: return D3D11_STENCIL_OP_INCR_SAT;
		case RenderBackendStencilOperation::DecrementSaturate: return D3D11_STENCIL_OP_DECR_SAT;
		case RenderBackendStencilOperation::Invert: return D3D11_STENCIL_OP_INVERT;
		case RenderBackendStencilOperation::Increment: return D3D11_STENCIL_OP_INCR;
		case RenderBackendStencilOperation::Decrement: return D3D11_STENCIL_OP_DECR;
		case RenderBackendStencilOperation::Keep:
		default: return D3D11_STENCIL_OP_KEEP;
		}
	}

	inline D3D11_BLEND To_D3D_Blend(RenderBackendBlendFactor factor)
	{
		switch (factor)
		{
		case RenderBackendBlendFactor::Zero: return D3D11_BLEND_ZERO;
		case RenderBackendBlendFactor::One: return D3D11_BLEND_ONE;
		case RenderBackendBlendFactor::SourceColor: return D3D11_BLEND_SRC_COLOR;
		case RenderBackendBlendFactor::InverseSourceColor: return D3D11_BLEND_INV_SRC_COLOR;
		case RenderBackendBlendFactor::SourceAlpha: return D3D11_BLEND_SRC_ALPHA;
		case RenderBackendBlendFactor::InverseSourceAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
		case RenderBackendBlendFactor::DestinationAlpha: return D3D11_BLEND_DEST_ALPHA;
		case RenderBackendBlendFactor::InverseDestinationAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
		case RenderBackendBlendFactor::DestinationColor: return D3D11_BLEND_DEST_COLOR;
		case RenderBackendBlendFactor::InverseDestinationColor: return D3D11_BLEND_INV_DEST_COLOR;
		default: return D3D11_BLEND_ONE;
		}
	}

	inline D3D11_BLEND To_D3D_Alpha_Blend(RenderBackendBlendFactor factor)
	{
		// D3D11 rejects *_COLOR factors in SrcBlendAlpha/DestBlendAlpha.
		// The neutral blend API uses one factor pair for both channels; for
		// alpha, the corresponding alpha factor preserves the D3D9 semantics.
		switch (factor)
		{
		case RenderBackendBlendFactor::SourceColor:
			return D3D11_BLEND_SRC_ALPHA;
		case RenderBackendBlendFactor::InverseSourceColor:
			return D3D11_BLEND_INV_SRC_ALPHA;
		case RenderBackendBlendFactor::DestinationColor:
			return D3D11_BLEND_DEST_ALPHA;
		case RenderBackendBlendFactor::InverseDestinationColor:
			return D3D11_BLEND_INV_DEST_ALPHA;
		default:
			return To_D3D_Blend(factor);
		}
	}

	inline D3D11_BLEND_OP To_D3D_Blend_Operation(RenderBackendBlendOperation operation)
	{
		switch (operation)
		{
		case RenderBackendBlendOperation::Subtract: return D3D11_BLEND_OP_SUBTRACT;
		case RenderBackendBlendOperation::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
		case RenderBackendBlendOperation::Minimum: return D3D11_BLEND_OP_MIN;
		case RenderBackendBlendOperation::Maximum: return D3D11_BLEND_OP_MAX;
		case RenderBackendBlendOperation::Add:
		default: return D3D11_BLEND_OP_ADD;
		}
	}

	inline D3D11_PRIMITIVE_TOPOLOGY To_D3D_Topology(RenderBackendPrimitiveType type)
	{
		switch (type)
		{
		case RenderBackendPrimitiveType::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
		case RenderBackendPrimitiveType::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
		case RenderBackendPrimitiveType::LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
		case RenderBackendPrimitiveType::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case RenderBackendPrimitiveType::TriangleList:
		default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

	inline unsigned Primitive_Vertex_Count(RenderBackendPrimitiveType type, unsigned primitive_count)
	{
		switch (type)
		{
		case RenderBackendPrimitiveType::PointList: return primitive_count;
		case RenderBackendPrimitiveType::LineList: return primitive_count * 2u;
		case RenderBackendPrimitiveType::LineStrip: return primitive_count + 1u;
		case RenderBackendPrimitiveType::TriangleStrip: return primitive_count + 2u;
		case RenderBackendPrimitiveType::TriangleList:
		default: return primitive_count * 3u;
		}
	}

	inline unsigned Primitive_Index_Count(RenderBackendPrimitiveType type, unsigned primitive_count)
	{
		return Primitive_Vertex_Count(type, primitive_count);
	}

	D3D11_FILTER To_D3D_Filter(const struct DX11StageState & stage);

	inline const char *Semantic_Name(RenderBackendVertexInputSemantic semantic)
	{
		switch (semantic)
		{
		case RenderBackendVertexInputSemantic::Position: return "POSITION";
		case RenderBackendVertexInputSemantic::BlendWeight: return "BLENDWEIGHT";
		case RenderBackendVertexInputSemantic::BlendIndices: return "BLENDINDICES";
		case RenderBackendVertexInputSemantic::Normal: return "NORMAL";
		case RenderBackendVertexInputSemantic::PointSize: return "PSIZE";
		case RenderBackendVertexInputSemantic::Color: return "COLOR";
		case RenderBackendVertexInputSemantic::TextureCoordinate: return "TEXCOORD";
		default: return nullptr;
		}
	}

	inline const char *Input_Field_Name(RenderBackendVertexInputSemantic semantic, unsigned index)
	{
		switch (semantic)
		{
		case RenderBackendVertexInputSemantic::Position: return "position";
		case RenderBackendVertexInputSemantic::BlendWeight: return "blend_weight";
		case RenderBackendVertexInputSemantic::BlendIndices: return "blend_indices";
		case RenderBackendVertexInputSemantic::Normal: return "normal";
		case RenderBackendVertexInputSemantic::PointSize: return "point_size";
		case RenderBackendVertexInputSemantic::Color: return index == 0 ? "color0" : "color1";
		case RenderBackendVertexInputSemantic::TextureCoordinate:
		default:
			break;
		}
		return nullptr;
	}

	inline DXGI_FORMAT Vertex_Native_Format(RenderBackendVertexInputType type)
	{
		switch (type)
		{
		case RenderBackendVertexInputType::Float1: return DXGI_FORMAT_R32_FLOAT;
		case RenderBackendVertexInputType::Float2: return DXGI_FORMAT_R32G32_FLOAT;
		case RenderBackendVertexInputType::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
		case RenderBackendVertexInputType::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case RenderBackendVertexInputType::Color: return DXGI_FORMAT_B8G8R8A8_UNORM;
		default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
	}

	struct DX11VertexInput
	{
		RenderBackendVertexInputElement element{};
		std::string field;
	};

	struct DX11Float4Constant
	{
		float values[4] = {};
	};

	struct DX11UInt4Constant
	{
		unsigned values[4] = {};
	};

	struct DX11VertexConstantData
	{
		float world[16] = {};
		float view[16] = {};
		float projection[16] = {};
		float viewport[4] = {};
		DX11Float4Constant legacy[MAX_VERTEX_SHADER_CONSTANTS] = {};
	};

	struct DX11PixelConstantData
	{
		DX11Float4Constant material_diffuse;
		DX11Float4Constant material_ambient;
		DX11Float4Constant material_emissive;
		DX11Float4Constant material_specular;
		DX11Float4Constant fog_color;
		DX11Float4Constant texture_factor;
		DX11Float4Constant legacy[MAX_PIXEL_SHADER_CONSTANTS];
		DX11Float4Constant pixel_state0;
		DX11UInt4Constant pixel_state1;
		DX11UInt4Constant material_source_state;
		DX11UInt4Constant stage_enabled[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_color_operation[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_alpha_operation[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_coordinate_state[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_color_argument[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_alpha_argument[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_color_modifier[MAX_TEXTURE_STAGES];
		DX11UInt4Constant stage_alpha_modifier[MAX_TEXTURE_STAGES];
		DX11Float4Constant texture_transform[MAX_TEXTURE_STAGES][4];
		DX11Float4Constant bump_matrix[MAX_TEXTURE_STAGES];
		DX11Float4Constant bump_params[MAX_TEXTURE_STAGES];
		DX11Float4Constant material_power;
		DX11Float4Constant scene_ambient;
		DX11Float4Constant light_diffuse[4];
		DX11Float4Constant light_specular[4];
		DX11Float4Constant light_ambient[4];
		DX11Float4Constant light_position[4];
		DX11Float4Constant light_direction[4];
		DX11Float4Constant light_attenuation[4];
		DX11Float4Constant light_spot[4];
		DX11UInt4Constant light_enabled;
	};

	static_assert(sizeof(DX11VertexConstantData) == 16u * (13u + MAX_VERTEX_SHADER_CONSTANTS));
	static_assert(sizeof(DX11PixelConstantData) == 16u * 160u);

	inline std::vector<DX11VertexInput> Make_Vertex_Input(const RenderBackendVertexLayout & layout)
	{
		std::vector<DX11VertexInput> inputs;
		inputs.push_back({{0, 0, layout.transformed ? RenderBackendVertexInputType::Float4 : RenderBackendVertexInputType::Float3,
			RenderBackendVertexInputSemantic::Position, 0, 0}, "position"});
		unsigned offset = layout.transformed ? 16u : 12u;
		if (layout.has_blend)
		{
			inputs.push_back({{0, offset, RenderBackendVertexInputType::Float4,
				RenderBackendVertexInputSemantic::BlendWeight, 0, 1}, "blend_weight"});
			offset += 16;
		}
		if (layout.has_normal)
		{
			inputs.push_back({{0, offset, RenderBackendVertexInputType::Float3,
				RenderBackendVertexInputSemantic::Normal, 0, 2}, "normal"});
			offset += 12;
		}
		if (layout.has_diffuse)
		{
			inputs.push_back({{0, offset, RenderBackendVertexInputType::Color,
				RenderBackendVertexInputSemantic::Color, 0, 3}, "color0"});
			offset += 4;
		}
		if (layout.has_specular)
		{
			inputs.push_back({{0, offset, RenderBackendVertexInputType::Color,
				RenderBackendVertexInputSemantic::Color, 1, 4}, "color1"});
			offset += 4;
		}
		for (unsigned index = 0; index < layout.texture_count && index < MAX_TEXTURE_STAGES; ++index)
		{
			const unsigned dimension = std::max(1u, std::min(4u, layout.texture_dimensions[index]));
			const auto type = static_cast<RenderBackendVertexInputType>(
				static_cast<unsigned>(RenderBackendVertexInputType::Float1) + dimension - 1u);
			inputs.push_back({{0, offset, type,
				RenderBackendVertexInputSemantic::TextureCoordinate, index, 5u + index},
				"texcoord" + std::to_string(index)});
			offset += dimension * sizeof(float);
		}
		return inputs;
	}

	inline std::vector<DX11VertexInput> Make_Vertex_Input(const RenderBackendVertexShaderInputLayout & layout)
	{
		std::vector<DX11VertexInput> inputs;
		for (unsigned index = 0; index < layout.element_count; ++index)
		{
			const RenderBackendVertexInputElement & element = layout.elements[index];
			std::string field;
			if (element.semantic == RenderBackendVertexInputSemantic::TextureCoordinate)
			{
				field = "texcoord" + std::to_string(element.semantic_index);
			}
			else
			{
				field = Input_Field_Name(element.semantic, element.semantic_index);
			}
			if (!field.empty())
			{
				inputs.push_back({element, std::move(field)});
			}
		}
		return inputs;
	}

	inline bool Vertex_Layouts_Equal(const RenderBackendVertexLayout & left,
		const RenderBackendVertexLayout & right)
	{
		if (left.format != right.format || left.transformed != right.transformed ||
			left.has_blend != right.has_blend || left.has_normal != right.has_normal ||
			left.has_diffuse != right.has_diffuse || left.has_specular != right.has_specular ||
			left.texture_count != right.texture_count)
		{
			return false;
		}
		for (unsigned index = 0; index < RENDER_BACKEND_MAX_TEXTURE_COORDINATES; ++index)
		{
			if (left.texture_dimensions[index] != right.texture_dimensions[index])
			{
				return false;
			}
		}
		return true;
	}

	struct DX11VertexShader final
	{
		ID3D11VertexShader *shader = nullptr;
		std::vector<unsigned char> bytecode;
		std::vector<DX11VertexInput> inputs;
		bool explicit_layout = false;
		bool precompiled_default = false;
		RenderBackendVertexFormat default_format = RenderBackendVertexFormat::Unknown;

		~DX11VertexShader() { Release_Com(shader); }
	};

	struct DX11PixelShader final
	{
		ID3D11PixelShader *shader = nullptr;
		std::vector<unsigned char> bytecode;
		bool precompiled_default = false;
		RenderBackendTextureKind default_texture_kind = RenderBackendTextureKind::Texture2D;
		unsigned default_texture_stage = 0;

		~DX11PixelShader() { Release_Com(shader); }
	};

	inline bool Float_Sequence_Equal(const float *left, const float *right, unsigned count)
	{
		for (unsigned index = 0; index < count; ++index)
		{
			if (left[index] != right[index])
			{
				return false;
			}
		}
		return true;
	}

	inline bool Lights_Equal(const RenderBackendLight &left, const RenderBackendLight &right)
	{
		return left.type == right.type &&
			Float_Sequence_Equal(left.diffuse, right.diffuse, 4) &&
			Float_Sequence_Equal(left.specular, right.specular, 4) &&
			Float_Sequence_Equal(left.ambient, right.ambient, 4) &&
			Float_Sequence_Equal(left.position, right.position, 3) &&
			Float_Sequence_Equal(left.direction, right.direction, 3) &&
			left.range == right.range && left.falloff == right.falloff &&
			left.attenuation0 == right.attenuation0 &&
			left.attenuation1 == right.attenuation1 &&
			left.attenuation2 == right.attenuation2 &&
			left.theta == right.theta && left.phi == right.phi;
	}

	constexpr unsigned kDefaultPixelShaderVariantCount = 1 + MAX_TEXTURE_STAGES * 2;

	inline unsigned Default_Pixel_Shader_Variant(RenderBackendTextureKind kind, unsigned stage)
	{
		if (kind == RenderBackendTextureKind::Cube)
		{
			return 1u + std::min(stage, MAX_TEXTURE_STAGES - 1u);
		}
		if (kind == RenderBackendTextureKind::Volume)
		{
			return 1u + MAX_TEXTURE_STAGES + std::min(stage, MAX_TEXTURE_STAGES - 1u);
		}
		return 0;
	}

	struct DX11Font final : RenderBackendFont
	{
		int height = 0;
		int width = 0;
		bool bold = false;
		std::string face;
		HFONT glyph_font = nullptr;
		HDC glyph_dc = nullptr;
		HBITMAP glyph_bitmap = nullptr;
		HGDIOBJ previous_bitmap = nullptr;
		HGDIOBJ previous_font = nullptr;
		unsigned char *glyph_bitmap_bits = nullptr;
		int glyph_bitmap_width = 0;
		int glyph_bitmap_height = 0;
		int glyph_bitmap_pitch = 0;
		int glyph_height = 0;
		int glyph_ascent = 0;
		int glyph_overhang = 0;
		std::vector<unsigned char> glyph_pixels;

		~DX11Font() override
		{
			if (glyph_dc != nullptr)
			{
				if (previous_font != nullptr)
				{
					SelectObject(glyph_dc, previous_font);
				}
				if (previous_bitmap != nullptr)
				{
					SelectObject(glyph_dc, previous_bitmap);
				}
			}
			if (glyph_font != nullptr)
			{
				DeleteObject(glyph_font);
			}
			if (glyph_bitmap != nullptr)
			{
				DeleteObject(glyph_bitmap);
			}
			if (glyph_dc != nullptr)
			{
				DeleteDC(glyph_dc);
			}
		}

		bool Initialize_Glyph_Font(int requested_height, const char *face_name,
			bool requested_bold, int requested_width)
		{
			if (face_name == nullptr)
			{
				return false;
			}

			const int font_height = std::max(1, requested_height < 0 ?
				-requested_height : requested_height);
			glyph_bitmap_width = std::max(64, font_height * 4);
			glyph_bitmap_height = std::max(64, font_height * 2);
			glyph_bitmap_pitch = (glyph_bitmap_width * 3 + 3) & ~3;
			glyph_font = CreateFontA(
				-font_height,
				requested_width,
				0,
				0,
				requested_bold ? FW_BOLD : FW_NORMAL,
				FALSE,
				FALSE,
				FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				ANTIALIASED_QUALITY,
				VARIABLE_PITCH,
				face_name);
			if (glyph_font == nullptr)
			{
				return false;
			}

			glyph_dc = CreateCompatibleDC(nullptr);
			if (glyph_dc == nullptr)
			{
				return false;
			}

			BITMAPINFO bitmap_info = {};
			bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmap_info.bmiHeader.biWidth = glyph_bitmap_width;
			bitmap_info.bmiHeader.biHeight = -glyph_bitmap_height;
			bitmap_info.bmiHeader.biPlanes = 1;
			bitmap_info.bmiHeader.biBitCount = 24;
			bitmap_info.bmiHeader.biCompression = BI_RGB;
			glyph_bitmap = CreateDIBSection(glyph_dc, &bitmap_info, DIB_RGB_COLORS,
				reinterpret_cast<void **>(&glyph_bitmap_bits), nullptr, 0);
			if (glyph_bitmap == nullptr || glyph_bitmap_bits == nullptr)
			{
				return false;
			}

			previous_bitmap = SelectObject(glyph_dc, glyph_bitmap);
			previous_font = SelectObject(glyph_dc, glyph_font);
			SetBkColor(glyph_dc, RGB(0, 0, 0));
			SetTextColor(glyph_dc, RGB(255, 255, 255));

			TEXTMETRIC text_metric = {};
			if (!GetTextMetrics(glyph_dc, &text_metric))
			{
				return false;
			}
			glyph_height = text_metric.tmHeight;
			glyph_ascent = text_metric.tmAscent;
			glyph_overhang = text_metric.tmOverhang;
			return glyph_height > 0;
		}
	};

	struct DX11StageState
	{
		RenderBackendTextureOperation color_operation = RenderBackendTextureOperation::Disable;
		RenderBackendTextureOperation alpha_operation = RenderBackendTextureOperation::Disable;
		RenderBackendTextureArgument color_argument[3] = {
			RenderBackendTextureArgument::Current,
			RenderBackendTextureArgument::Current,
			RenderBackendTextureArgument::Current};
		RenderBackendTextureArgument alpha_argument[3] = {
			RenderBackendTextureArgument::Current,
			RenderBackendTextureArgument::Current,
			RenderBackendTextureArgument::Current};
		RenderBackendTextureArgumentModifiers color_modifiers[3] = {};
		RenderBackendTextureArgumentModifiers alpha_modifiers[3] = {};
		RenderBackendTextureCoordinateSource coordinate_source = RenderBackendTextureCoordinateSource::PassThrough;
		RenderBackendTextureTransformFlags transform_flags = RenderBackendTextureTransformFlags::Disabled;
		RenderBackendTextureAddressMode address_u = RenderBackendTextureAddressMode::Wrap;
		RenderBackendTextureAddressMode address_v = RenderBackendTextureAddressMode::Wrap;
		RenderBackendTextureFilter min_filter = RenderBackendTextureFilter::Linear;
		RenderBackendTextureFilter mag_filter = RenderBackendTextureFilter::Linear;
		RenderBackendTextureFilter mip_filter = RenderBackendTextureFilter::Linear;
		unsigned uv_array_index = 0;
		unsigned anisotropy = 1;
		float bump_matrix[6] = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f};
	};

	inline D3D11_FILTER To_D3D_Filter(const DX11StageState & stage)
	{
		if (stage.anisotropy > 1 || stage.min_filter == RenderBackendTextureFilter::Anisotropic ||
			stage.mag_filter == RenderBackendTextureFilter::Anisotropic)
		{
			return D3D11_FILTER_ANISOTROPIC;
		}
		if (stage.min_filter == RenderBackendTextureFilter::Point &&
			stage.mag_filter == RenderBackendTextureFilter::Point &&
			stage.mip_filter == RenderBackendTextureFilter::Point)
		{
			return D3D11_FILTER_MIN_MAG_MIP_POINT;
		}
		if (stage.min_filter == RenderBackendTextureFilter::Point &&
			stage.mag_filter == RenderBackendTextureFilter::Point)
		{
			return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
		}
		if (stage.min_filter == RenderBackendTextureFilter::Point)
		{
			return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
		}
		return stage.mip_filter == RenderBackendTextureFilter::Point ?
			D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	}

struct DX11BackendState
{
	bool initialized = false;
	bool lite = false;
	HWND window = nullptr;
	HCURSOR cursor = nullptr;
	bool cursor_visible = true;
	unsigned width = kDefaultWidth;
	unsigned height = kDefaultHeight;
	unsigned bits = kDefaultBits;
	bool windowed = true;
	RenderBackendFullscreenMode fullscreen_mode = RenderBackendFullscreenMode::Borderless;
	int swap_interval = 0;
	int texture_bitdepth = 16;
	RenderBackendMultisampleMode multisample_mode = RenderBackendMultisampleMode::None;
	bool scene_active = false;
	bool render_to_texture = false;
	TextureBaseClass *active_render_target = nullptr;
	ZTextureClass *active_depth_target = nullptr;
	bool triangle_draw_enabled = true;
	RenderBackendDeviceStatus device_status = RenderBackendDeviceStatus::Error;
	RenderBackendDebugSettings debug_settings;
	RenderBackendCleanupHook *cleanup_hook = nullptr;
	RenderDeviceDescClass device_desc;
	RenderBackendViewport viewport{};
	std::array<unsigned, 256> render_states{};
	std::array<std::array<unsigned, 64>, MAX_TEXTURE_STAGES> texture_stage_states{};
	std::array<DX11StageState, MAX_TEXTURE_STAGES> stages{};
	std::array<DX11Texture *, MAX_TEXTURE_STAGES> textures{};
	// One additional resource slot is reserved for explicit modern passes
	// whose material contract is wider than the legacy stage count. The ocean
	// material uses it for the readable opaque-scene depth snapshot.
	DX11Texture *programmable_texture = nullptr;
	// Some WW3D shader paths bind a resource directly after the ordinary
	// render-state texture has been applied. Keep those bindings separate so a
	// later draw-side state synchronization does not replace them with the
	// material texture.
	std::array<DX11Texture *, MAX_TEXTURE_STAGES> direct_texture_overrides{};
	std::array<bool, MAX_TEXTURE_STAGES> direct_texture_override_valid{};
	bool applying_render_state = false;
	bool programmable_pass_active = false;
	bool applying_render_state_buffers = false;
	bool direct_vertex_binding_override = false;
	bool direct_index_binding_override = false;
	std::array<RenderBackendTextureKind, MAX_TEXTURE_STAGES> texture_kinds{};
	std::vector<DX11TextureRegistration> registered_textures;
	std::array<RenderBackendLight, 4> lights{};
	std::array<bool, 4> light_enabled{};
	std::array<bool, 4> light_position_camera_space{};
	std::array<bool, 4> light_direction_camera_space{};
	RenderBackendMaterial material{};
	float ambient[3] = {};
	float fog_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float fog_start = 0.0f;
	float fog_end = 1.0f;
	bool fog_enabled = false;
	bool lighting_enabled = false;
	bool normalize_normals = false;
	bool specular_enabled = false;
	unsigned alpha_reference = 0;
	RenderBackendCompareFunction alpha_function = RenderBackendCompareFunction::Always;
	bool alpha_test_enabled = false;
	bool alpha_blend_enabled = false;
	RenderBackendBlendOperation blend_operation = RenderBackendBlendOperation::Add;
	RenderBackendBlendFactor source_blend = RenderBackendBlendFactor::One;
	RenderBackendBlendFactor destination_blend = RenderBackendBlendFactor::Zero;
	RenderBackendFillMode fill_mode = RenderBackendFillMode::Solid;
	RenderBackendColorWriteMask color_write_mask = RenderBackendColorWriteMask::All;
	bool depth_test_enabled = true;
	bool depth_write_enabled = true;
	RenderBackendCompareFunction depth_function = RenderBackendCompareFunction::LessEqual;
	RenderBackendCullMode cull_mode = RenderBackendCullMode::CounterClockwise;
	std::array<RenderBackendCullMode, 8> cull_mode_overrides{};
	unsigned cull_mode_override_count = 0;
	RenderBackendShadeMode shade_mode = RenderBackendShadeMode::Gouraud;
	bool stencil_enabled = false;
	RenderBackendCompareFunction stencil_function = RenderBackendCompareFunction::Always;
	unsigned stencil_reference = 0;
	unsigned stencil_read_mask = 0xff;
	unsigned stencil_write_mask = 0xff;
	RenderBackendStencilOperation stencil_z_fail = RenderBackendStencilOperation::Keep;
	RenderBackendStencilOperation stencil_fail = RenderBackendStencilOperation::Keep;
	RenderBackendStencilOperation stencil_pass = RenderBackendStencilOperation::Keep;
	unsigned depth_bias = 0;
	unsigned texture_factor = 0xffffffff;
	bool point_sprite_enabled = false;
	bool point_scale_enabled = false;
	float point_size = 1.0f;
	float point_size_min = 1.0f;
	float point_size_max = 64.0f;
	float point_scale[3] = {1.0f, 0.0f, 0.0f};
	float npatch_segments = 1.0f;
	RenderBackendMaterialSource ambient_source = RenderBackendMaterialSource::MaterialValue;
	RenderBackendMaterialSource diffuse_source = RenderBackendMaterialSource::MaterialValue;
	RenderBackendMaterialSource emissive_source = RenderBackendMaterialSource::MaterialValue;
	Matrix4x4 transforms[11];
	std::array<Vector4, MAX_VERTEX_SHADER_CONSTANTS> vertex_constants{};
	std::array<Vector4, MAX_PIXEL_SHADER_CONSTANTS> pixel_constants{};
	RenderStateStruct render_state;
	// The engine's render state is deferred.  Keep a non-owning snapshot of
	// the last state synchronized with the native context so a direct backend
	// draw (for example a shadow-volume batch) is not overwritten by a second
	// full state replay from Draw_Indexed_Primitives.
	bool applied_render_state_valid = false;
	unsigned applied_shader_bits = 0;
	VertexMaterialClass *applied_material = nullptr;
	std::array<TextureBaseClass *, MAX_TEXTURE_STAGES> applied_textures{};
	std::array<RenderBackendLight, 4> applied_lights{};
	std::array<bool, 4> applied_light_enable{};
	Matrix4x4 applied_world;
	Matrix4x4 applied_view;
	std::array<unsigned, MAX_VERTEX_STREAMS> applied_vertex_buffer_types{};
	std::array<VertexBufferClass *, MAX_VERTEX_STREAMS> applied_vertex_buffers{};
	unsigned applied_index_buffer_type = BUFFER_TYPE_INVALID;
	IndexBufferClass *applied_index_buffer = nullptr;
	unsigned short applied_vba_offset = 0;
	unsigned short applied_vba_count = 0;
	unsigned short applied_iba_offset = 0;
	unsigned short applied_index_base_offset = 0;
	ShaderClass current_shader;
	ShaderClass captured_shader;
	bool captured_state_valid = false;
	RenderBackendVertexLayout current_layout;
	std::array<DX11VertexBuffer *, MAX_VERTEX_STREAMS> vertex_buffers{};
	std::array<unsigned, MAX_VERTEX_STREAMS> vertex_offsets{};
	std::array<unsigned, MAX_VERTEX_STREAMS> vertex_strides{};
	DX11IndexBuffer *index_buffer = nullptr;
	unsigned index_offset = 0;
	unsigned base_vertex_offset = 0;
	DX11VertexShader *active_vertex_shader = nullptr;
	DX11PixelShader *active_pixel_shader = nullptr;
	const RenderBackendVertexShaderInputLayout *active_explicit_layout = nullptr;
	RenderBackendVertexShaderInputLayout explicit_layout_storage;
	bool has_explicit_layout = false;
	ID3D11InputLayout *input_layout = nullptr;
	bool input_layout_owned = false;
	std::array<ID3D11InputLayout *, 22> default_input_layouts{};
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	IDXGISwapChain *swap_chain = nullptr;
	ID3D11Texture2D *back_buffer = nullptr;
	ID3D11RenderTargetView *back_buffer_view = nullptr;
	ID3D11Texture2D *depth_buffer = nullptr;
	ID3D11DepthStencilView *depth_buffer_view = nullptr;
	// Backend-owned copy of the active scene depth. It is recreated when the
	// swap-chain dimensions change, while its opaque handle remains stable for
	// the water material wrapper.
	DX11Texture *scene_depth_texture = nullptr;
	ID3D11RenderTargetView *active_render_target_view = nullptr;
	ID3D11DepthStencilView *active_depth_stencil_view = nullptr;
	ID3D11Buffer *vertex_constant_buffer = nullptr;
	ID3D11Buffer *pixel_constant_buffer = nullptr;
	ID3D11BlendState *blend_state = nullptr;
	ID3D11DepthStencilState *depth_state = nullptr;
	ID3D11RasterizerState *rasterizer_state = nullptr;
	std::array<ID3D11SamplerState *, MAX_TEXTURE_STAGES> samplers{};
	D3D11_BLEND_DESC cached_blend_description = {};
	D3D11_DEPTH_STENCIL_DESC cached_depth_description = {};
	D3D11_RASTERIZER_DESC cached_raster_description = {};
	std::array<D3D11_SAMPLER_DESC, MAX_TEXTURE_STAGES> cached_sampler_descriptions{};
	bool blend_description_valid = false;
	bool depth_description_valid = false;
	bool raster_description_valid = false;
	std::array<bool, MAX_TEXTURE_STAGES> sampler_description_valid{};
	DX11VertexConstantData uploaded_vertex_constants{};
	DX11PixelConstantData uploaded_pixel_constants{};
	bool uploaded_vertex_constants_valid = false;
	bool uploaded_pixel_constants_valid = false;
	bool constant_buffers_bound = false;
	bool native_state_valid = false;
	bool native_state_dirty = true;
	bool constant_state_dirty = true;
	ID3D11BlendState *applied_blend_state = nullptr;
	ID3D11DepthStencilState *applied_depth_state = nullptr;
	ID3D11RasterizerState *applied_rasterizer_state = nullptr;
	UINT applied_stencil_reference = 0;
	std::array<ID3D11ShaderResourceView *, MAX_TEXTURE_STAGES> applied_shader_resources{};
	std::array<ID3D11SamplerState *, MAX_TEXTURE_STAGES> applied_samplers{};
	bool shader_bindings_valid = false;
	ID3D11InputLayout *bound_input_layout = nullptr;
	DX11VertexShader *bound_vertex_shader = nullptr;
	DX11PixelShader *bound_pixel_shader = nullptr;
	DX11VertexBuffer *immediate_vertex_buffer = nullptr;
	unsigned immediate_vertex_capacity_bytes = 0;
	std::vector<DX11VertexShader *> vertex_shaders;
	std::vector<DX11PixelShader *> pixel_shaders;
	std::array<DX11VertexShader *, 22> default_vertex_shaders{};
	std::array<DX11PixelShader *, kDefaultPixelShaderVariantCount> default_pixel_shaders{};

	void Mark_Native_State_Dirty()
	{
		native_state_dirty = true;
	}

	void Mark_Constant_State_Dirty()
	{
		constant_state_dirty = true;
	}

	void Mark_All_State_Dirty()
	{
		native_state_dirty = true;
		constant_state_dirty = true;
	}

	bool Create_Device();
	void Release_Device();
	bool Create_Render_Targets();
	void Release_Render_Targets();
	bool Create_Constant_Buffers();
	void Release_Pipeline_States();
	void Release_Registered_Textures();
	void Recreate_Registered_Textures();
	bool Create_Input_Layout(DX11VertexShader * shader,
		const std::vector<DX11VertexInput> & inputs,
		ID3D11InputLayout **result);
	bool Recreate_Compiled_Shaders();
	void Invalidate_Default_Pixel_Shader_Selection()
	{
		if (active_pixel_shader != nullptr && active_pixel_shader->precompiled_default)
		{
			active_pixel_shader = nullptr;
		}
	}
	void Upload_Constants();
	void Apply_D3D_States();
	void Release_Active_Input_Layout()
	{
		// The layout is validated against the active explicit shader signature.
		// Releasing it must invalidate the native shader binding cache as well;
		// allocator reuse can otherwise make a newly created layout look equal to
		// the one that belonged to the previous shader lifetime.
		shader_bindings_valid = false;
		if (input_layout_owned)
		{
			Release_Com(input_layout);
		}
		else
		{
			input_layout = nullptr;
		}
		input_layout_owned = false;
	}

	void Set_Current_Vertex_Layout(const RenderBackendVertexLayout & layout)
	{
		if (Vertex_Layouts_Equal(current_layout, layout))
		{
			return;
		}

		current_layout = layout;
		// An input layout is validated against the active vertex shader's input
		// signature and the currently bound stream declaration. Reusing it after
		// a WW3D vertex format change decodes normals, colors, and UVs at the
		// wrong offsets, which turns otherwise valid meshes into corrupted geometry.
		active_vertex_shader = nullptr;
		active_explicit_layout = nullptr;
		has_explicit_layout = false;
		Release_Active_Input_Layout();
	}

	DX11BackendState()
	{
		for (Matrix4x4 & transform : transforms)
		{
			transform.Make_Identity();
		}
		current_layout = RenderBackend_Vertex_Layout(RenderBackend_Dynamic_Vertex_Format);
		applied_world.Make_Identity();
		applied_view.Make_Identity();
		material.diffuse[0] = material.diffuse[1] = material.diffuse[2] = material.diffuse[3] = 1.0f;
		material.ambient[0] = material.ambient[1] = material.ambient[2] = material.ambient[3] = 1.0f;
		material.specular[3] = material.emissive[3] = 1.0f;
		material.power = 1.0f;
		viewport = {0, 0, width, height, 0.0f, 1.0f};
		for (auto & stage : texture_stage_states)
		{
			stage.fill(0);
		}
	}

	~DX11BackendState()
	{
		if (cursor != nullptr)
		{
			DestroyCursor(cursor);
			cursor = nullptr;
		}
		for (DX11VertexShader *shader : vertex_shaders) delete shader;
		for (DX11PixelShader *shader : pixel_shaders) delete shader;
		vertex_shaders.clear();
		pixel_shaders.clear();
		Release_Pipeline_States();
		delete immediate_vertex_buffer;
		immediate_vertex_buffer = nullptr;
		Release_Com(vertex_constant_buffer);
		Release_Com(pixel_constant_buffer);
		Release_Com(blend_state);
		Release_Com(depth_state);
		Release_Com(rasterizer_state);
		for (ID3D11SamplerState *& sampler : samplers) Release_Com(sampler);
		delete scene_depth_texture;
		scene_depth_texture = nullptr;
		Release_Com(back_buffer_view);
		Release_Com(back_buffer);
		Release_Com(depth_buffer_view);
		Release_Com(depth_buffer);
		Release_Com(swap_chain);
		Release_Com(context);
		Release_Com(device);
	}
};

inline bool Create_Constant_Buffer(ID3D11Device * device, unsigned size, ID3D11Buffer ** buffer)
{
	if (device == nullptr || buffer == nullptr)
	{
		return false;
	}
	D3D11_BUFFER_DESC description = {};
	 description.ByteWidth = (size + 15u) & ~15u;
	 description.Usage = D3D11_USAGE_DYNAMIC;
	 description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	 description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	 return SUCCEEDED(device->CreateBuffer(&description, nullptr, buffer));
}

} // namespace dx11_backend
