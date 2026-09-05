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
Int W3DStatusCircle::m_diffuse=255; // blue.

W3DStatusCircle::~W3DStatusCircle()
{
	freeMapResources();
}

W3DStatusCircle::W3DStatusCircle() {}

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
    Graphics::GetRingRenderer().Clear();
    Graphics::GetFullscreenOverlayRenderer().Clear();
    return 0;
}

bool W3DStatusCircle::queueGraphics()
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

void W3DStatusCircle::Render(RenderInfoClass &)
{
    if (!TheGameLogic->isInGame() || TheGameLogic->getGameMode() == GAME_SHELL) return;
    queueGraphics();
}
