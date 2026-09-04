/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DStatusCircle.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"

#include <algorithm>
#include <stdlib.h>
#include <WW3D2/AssetMgr.h>
#include <WW3D2/Texture.h>
#include <WWMath/tri.h>
#include <WWMath/colmath.h>
#include <WW3D2/ColTest.h>
#include <WW3D2/RInfo.h>
#include <WW3D2/Camera.h>
#include "WW3D2/WW3D.h"
#include "WW3D2/VertexFormat.h"
#include "WW3D2/Shader.h"
#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"

import Graphics.Scene.Ring;
import Graphics.Scene.Screen.FullscreenOverlay;

#define SC_DETAIL_BLEND ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_ENABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ZERO, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_SCALE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, primary gradient, alpha blending
#define SC_ALPHA_Z ( SHADE_CNST(ShaderClass::PASS_LEQUAL, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE, ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

// Texturing, no zbuffer, disabled zbuffer write, no gradient, add src to dest.
#define SC_ADD ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_ONE, \
	ShaderClass::DSTBLEND_ONE, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_DISABLE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_ENABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )



static ShaderClass detailOpaqueShader(SC_ALPHA);
Bool W3DStatusCircle::m_needUpdate;
Int W3DStatusCircle::m_diffuse=255; // blue.

W3DStatusCircle::~W3DStatusCircle()
{
	freeMapResources();
}

W3DStatusCircle::W3DStatusCircle()
{
	m_indexBuffer=nullptr;
	m_vertexMaterialClass=nullptr;
	m_vertexBufferCircle=nullptr;
	m_vertexBufferScreen=nullptr;
}


bool W3DStatusCircle::Cast_Ray(RayCollisionTestClass & raytest)
{

	return false;

}


//@todo: MW Handle both of these properly!!
W3DStatusCircle::W3DStatusCircle(const W3DStatusCircle & src)
{
	*this = src;
}

W3DStatusCircle & W3DStatusCircle::operator = (const W3DStatusCircle & that)
{
	assert(false);
	return *this;
}

void W3DStatusCircle::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	Vector3	ObjSpaceCenter((float)1000*0.5f,(float)1000*0.5f,(float)0);
	float length = ObjSpaceCenter.Length();

	sphere.Init(ObjSpaceCenter, length);
}

void W3DStatusCircle::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	Vector3	minPt(0,0,0);
	Vector3	maxPt((float)1000,(float)1000,(float)1000);
	box.Init(minPt,maxPt);
}

Int W3DStatusCircle::Class_ID() const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * W3DStatusCircle::Clone() const
{
	return NEW W3DStatusCircle(*this);
}


Int W3DStatusCircle::freeMapResources()
{
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	RenderBackend_Release_Index_Buffer(backend, m_indexBuffer);
	RenderBackend_Release_Vertex_Buffer(backend, m_vertexBufferScreen);
	RenderBackend_Release_Vertex_Buffer(backend, m_vertexBufferCircle);
	REF_PTR_RELEASE(m_vertexMaterialClass);
	return 0;
}

#define NUM_TRI 20
//Allocate a heightmap of x by y vertices.
//data must be an array matching this size.
Int W3DStatusCircle::initData()
{
	Int i;

	m_needUpdate = true;
	freeMapResources();	//free old data and ib/vb

	m_numTriangles = NUM_TRI;
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr) {
		return -1;
	}
	m_indexBuffer=backend->Create_Index_Buffer(
		static_cast<unsigned>(m_numTriangles * 3) * sizeof(UnsignedShort), false);

	// Fill up the IB
	RenderBackendIndexBufferLock lockIdxBuffer(backend, m_indexBuffer, 0, 0,
		RenderBackendBufferLockMode::Normal);
	if (!lockIdxBuffer.Is_Locked()) {
		return -1;
	}
	UnsignedShort *ib=(UnsignedShort*)lockIdxBuffer.Get_Data();

	for (i=0; i<3*m_numTriangles; i+=3)
	{
		ib[0]=i;
		ib[1]=i+1;
		ib[2]=i+2;

		ib+=3;	//skip the 3 indices we just filled
	}

	m_vertexBufferCircle=backend->Create_Vertex_Buffer(
		static_cast<unsigned>(m_numTriangles * 3) * sizeof(VertexFormatXYZDUV1),
		RenderBackendVertexFormat::PositionDiffuseTexture, false);
	m_vertexBufferScreen=backend->Create_Vertex_Buffer(
		static_cast<unsigned>(2 * 3) * sizeof(VertexFormatXYZDUV1),
		RenderBackendVertexFormat::PositionDiffuseTexture, false);

	//go with a preset material for now.
	m_vertexMaterialClass=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);

	m_shaderClass = ShaderClass(SC_ALPHA);// _PresetOpaque2DShader;//; //_PresetOpaqueShader;


	return 0;
}


