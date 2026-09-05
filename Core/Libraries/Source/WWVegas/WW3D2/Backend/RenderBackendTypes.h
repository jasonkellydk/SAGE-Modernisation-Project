/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Shared render-backend vocabulary. This header is deliberately free of
// graphics API and platform types so every backend can use the same contract.

#pragma once

#include <stdint.h>

#include "WW3D2/Buffer.h"
#include "WW3D2/VertexFormat.h"
#include "WW3D2/WW3DFormat.h"

class LightEnvironmentClass;
class LightClass;
class Matrix3D;
class Matrix4x4;
class DynamicIBAccessClass;
class DynamicVBAccessClass;
class IndexBufferClass;
class ShaderClass;
class TextureClass;
class TextureBaseClass;
class ZTextureClass;
struct RenderStateStruct;
class VertexBufferClass;
class VertexMaterialClass;
class Vector3;
class RenderDeviceDescClass;
class SurfaceClass;
class Vector4;

// These limits are part of the WW3D contract, rather than a property of one
// graphics API.  Backends may support fewer active stages, but the state
// objects and callers use these stable upper bounds.
constexpr unsigned MAX_TEXTURE_STAGES = 8;
// Explicit modern materials may need one resource in addition to the eight
// legacy-compatible stages. Keep the legacy stage count and constant-buffer
// contract stable while allowing programmable passes to opt into this slot.
constexpr unsigned MAX_PROGRAMMABLE_TEXTURE_STAGES = MAX_TEXTURE_STAGES + 1;
constexpr unsigned MAX_VERTEX_STREAMS = 2;
constexpr unsigned MAX_VERTEX_SHADER_CONSTANTS = 96;
constexpr unsigned MAX_PIXEL_SHADER_CONSTANTS = 8;
constexpr unsigned MAX_SHADOW_MAPS = 1;
constexpr unsigned RENDER_BACKEND_MAX_VERTEX_INPUT_ELEMENTS = 16;

// A programmable vertex shader can use a different register layout from the
// fixed-function WW3D vertex format.  This describes that input contract
// without exposing a graphics API declaration or resource handle to callers.
enum class RenderBackendVertexInputType
{
	Float1,
	Float2,
	Float3,
	Float4,
	Color
};

enum class RenderBackendVertexInputSemantic
{
	Position,
	BlendWeight,
	BlendIndices,
	Normal,
	PointSize,
	Color,
	TextureCoordinate
};

struct RenderBackendVertexInputElement
{
	unsigned stream;
	unsigned offset;
	RenderBackendVertexInputType type;
	RenderBackendVertexInputSemantic semantic;
	unsigned semantic_index;
	unsigned shader_register;
};

struct RenderBackendVertexShaderInputLayout
{
	unsigned element_count;
	RenderBackendVertexInputElement elements[
		RENDER_BACKEND_MAX_VERTEX_INPUT_ELEMENTS];

	RenderBackendVertexShaderInputLayout() : element_count(0)
	{
	}

	bool Add(unsigned stream, unsigned offset,
		RenderBackendVertexInputType type,
		RenderBackendVertexInputSemantic semantic,
		unsigned semantic_index, unsigned shader_register)
	{
		if (element_count >= RENDER_BACKEND_MAX_VERTEX_INPUT_ELEMENTS)
		{
			return false;
		}

		elements[element_count++] = {
			stream, offset, type, semantic, semantic_index, shader_register};
		return true;
	}
};

enum class RenderBackendTransform
{
	World,
	View,
	Projection,
	Texture0,
	Texture1,
	Texture2,
	Texture3,
	Texture4,
	Texture5,
	Texture6,
	Texture7
};

// A backend may present a fullscreen application through an exclusive swap
// chain or through a borderless window-system surface. This is separate from
// the logical windowed state reported by Is_Windowed().
enum class RenderBackendFullscreenMode
{
	Exclusive,
	Borderless
};

inline RenderBackendTransform RenderBackend_Texture_Transform(unsigned stage)
{
	return static_cast<RenderBackendTransform>(
		static_cast<unsigned>(RenderBackendTransform::Texture0) + stage);
}

enum class RenderBackendFillMode
{
	Point,
	Wireframe,
	Solid
};

enum class RenderBackendCullMode
{
	None,
	Clockwise,
	CounterClockwise
};

enum class RenderBackendShadeMode
{
	Flat,
	Gouraud,
	Phong
};

enum class RenderBackendMaterialSource
{
	MaterialValue,
	Color1,
	Color2
};

enum class RenderBackendColorWriteMask : unsigned
{
	None = 0,
	Red = 1u,
	Green = 2u,
	Blue = 4u,
	Alpha = 8u,
	RGB = 7u,
	All = 15u
};