/** updateCircleVB puts a circle with a team color vertex buffer. */

Int W3DStatusCircle::updateCircleVB()
{
	Int i, k;
	Real shade;
	RenderBackendVertexBuffer	*pVB = m_vertexBufferCircle;
	if (m_vertexBufferCircle )
	{
		m_needUpdate = false;
		IRenderBackend *backend = WW3D::Get_Render_Backend();
		RenderBackendVertexBufferLock lockVtxBuffer(backend, pVB, 0, 0,
			RenderBackendBufferLockMode::Normal);
		if (!lockVtxBuffer.Is_Locked()) {
			return -1;
		}
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Data();

		const Real theZ = 0.0f;
		const Real theRadius = 0.02f;
		const Int theAlpha = 127;
	  Int diffuse = m_diffuse + (theAlpha<<24);	 // b g<<8 r<<16 a<<24.
		Int limit = m_numTriangles;
		float curAngle = 0;
		float deltaAngle = 2*PI/limit;
		for (i=0; i<limit; i++)
		{

			shade=0.7f*255.0f;
			for (k=0; k<3; k++) {
				vb->z=  theZ;
				if (k==0) {
					vb->x=	0;
					vb->y=	0;
				} else if (k==1) {
					Vector3 vec(theRadius,0,theZ);
					vec.Rotate_Z(curAngle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				} else if (k==2) {
					Real angle = curAngle+deltaAngle;
					if (i==limit-1) {
						angle = 0;
					}
					Vector3 vec(theRadius,0,theZ);
					vec.Rotate_Z(angle);
					vb->x=	vec.X;
					vb->y=	vec.Y;
				}
				vb->diffuse = diffuse;
				vb->u1=0;
				vb->v1=0;
				vb++;
			}
			curAngle += deltaAngle;

		}
		return 0; //success.
	}
	return -1;
}

/** updateCircleVB puts a circle with a team color vertex buffer. */

Int W3DStatusCircle::updateScreenVB(Int diffuse)
{
	RenderBackendVertexBuffer	*pVB = m_vertexBufferScreen;
	if (m_vertexBufferScreen )
	{
		m_needUpdate = false;
		IRenderBackend *backend = WW3D::Get_Render_Backend();
		RenderBackendVertexBufferLock lockVtxBuffer(backend, pVB, 0, 0,
			RenderBackendBufferLockMode::Normal);
		if (!lockVtxBuffer.Is_Locked()) {
			return -1;
		}
		VertexFormatXYZDUV1 *vb = (VertexFormatXYZDUV1*)lockVtxBuffer.Get_Data();

		vb->x =	-1;
		vb->y =	-1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;

		vb->x =	1;
		vb->y =	1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;

		vb->x =	-1;
		vb->y =	1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;

		vb->x =	-1;
		vb->y =	-1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;

		vb->x =	1;
		vb->y =	-1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;

		vb->x =	1;
		vb->y =	1;
		vb->z = 0;
		vb->diffuse = diffuse;
		vb->u1=0;
		vb->v1=0;
		vb++;
		return 0; //success.
	}
	return -1;
}

bool W3DStatusCircle::renderModern()
{
	Graphics::RingRenderer &ring_renderer = Graphics::GetRingRenderer();
	Graphics::FullscreenOverlayRenderer &overlay_renderer = Graphics::GetFullscreenOverlayRenderer();
	if (!ring_renderer.Is_Initialized() || !overlay_renderer.Is_Initialized())
		return false;

	ring_renderer.Clear();
	overlay_renderer.Clear();
	if (TheGlobalData->m_showTeamDot) {
		const float red = static_cast<float>((m_diffuse >> 16) & 0xff) / 255.0f;
		const float green = static_cast<float>((m_diffuse >> 8) & 0xff) / 255.0f;
		const float blue = static_cast<float>(m_diffuse & 0xff) / 255.0f;
		const Graphics::RingDescription description{
			0.95f,
			0.67f,
			0.0f,
			0.0f,
			0.02f,
			{red, green, blue, 127.0f / 255.0f},
			20};
		if (!ring_renderer.Set_Ring(description))
			return false;
	}

	const ScriptEngine::TFade fade = TheScriptEngine->getFade();
	if (fade == ScriptEngine::FADE_NONE)
		return true;

	const float intensity = std::clamp(static_cast<float>(TheScriptEngine->getFadeValue()), 0.0f, 1.0f);
	Graphics::FullscreenOverlayDescription overlay;
	overlay.color = {intensity, intensity, intensity, 1.0f};
	switch (fade) {
	default:
	case ScriptEngine::FADE_ADD:
		overlay.blend_mode = Graphics::RHIBlendMode::Additive;
		overlay.blend_operation = Graphics::RHIBlendOperation::Add;
		overlay.draw_count = 1;
		break;
	case ScriptEngine::FADE_SUBTRACT:
		overlay.blend_mode = Graphics::RHIBlendMode::Additive;
		overlay.blend_operation = Graphics::RHIBlendOperation::ReverseSubtract;
		overlay.draw_count = 1;
		break;
	case ScriptEngine::FADE_SATURATE:
		overlay.blend_mode = Graphics::RHIBlendMode::ColorMultiply;
		overlay.blend_operation = Graphics::RHIBlendOperation::Add;
		overlay.draw_count = 2;
		break;
	case ScriptEngine::FADE_MULTIPLY:
		overlay.blend_mode = Graphics::RHIBlendMode::Multiply;
		overlay.blend_operation = Graphics::RHIBlendOperation::Add;
		overlay.draw_count = 1;
		break;
	}
	return overlay_renderer.Set_Overlay(overlay);
}

void W3DStatusCircle::Render(RenderInfoClass & rinfo)
{
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr) {
		return;
	}

	if (!TheGameLogic->isInGame() || TheGameLogic->getGameMode() == GAME_SHELL)
		return;

	// The legacy path below remains the per-object fallback until the modern
	// shared frame has been verified on the target backend.
	if (renderModern())
		return;

	if (m_indexBuffer == nullptr) {
		initData();
	}
	if (m_indexBuffer == nullptr) {
		return;
	}
	Bool setIndex = false;
	Matrix3D tm(true);
	if( TheGlobalData->m_showTeamDot )
	{
		if (m_needUpdate) {
			updateCircleVB();
		}
		//Apply the shader and material
		backend->Set_Material(m_vertexMaterialClass);
		backend->Set_Shader(m_shaderClass);
		backend->Set_Texture(0, nullptr);
		backend->Set_Index_Buffer(m_indexBuffer);
		backend->Set_Vertex_Buffer(m_vertexBufferCircle, 0, sizeof(VertexFormatXYZDUV1));
		backend->Set_Vertex_Format(RenderBackendVertexFormat::PositionDiffuseTexture);
		setIndex = true;

		Vector3 vec(0.95f, 0.67f, 0);
		Matrix3x3 rot(true);

		tm.Set_Translation(vec);

		backend->Set_Transform(RenderBackendTransform::World,tm);
		backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
			0, 0, m_numTriangles * 3, 0, NUM_TRI);
	}


	ScriptEngine::TFade fade = TheScriptEngine->getFade();
	if (fade == ScriptEngine::FADE_NONE) {
		return;
	}

	if (!setIndex) {
		backend->Set_Material(m_vertexMaterialClass);
		backend->Set_Index_Buffer(m_indexBuffer);
		backend->Set_Texture(0, nullptr);
	}

	tm.Make_Identity();
	Real intensity = TheScriptEngine->getFadeValue();
	Int clr = 255*intensity;
	Int diffuse = (0xff<<24)|(clr<<16)|(clr<<8)|clr;	 // b g<<8 r<<16 a<<24.
	updateScreenVB(diffuse);
	backend->Set_Transform(RenderBackendTransform::World,tm);
	backend->Set_Shader(ShaderClass(SC_ADD));
	backend->Set_Vertex_Buffer(m_vertexBufferScreen, 0, sizeof(VertexFormatXYZDUV1));
	backend->Set_Vertex_Format(RenderBackendVertexFormat::PositionDiffuseTexture);
	backend->Apply_Render_State_Changes();
	switch (fade) {
		default:
		case ScriptEngine::FADE_ADD:
			backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
				0, 0, 2 * 3, 0, 2);
			break;
		case ScriptEngine::FADE_SUBTRACT:
			WW3D::Get_Render_Backend()->Set_Blend_Operation(RenderBackendBlendOperation::ReverseSubtract);
			backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
				0, 0, 2 * 3, 0, 2);
			WW3D::Get_Render_Backend()->Set_Blend_Operation(RenderBackendBlendOperation::Add);
			break;
		case ScriptEngine::FADE_SATURATE:
			// 4x multiply
			WW3D::Get_Render_Backend()->Set_Blend_Factors(RenderBackendBlendFactor::DestinationColor,
				RenderBackendBlendFactor::SourceColor);
			backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
				0, 0, 2 * 3, 0, 2);
			backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
				0, 0, 2 * 3, 0, 2);
			break;
		case ScriptEngine::FADE_MULTIPLY:
			// Straight multiply
			WW3D::Get_Render_Backend()->Set_Blend_Factors(RenderBackendBlendFactor::Zero,
				RenderBackendBlendFactor::SourceColor);
			backend->Draw_Indexed_Primitives(RenderBackendPrimitiveType::TriangleList,
				0, 0, 2 * 3, 0, 2);
			break;
	}
	ShaderClass::Invalidate();
}