enum class RenderBackendBlendOperation
{
	Add,
	Subtract,
	ReverseSubtract,
	Minimum,
	Maximum
};

enum class RenderBackendBlendFactor
{
	Zero,
	One,
	SourceColor,
	InverseSourceColor,
	SourceAlpha,
	InverseSourceAlpha,
	DestinationAlpha,
	InverseDestinationAlpha,
	DestinationColor,
	InverseDestinationColor
};

enum class RenderBackendCompareFunction
{
	Never,
	Less,
	Equal,
	LessEqual,
	Greater,
	NotEqual,
	GreaterEqual,
	Always
};

enum class RenderBackendStencilOperation
{
	Keep,
	Zero,
	Replace,
	IncrementSaturate,
	DecrementSaturate,
	Invert,
	Increment,
	Decrement
};

// CPU state snapshot for adapters that submit through another renderer.
struct RenderBackendFogState
{
    bool enabled = false;
    float color[4] = {0,0,0,1};
    float start = 0;
    float end = 1;
};

struct RenderBackendStencilState
{
    bool enabled = false;
    unsigned reference = 0;
    unsigned read_mask = 255;
    unsigned write_mask = 255;
    RenderBackendCompareFunction comparison = RenderBackendCompareFunction::Always;
    RenderBackendStencilOperation fail = RenderBackendStencilOperation::Keep;
    RenderBackendStencilOperation depth_fail = RenderBackendStencilOperation::Keep;
    RenderBackendStencilOperation pass = RenderBackendStencilOperation::Keep;
};

enum class RenderBackendTextureOperation
{
	Disable,
	SelectArgument1,
	SelectArgument2,
	Modulate,
	AddSmooth,
	Add,
	Subtract,
	BlendTextureAlpha,
	BlendCurrentAlpha,
	AddSigned,
	AddSigned2X,
	Modulate2X,
	ModulateAlphaAddColor,
	BumpEnvironmentMap,
	BumpEnvironmentMapLuminance,
	DotProduct3,
	MultiplyAdd
};

enum class RenderBackendTextureComponent
{
	Color,
	Alpha
};

enum class RenderBackendTextureArgument
{
	Current,
	Diffuse,
	Texture,
	TextureFactor,
	CurrentAlpha,
	DiffuseAlpha,
	TextureAlpha,
	TextureFactorAlpha
};

enum class RenderBackendTextureArgumentModifiers : unsigned
{
	None = 0,
	Complement = 1u,
	AlphaReplicate = 2u
};

inline RenderBackendTextureArgumentModifiers operator | (
	RenderBackendTextureArgumentModifiers left,
	RenderBackendTextureArgumentModifiers right)
{
	return static_cast<RenderBackendTextureArgumentModifiers>(
		static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

enum class RenderBackendTextureCoordinateSource
{
	PassThrough,
	CameraSpacePosition,
	CameraSpaceNormal,
	CameraSpaceReflectionVector
};

enum class RenderBackendTextureTransformFlags
{
	Disabled,
	Count2,
	Count3,
	ProjectedCount3
};

enum class RenderBackendTextureAddressMode
{
	Wrap,
	Clamp
};

enum class RenderBackendTextureFilterType
{
	Minification,
	Magnification,
	MipMap
};

enum class RenderBackendTextureFilter
{
	None,
	Point,
	Linear,
	Anisotropic
};

enum class RenderBackendPrimitiveType
{
	PointList,
	LineList,
	LineStrip,
	TriangleList,
	TriangleStrip
};

enum class RenderBackendBufferLockMode
{
	Normal,
	Discard,
	NoOverwrite
};

enum class RenderBackendMultisampleMode
{
	None,
	Samples2,
	Samples4,
	Samples8
};

enum class RenderBackendDeviceStatus
{
	Ready,
	Lost,
	NeedsReset,
	Error
};

struct RenderBackendViewport
{
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
	float min_z;
	float max_z;
};

// A render pass may temporarily replace the current color/depth attachment.
// The state contains engine-owned texture objects only; it deliberately does
// not expose a native graphics resource or view.
struct RenderBackendRenderTargetState
{
	TextureBaseClass *color = nullptr;
	ZTextureClass *depth = nullptr;
};

struct RenderBackendAdapterInfo
{
	unsigned vendor_id = 0;
	unsigned device_id = 0;
	unsigned driver_version_high = 0;
	unsigned driver_version_low = 0;
};

struct RenderBackendTextureLimits
{
	unsigned max_width = 0;
	unsigned max_height = 0;
	unsigned max_volume_extent = 0;
	unsigned max_aspect_ratio = 0;
};

// Resource handles are deliberately opaque to WW3D2.  The active backend owns
// the object represented by a handle and is responsible for its lifetime.
using RenderBackendResourceHandle = uintptr_t;
using RenderBackendTextureHandle = RenderBackendResourceHandle;

enum class RenderBackendTexturePool
{
	Default,
	Managed,
	SystemMemory
};

enum class RenderBackendTextureKind
{
	Texture2D,
	DepthStencil,
	Cube,
	Volume
};

struct RenderBackendTextureDescription
{
	RenderBackendTextureKind kind = RenderBackendTextureKind::Texture2D;
	WW3DFormat format = WW3D_FORMAT_UNKNOWN;
	WW3DZFormat depth_format = WW3D_ZFORMAT_UNKNOWN;
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int depth = 1;
	unsigned int mip_levels = 0;
};

struct RenderBackendTextureLock
{
	void * bits = nullptr;
	unsigned int row_pitch = 0;
	unsigned int slice_pitch = 0;
};

enum class RenderBackendCubeFace
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY,
	PositiveZ,
	NegativeZ
};

struct RenderBackendMaterial
{
	float diffuse[4];
	float ambient[4];
	float specular[4];
	float emissive[4];
	float power;
};

enum class RenderBackendLightType
{
	Unknown,
	Point,
	Spot,
	Directional
};

struct RenderBackendLight
{
	RenderBackendLightType type = RenderBackendLightType::Unknown;
	float diffuse[4] = {};
	float specular[4] = {};
	float ambient[4] = {};
	float position[3] = {};
	float direction[3] = {};
	float range = 0.0f;
	float falloff = 0.0f;
	float attenuation0 = 0.0f;
	float attenuation1 = 0.0f;
	float attenuation2 = 0.0f;
	float theta = 0.0f;
	float phi = 0.0f;
};

class RenderBackendSurface
{
public:
	virtual ~RenderBackendSurface() {}
};

// Opaque per-mesh storage owned by the active renderer. WW3D2 keeps only this
// handle; the concrete backend decides how polygon batches are represented.
class RenderBackendMeshData
{
public:
	virtual ~RenderBackendMeshData() {}
};

class RenderBackendVertexBuffer
{
public:
	virtual ~RenderBackendVertexBuffer() {}
};

class RenderBackendIndexBuffer
{
public:
	virtual ~RenderBackendIndexBuffer() {}
};

class RenderBackendFont
{
public:
	virtual ~RenderBackendFont() {}
};

struct RenderBackendFontMetrics
{
	int height = 0;
	int ascent = 0;
	int overhang = 0;
};

struct RenderBackendFontGlyph
{
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int pitch = 0;
	const unsigned char *pixels = nullptr;
};

struct RenderBackendLockedSurface
{
	void * bits;
	unsigned int pitch;
};

struct RenderBackendSurfaceDescription
{
	WW3DFormat format = WW3D_FORMAT_UNKNOWN;
	unsigned int width = 0;
	unsigned int height = 0;
};

enum class RenderBackendSurfaceLockMode
{
	ReadWrite,
	ReadOnly
};

// Settings used by the game and tools while collecting extended render
// statistics. These belong to the active backend because they affect frame
// submission (for example the optional frame sleep), not just the caller
// displaying the statistics.
struct RenderBackendDebugSettings
{
	bool m_showingStats;
	bool m_disableTerrain;
	bool m_disableWater;
	bool m_disableObjects;
	bool m_disableOverhead;
	bool m_disableConsole;
	int m_debugLinesToShow;
	int m_sleepTime;

	RenderBackendDebugSettings() :
		m_showingStats(false),
		m_disableTerrain(false),
		m_disableWater(false),
		m_disableObjects(false),
		m_disableOverhead(false),
		m_disableConsole(false),
		m_debugLinesToShow(-1),
		m_sleepTime(0)
	{
	}
};

// Render resources outside WW3D (the WorldBuilder is the current user) can
// release and reacquire those resources around a device reset without seeing
// any backend-specific device type.
class RenderBackendCleanupHook
{
public:
	virtual ~RenderBackendCleanupHook() {}
	virtual void ReleaseResources() = 0;
	virtual void ReAcquireResources() = 0;
};

enum RenderBackendBrowserOption : unsigned
{
	RenderBackendBrowserOptionScrollbars = 0x0001,
	RenderBackendBrowserOption3DBorder = 0x0002
};

enum RenderBackendFontDrawFlag : unsigned
{
	RenderBackendFontDrawFlagLeft = 1u << 0,
	RenderBackendFontDrawFlagNoClip = 1u << 1,
	RenderBackendFontDrawFlagTop = 1u << 2,
	RenderBackendFontDrawFlagSingleLine = 1u << 3
};

struct RenderBackendRect
{
	int left;
	int top;
	int right;
	int bottom;
};

struct RenderBackendPoint
{
	int x;
	int y;
};
