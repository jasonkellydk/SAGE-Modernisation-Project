#ifdef RTS_ZEROHOUR
#include "W3DDevice/GameClient/W3DScreenFilterGraphics.h"
#endif
#include "WW3D2/WW3D.h"
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

// FILE: W3DShaderManager.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DShaderManager.cpp
//
// Created:   Mark Wilczynski, August 2001
//
// Desc:      Perform tests on currently selected WW3D/D3D device to determine
//			  which of our rendering features are supported.  The system allows
//			  setting up a few custom shaders that are selected based on video
//			  card features.
//
//			  To add a new shader to the system:
//			  0) Add your shader to the ShaderTypes enum
//			  1) Create shader using W3DShaderInterface
//			  2) Repeat step 1 for any alternate shaders
//			  3) Create list of alternate shaders sorted by order of preference.
//				 The first shader which passes hardware validation will be selected.
//			  4) Add list from step 3) to MasterShaderList[].
//
//-----------------------------------------------------------------------------

#include "WW3D2/AssetMgr.h"
#include "Lib/BaseType.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include <SDL3/SDL.h>
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "GameClient/View.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/Display.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "Common/GlobalData.h"
#include "Common/GameLOD.h"
#include "WW3D2/Backend/RenderBackend.h"
#include "WW3D2/VertMaterial.h"
#include "WW3D2/VertexFormat.h"
#include "WWLib/cpudetect.h"
#include "WWMath/matrix4.h"
#include <cstdint>
#include <vector>

namespace
{
Matrix4x4 Make_Scaling(float x, float y, float z)
{
	return Matrix4x4(
		x, 0.0f, 0.0f, 0.0f,
		0.0f, y, 0.0f, 0.0f,
		0.0f, 0.0f, z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

Matrix4x4 Make_Translation(float x, float y, float z)
{
	return Matrix4x4(
		1.0f, 0.0f, 0.0f, x,
		0.0f, 1.0f, 0.0f, y,
		0.0f, 0.0f, 1.0f, z,
		0.0f, 0.0f, 0.0f, 1.0f);
}

}

// Turn this on to turn off pixel shaders. jba[4/3/2003]
#define do_not_DISABLE_PIXEL_SHADERS 1

/** Interface definition for custom shaders we define in our app.  These shaders can perform more complex
	operations than those allowed in the WW3D2 shader system.
*/
class W3DShaderInterface
{
public:
	Int getNumPasses() {return m_numPasses;};	///<return number of passes needed for this shader
	virtual Int set(Int pass) {return TRUE;};		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset() {
		ShaderClass::Invalidate();
		WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);
		WW3D::Get_Render_Backend()->Set_Texture_Resource(1, nullptr);};
	virtual Int init() = 0;			///<perform any one time initialization and validation
	virtual Int shutdown() { return TRUE;};			///<release resources used by shader
protected:
	Int m_numPasses;						///<number of passes to complete shader
};

//this table will contain custom versions of each shader tuned for specific video card and user options.
static W3DFilterInterface *W3DFilters[FT_MAX];
static W3DShaderInterface *W3DShaders[W3DShaderManager::ST_MAX];
static Int W3DShadersPassCount[W3DShaderManager::ST_MAX];	//number of passes for each of the above shaders
TextureClass *W3DShaderManager::m_Textures[8];
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader;
FilterTypes W3DShaderManager::m_currentFilter=FT_NULL_FILTER; ///< Last filter that was set.
Int W3DShaderManager::m_currentShaderPass;
ChipsetType W3DShaderManager::m_currentChipset;
GraphicsVenderID W3DShaderManager::m_currentVendor;
std::int64_t W3DShaderManager::m_driverVersion;

Bool W3DShaderManager::m_renderingToTexture = false;
TextureClass *W3DShaderManager::m_renderTexture=nullptr;	///<texture into which rendering will be redirected.

static void DeletePixelShaderHandle(uintptr_t &handle)
{
	if (!handle) {
		return;
	}
	if (IRenderBackend *backend = WW3D::Get_Render_Backend()) {
		backend->Release_Pixel_Shader(handle);
	}
	handle = 0;
}

static void DeleteVertexShaderHandle(uintptr_t &handle)
{
	if (!handle) return;
	if (IRenderBackend *backend = WW3D::Get_Render_Backend())
		backend->Release_Vertex_Shader(handle);
	handle = 0;
}

static RenderBackendVertexShaderInputLayout MakeTerrainVertexShaderLayout()
{
	RenderBackendVertexShaderInputLayout layout;
	layout.Add(0, offsetof(VertexFormatXYZDUV2, x), RenderBackendVertexInputType::Float3,
		RenderBackendVertexInputSemantic::Position, 0, 0);
	layout.Add(0, offsetof(VertexFormatXYZDUV2, diffuse), RenderBackendVertexInputType::Color,
		RenderBackendVertexInputSemantic::Color, 0, 1);
	layout.Add(0, offsetof(VertexFormatXYZDUV2, u1), RenderBackendVertexInputType::Float2,
		RenderBackendVertexInputSemantic::TextureCoordinate, 0, 2);
	layout.Add(0, offsetof(VertexFormatXYZDUV2, u2), RenderBackendVertexInputType::Float2,
		RenderBackendVertexInputSemantic::TextureCoordinate, 1, 3);
	return layout;
}
/*===========================================================================================*/
/*=========      Screen Shaders	=============================================================*/
/*===========================================================================================*/

class ScreenDefaultFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
};

ScreenDefaultFilter screenDefaultFilter;

///Default filter that just renders screen to off-screen texture and then copies it the the screen.
///Useful because we added some full-time unit effects (microwave tank smudge) to Generals MD that need access
///to the background as a texture.  This filter makes that texture always available for these effects.
W3DFilterInterface *ScreenDefaultFilterList[]=
{
	&screenDefaultFilter,
	nullptr
};

Int ScreenDefaultFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return FALSE;
	}

	//Can render to texture, but we don't know if it can read and write to the same texture.
	//Since there is no D3D caps bit to tell you this, we will just hard-code some specific
	//cards that we know should work.

	Int res;

	if ((res=W3DShaderManager::getChipset()) != DC_UNKNOWN)
	{
		if ( res >=	DC_GEFORCE2)
		{
			//Check if their driver is newer than what we tested for this vendor
/*			if (TheGameLODManager)
			{
				if (TheGameLODManager->getTestedDriverVersion(W3DShaderManager::getCurrentVendor()) < W3DShaderManager::getCurrentDriverVersion())
					return FALSE;
			}*/
		}
	}

	W3DFilters[FT_VIEW_DEFAULT]=&screenDefaultFilter;

	return TRUE;
}

Bool ScreenDefaultFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	// TheSuperHackers @bugfix Disable Render To Texture redirection for the default filter
	// When MSAA is forced by Nvidia driver profile depth buffer is multisampled internally.
	// Rendering to non-MSAA texture with this depth buffer corrupts depth testing producing black screen
	// The smudge system has its own Copy path that works without Render To Texture.
	return FALSE;
}

Bool ScreenDefaultFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{

	TextureClass * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

#ifdef RTS_ZEROHOUR
    Draw_Screen_Filter_Quad(v,tex);
#else
	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif

	reset();
	return true;
}

Int ScreenDefaultFilter::set(FilterModes mode)
{
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	WW3D::Get_Render_Backend()->Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	WW3D::Get_Render_Backend()->Set_Shader(ShaderClass::_PresetOpaqueShader);
	WW3D::Get_Render_Backend()->Set_Texture(0,nullptr);
	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

	WW3D::Get_Render_Backend()->Set_Depth_Function(RenderBackendCompareFunction::Always);;
	WW3D::Get_Render_Backend()->Set_Depth_Write_Enabled(false);;
	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

	return true;
}

void ScreenDefaultFilter::reset()
{
	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);	//previously rendered frame inside this texture
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
}

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.

Int ScreenBWFilter::m_fadeFrames;
Int ScreenBWFilter::m_curFadeFrame;
Real ScreenBWFilter::m_curFadeValue;
Int ScreenBWFilter::m_fadeDirection;

ScreenBWFilter screenBWFilter;
ScreenBWFilterDOT3 screenBWFilterDOT3;	//slower version for older cards without pixel shaders.

///List of different BW shader implementations in order of preference
W3DFilterInterface *ScreenBWFilterList[]=
{
	&screenBWFilter,
	&screenBWFilterDOT3,	//slower version for older cards without pixel shaders.
	nullptr
};

Int ScreenBWFilter::init()
{
#ifdef RTS_ZEROHOUR
    m_dwBWPixelShader=0; m_curFadeFrame=0;
    W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;
    return W3DShaderManager::canRenderToTexture();
#else
	Int res;

	m_dwBWPixelShader = 0;
	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//Monochrome pixel shader.
			if (!W3DShaderManager::LoadAndCreateShader("shaders\\monochrome.pso", false, &m_dwBWPixelShader))
				return FALSE;

			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;

			return TRUE;
		}
	}
	return FALSE;
#endif
}

Bool ScreenBWFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{

	TextureClass * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

#ifdef RTS_ZEROHOUR
    Graphics::ScreenFilterParameters parameters;
    parameters.operation=1; parameters.fade=m_curFadeValue;
    if (mode==FM_VIEW_BW_RED_AND_WHITE) parameters.tint={1,0,0,1};
    if (mode==FM_VIEW_BW_GREEN_AND_WHITE) parameters.tint={0,1,0,1};
    Draw_Screen_Filter_Quad(v,tex,parameters);
#else
	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif

	reset();
	return true;
}

Int ScreenBWFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		WW3D::Get_Render_Backend()->Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		WW3D::Get_Render_Backend()->Set_Shader(ShaderClass::_PresetOpaqueShader);
		WW3D::Get_Render_Backend()->Set_Texture(0,nullptr);
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		WW3D::Get_Render_Backend()->Set_Depth_Function(RenderBackendCompareFunction::Always);;
		WW3D::Get_Render_Backend()->Set_Depth_Write_Enabled(false);;
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		WW3D::Get_Render_Backend()->Set_Pixel_Shader(m_dwBWPixelShader);
		{ const Vector4 value(0.3f, 0.59f, 0.11f, 1.0f); WW3D::Get_Render_Backend()->Set_Pixel_Shader_Constant(0, &value, 1); }

		Vector4	color(1.0f,1.0f,1.0f,1.0f);	//multiply color

		if (mode == FM_VIEW_BW_BLACK_AND_WHITE)
		{	//back & white mode
			color.X=1.0f;
			color.Y=1.0f;
			color.Z=1.0f;
		}
		if (mode == FM_VIEW_BW_RED_AND_WHITE)
		{	//red is on
			color.X = 1.0f;
			color.Y = 0.0f;
			color.Z = 0.0f;
			//inverse red is on
			//red is on
//			color.x = 0.0f;
//			color.y = 1.0f;
//			color.z = 1.0f;
		}
		if (mode == FM_VIEW_BW_GREEN_AND_WHITE)
		{
			color.X = 0.0f;
			color.Y = 1.0f;
			color.Z = 0.0f;
		}

		WW3D::Get_Render_Backend()->Set_Pixel_Shader_Constant(1, &color, 1);
		{ const Vector4 value(m_curFadeValue, m_curFadeValue, m_curFadeValue, 1.0f); WW3D::Get_Render_Backend()->Set_Pixel_Shader_Constant(2, &value, 1); }
/* Optional pixel shader constants are set through IRenderBackend. */
		return true;
	}
	return false;
}

void ScreenBWFilter::reset()
{
	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);	//previously rendered frame inside this texture
	WW3D::Get_Render_Backend()->Set_Pixel_Shader(0);	//turn off pixel shader
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
}

Int ScreenBWFilter::shutdown()
{
	if (m_dwBWPixelShader)
		DeletePixelShaderHandle(m_dwBWPixelShader);

	m_dwBWPixelShader=0;

	return TRUE;
}

/**Alternate version of the above filter which does not require pixel shaders - good for older cards*/
Int ScreenBWFilterDOT3::init()
{
#ifdef RTS_ZEROHOUR
    return FALSE;
#else
	Int res;

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilterDOT3;
			return TRUE;
	}
	return FALSE;
#endif
}

Bool ScreenBWFilterDOT3::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilterDOT3::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
#ifdef RTS_ZEROHOUR
    return ScreenBWFilter::postRender(mode,scrollDelta,doExtraRender);
#else
	TextureClass * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();

	std::uint32_t currentFade=(((Int)((1.0f-m_curFadeValue) * 255.0f))<<24) | 0x00ffffff;	//store alpha value

	v[0].color = currentFade;
	v[1].color = currentFade;
	v[2].color = currentFade;
	v[3].color = currentFade;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

	//Draw B&W version first
	if (WW3D::Get_Render_Backend()->Supports_Dot3())
	{	//Override W3D states with customizations for grayscale
		WW3D::Get_Render_Backend()->Set_Texture_Factor(0x80A5CA8E);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Color, 0, RenderBackendTextureArgument::TextureFactor, RenderBackendTextureArgumentModifiers::AlphaReplicate);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Color, 1, RenderBackendTextureArgument::Texture, RenderBackendTextureArgumentModifiers::None);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Color, 2, RenderBackendTextureArgument::TextureFactor, RenderBackendTextureArgumentModifiers::AlphaReplicate);;
		WW3D::Get_Render_Backend()->Set_Texture_Operation(0, RenderBackendTextureComponent::Color, RenderBackendTextureOperation::MultiplyAdd);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Color, 1, RenderBackendTextureArgument::Current, RenderBackendTextureArgumentModifiers::None);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Color, 2, RenderBackendTextureArgument::TextureFactor, RenderBackendTextureArgumentModifiers::None);;
		WW3D::Get_Render_Backend()->Set_Texture_Operation(1, RenderBackendTextureComponent::Color, RenderBackendTextureOperation::DotProduct3);;
	}
	else
	{	//doesn't have DOT3 blend mode so fake it another way.
		WW3D::Get_Render_Backend()->Set_Texture_Factor(0x60606060);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Color, 1, RenderBackendTextureArgument::Texture, RenderBackendTextureArgumentModifiers::None);;
		WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Color, 2, RenderBackendTextureArgument::TextureFactor, RenderBackendTextureArgumentModifiers::None);;
		WW3D::Get_Render_Backend()->Set_Texture_Operation(0, RenderBackendTextureComponent::Color, RenderBackendTextureOperation::Modulate);;
	}

	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, tex);	//previously rendered frame inside this texture

	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

	//Draw normal view blended by current fade level
	ShaderClass::Invalidate();	//reset DOT3 blend from above.
	ShaderClass shader=ShaderClass::_PresetAlphaShader;
	shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	WW3D::Get_Render_Backend()->Set_Shader(shader);
	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices
	//replace texture alpha with vertex alpha
	WW3D::Get_Render_Backend()->Set_Texture_Operation(0, RenderBackendTextureComponent::Alpha, RenderBackendTextureOperation::SelectArgument2);;

	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

	reset();
	return true;
#endif
}

Int ScreenBWFilterDOT3::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		WW3D::Get_Render_Backend()->Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		WW3D::Get_Render_Backend()->Set_Shader(ShaderClass::_PresetOpaqueShader);
		WW3D::Get_Render_Backend()->Set_Texture(0,nullptr);
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		WW3D::Get_Render_Backend()->Set_Depth_Function(RenderBackendCompareFunction::Always);;
		WW3D::Get_Render_Backend()->Set_Depth_Write_Enabled(false);;
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		return true;
	}
	return false;
}

void ScreenBWFilterDOT3::reset()
{
	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);	//previously rendered frame inside this texture
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
}

Int ScreenBWFilterDOT3::shutdown()
{
	return TRUE;
}

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades screen between 2 different views of the scene with both being visible at once.

Int ScreenCrossFadeFilter::m_fadeFrames;
Int ScreenCrossFadeFilter::m_curFadeFrame;
Real ScreenCrossFadeFilter::m_curFadeValue;
Int ScreenCrossFadeFilter::m_fadeDirection;
TextureClass *ScreenCrossFadeFilter::m_fadePatternTexture=nullptr;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;

ScreenCrossFadeFilter screenCrossFadeFilter;

///List of different BW shader implementations in order of preference
///@todo: Add a version that doesn't require pixel shader
W3DFilterInterface *ScreenCrossFadeFilterList[]=
{
	&screenCrossFadeFilter,
	nullptr
};

Int ScreenCrossFadeFilter::init()
{
	if (!TheDisplay)
		return FALSE;	//effect is useless without a view so no point initializing for the WB, etc.

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture())
		// Have to be able to render to texture.
		return FALSE;

	//Load an alpha mask texture that will mix foreground/background views.
	m_fadePatternTexture=WW3DAssetManager::Get_Instance()->Get_Texture("exmask_g.tga");
	if (!m_fadePatternTexture)
		return FALSE;
	m_fadePatternTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	W3DFilters[FT_VIEW_CROSSFADE]=&screenCrossFadeFilter;

	return TRUE;
}

Bool ScreenCrossFadeFilter::updateFadeLevel()
{
	if (m_fadeDirection > 0)
	{	//turning effect on
		m_curFadeFrame++;
		Int fade = m_curFadeFrame;

		if (fade<m_fadeFrames)
		{
			m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
		}
		else
		{
			m_curFadeFrame = 0;
			m_curFadeValue = 1.0f;
			m_fadeDirection = 0;
			return false;
		}
	}
	else
	if (m_fadeDirection < 0)
	{	//turning effect off
		Int fade = m_curFadeFrame;
		if (fade<m_fadeFrames)
		{
			m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			m_curFadeFrame++;
		}
		else
		{	m_curFadeValue = 0.0f;
			TheTacticalView->setViewFilterMode(FM_NULL_MODE);
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
			m_curFadeFrame = 0;
			m_fadeDirection = 0;
			return false;
		}
	}
	return true;
}

Bool ScreenCrossFadeFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (updateFadeLevel())
	{	//if fade has not completed
		W3DShaderManager::startRenderToTexture();
		scenePassMode=SCENE_PASS_ALPHA_MASK;
		skipRender = false;
		m_skipRender=true;	//tell the postRender function not to draw into framebuffer yet.
		return true;
	}
	//fade must have completed
	return true;
}

Bool ScreenCrossFadeFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{

	TextureClass * tex;

	if (m_skipRender)
	{
		//don't render anything to frame buffer because we still need to draw the new scene
		//that we're fading into.  Okay to render on the next call.
		m_skipRender = false;
		doExtraRender = TRUE;
		tex =	W3DShaderManager::endRenderToTexture();
		return true;
	}

	tex=W3DShaderManager::getRenderTexture();

	DEBUG_ASSERTCRASH(tex, ("Require last rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
		float	u1;
		float	v1;
	} v[4];

	Int xpos, ypos, width, height;
	Real radius = 0.0f;

	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, tex);	//previously rendered frame inside this texture
	if (mode == FM_VIEW_CROSSFADE_CIRCLE)
	{	WW3D::Get_Render_Backend()->Set_Texture_Resource(1, m_fadePatternTexture);
		//Use the current fade level to scale the mask texture, for other modes the texture
		//comes pre-scaled so doesn't require uv scaling.
		radius = (1.0f-m_curFadeValue)*2.0f;
		if (radius <= 0)
			radius = 0.01f;
		radius = 0.5f/radius;
	}

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

/*	Real radius = (1.0f-m_curFadeValue);
	if (radius <= 0)
		radius = 0.01f;
	radius = 25.0f-radius*24.75f;
*/
	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[0].u1 = 0.5f+radius;	v[0].v1 = 0.5f+radius;
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[1].u1 = 0.5f+radius;	v[1].v1 = 0.5f-radius;
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[2].u1 = 0.5f-radius;	v[2].v1 = 0.5f+radius;
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[3].u1 = 0.5f-radius;	v[3].v1 = 0.5f-radius;

	std::uint32_t diffuse = 0xffffffff;//((Int)((m_curFadeValue) * 255.0f) << 24) | 0x00ffffff;	//store alpha value in vertex diffuse

	v[0].color = diffuse;
	v[1].color = diffuse;
	v[2].color = diffuse;
	v[3].color = diffuse;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture2);

// Fixed-function texture filtering is owned by the render backend.

#ifdef RTS_ZEROHOUR
    Graphics::ScreenFilterParameters parameters;
    parameters.operation=mode==FM_VIEW_CROSSFADE_CIRCLE ? 2.0f : 0.0f;
    Graphics::ScreenFilterStyle style; style.blend=true;
    Draw_Screen_Filter_Quad(v,tex,parameters,style,m_fadePatternTexture);
#else
	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif

	reset();
	return true;
}

Int ScreenCrossFadeFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		WW3D::Get_Render_Backend()->Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		WW3D::Get_Render_Backend()->Set_Shader(ShaderClass::_PresetAlphaShader);
		WW3D::Get_Render_Backend()->Set_Texture(0,nullptr);
		WW3D::Get_Render_Backend()->Set_Texture(1,nullptr);
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(0, true, RenderBackendTextureAddressMode::Clamp);;
		WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(0, false, RenderBackendTextureAddressMode::Clamp);;

		if (mode == FM_VIEW_CROSSFADE_CIRCLE)
		{	//cross-fading using circle mask stored in stage 1
			WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Color, 1, RenderBackendTextureArgument::Texture, RenderBackendTextureArgumentModifiers::None);;
			WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Color, 2, RenderBackendTextureArgument::Current, RenderBackendTextureArgumentModifiers::None);;
			WW3D::Get_Render_Backend()->Set_Texture_Operation(1, RenderBackendTextureComponent::Color, RenderBackendTextureOperation::Modulate);;
			WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Alpha, 1, RenderBackendTextureArgument::Texture, RenderBackendTextureArgumentModifiers::None);;
			WW3D::Get_Render_Backend()->Set_Texture_Argument(1, RenderBackendTextureComponent::Alpha, 2, RenderBackendTextureArgument::Current, RenderBackendTextureArgumentModifiers::None);;
			WW3D::Get_Render_Backend()->Set_Texture_Operation(1, RenderBackendTextureComponent::Alpha, RenderBackendTextureOperation::Modulate);;
			WW3D::Get_Render_Backend()->Set_Texture_Coordinate_Source(1, RenderBackendTextureCoordinateSource::PassThrough, 1);;
			WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(1, true, RenderBackendTextureAddressMode::Clamp);;
			WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(1, false, RenderBackendTextureAddressMode::Clamp);;
			WW3D::Get_Render_Backend()->Set_Texture_Filter(1, RenderBackendTextureFilterType::MipMap, RenderBackendTextureFilter::None);;
		}

		WW3D::Get_Render_Backend()->Set_Depth_Function(RenderBackendCompareFunction::Always);;
		WW3D::Get_Render_Backend()->Set_Depth_Write_Enabled(false);;

		return true;
	}
	return false;
}

void ScreenCrossFadeFilter::reset()
{
	WW3D::Get_Render_Backend()->Set_Texture_Operation(1, RenderBackendTextureComponent::Color, RenderBackendTextureOperation::Disable);;
	WW3D::Get_Render_Backend()->Set_Texture_Operation(1, RenderBackendTextureComponent::Alpha, RenderBackendTextureOperation::Disable);;
	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);	//previously rendered frame inside this texture
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
}

Int ScreenCrossFadeFilter::shutdown()
{
	REF_PTR_RELEASE(m_fadePatternTexture);

	return TRUE;
}

/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.

ScreenMotionBlurFilter screenMotionBlurFilter;

Coord3D ScreenMotionBlurFilter::m_zoomToPos;
Bool ScreenMotionBlurFilter::m_zoomToValid = false;

ScreenMotionBlurFilter::ScreenMotionBlurFilter():
m_decrement(false),
m_maxCount(0),
m_lastFrame(0),
m_skipRender(false)
{
}
///List of different motion blur implementations in order of preference
W3DFilterInterface *ScreenMotionBlurFilterList[]=
{
	&screenMotionBlurFilter,
	nullptr
};

Int ScreenMotionBlurFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}
	W3DFilters[FT_VIEW_MOTION_BLUR_FILTER]=this;
	return true;
}

Bool ScreenMotionBlurFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = m_skipRender;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenMotionBlurFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{

	TextureClass * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	
	Bool continueEffect = true;
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;


	if (m_additive) {
		WW3D::Get_Render_Backend()->Set_Source_Blend_Factor(RenderBackendBlendFactor::SourceAlpha);;
		WW3D::Get_Render_Backend()->Set_Destination_Blend_Factor(RenderBackendBlendFactor::One);;
	} else {
		WW3D::Get_Render_Backend()->Set_Source_Blend_Factor(RenderBackendBlendFactor::SourceAlpha);;
		WW3D::Get_Render_Backend()->Set_Destination_Blend_Factor(RenderBackendBlendFactor::InverseSourceAlpha);;
	}
	WW3D::Get_Render_Backend()->Set_Alpha_Blend_Enabled(false);;
	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

	Coord2D center;
	center.x = 0.5f;
	center.y = 0.5f;
	Bool pan = false;
	if (mode>=FM_VIEW_MB_PAN_ALPHA) {
		Real len = sqrt(scrollDelta.x*scrollDelta.x + scrollDelta.y*scrollDelta.y);
		//center.x += 0.5f * (scrollDelta.x/len);
		center.y -= 0.5f; // * (scrollDelta.y/len);
		m_decrement = false;
		m_maxCount = (len*200*m_panFactor/(Real)DEFAULT_PAN_FACTOR);
		if (m_maxCount<m_panFactor/2)
			m_maxCount = m_panFactor/2;
		if (m_maxCount>m_panFactor)
			m_maxCount=m_panFactor;
		pan = true;
		m_priorDelta = scrollDelta;
	} else if (mode == FM_VIEW_MB_END_PAN_ALPHA) {
		Real len = sqrt(m_priorDelta.x*m_priorDelta.x + m_priorDelta.y*m_priorDelta.y);
		center.x += 0.5f * (m_priorDelta.x/len);
		center.y -= 0.5f * (m_priorDelta.y/len);
		m_decrement = false;
		m_maxCount--;
		if (m_maxCount<2) {
			continueEffect = false;
		}
		pan = true;
	}


	m_skipRender = false;
	if (!pan && m_lastFrame != TheGameLogic->getFrame()) {
		if (m_decrement) {
			m_maxCount-=COUNT_STEP;
			if (m_maxCount<1) {
				m_decrement = false;
				continueEffect = false;
			}	else {
				m_skipRender = true;
			}
		} else {
			m_maxCount+=COUNT_STEP;
			if (m_maxCount>=MAX_COUNT) {
				m_decrement = true;
				if (m_doZoomTo && m_zoomToValid) {
					TheTacticalView->lookAt(&m_zoomToPos);
				} else {
					continueEffect = false;
				}
			}	else {
				m_skipRender = true;
			}
		}
	}
	Int	 i, j;
	if (!pan) {
		for (i=0; i<4; i++) {
			Real factor = 1.0f - (m_maxCount/(Real)MAX_COUNT)*0.90f;
			factor = sqrt(factor);
			v[i].u = ((v[i].u-center.x)*factor) + center.x;
			v[i].v = ((v[i].v-center.y)*factor) + center.y;
		}
	}
	WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Alpha, 1, RenderBackendTextureArgument::Current, RenderBackendTextureArgumentModifiers::None);;
	WW3D::Get_Render_Backend()->Set_Texture_Argument(0, RenderBackendTextureComponent::Alpha, 2, RenderBackendTextureArgument::Texture, RenderBackendTextureArgumentModifiers::None);;
	WW3D::Get_Render_Backend()->Set_Texture_Operation(0, RenderBackendTextureComponent::Alpha, RenderBackendTextureOperation::SelectArgument1);;
#ifdef RTS_ZEROHOUR
    {
        Graphics::ScreenFilterParameters parameters; parameters.vertex_alpha=1;
        Graphics::ScreenFilterStyle style;
        Draw_Screen_Filter_Quad(v,tex,parameters,style);
    }
#else
	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif
	WW3D::Get_Render_Backend()->Set_Alpha_Blend_Enabled(true);;

	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();
	{
		Int limit = m_maxCount;
		if (m_maxCount>30) limit = 30;
		for (j=0; j<limit; j++) {
			for (i=0; i<4; i++) {
				Real factor = 0.99f;
				if (m_additive) factor = 0.98f;
				Int alpha = 0x15;
				if (m_additive) {
					alpha = 0x09;
					if (m_maxCount>limit) {
						alpha += (m_maxCount-limit)/5;
					}
					if (m_maxCount==MAX_COUNT) alpha += 60;
				}
				v[i].color = (alpha<<24)|0x00ffffff; //
				if (pan) {
					v[i].u = ((v[i].u-center.x)*(factor+.006)) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				} else {
					v[i].u = ((v[i].u-center.x)*factor) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				}
			}
#ifdef RTS_ZEROHOUR
    {
        Graphics::ScreenFilterParameters parameters; parameters.vertex_alpha=1;
        Graphics::ScreenFilterStyle style;
        style.blend=true;
        style.destination=m_additive ? Graphics::RHIBlendFactor::One : Graphics::RHIBlendFactor::InverseSourceAlpha;
        Draw_Screen_Filter_Quad(v,tex,parameters,style);
    }
#else
			WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif

		}
	}
	m_lastFrame = TheGameLogic->getFrame();
	if (pan){
		m_skipRender = false;
	}
	reset();
	if (!continueEffect) {
		m_zoomToValid = false;
	}
	return continueEffect;
}

Bool ScreenMotionBlurFilter::setup(FilterModes mode)
{

	m_additive = false;

	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_SATURATE ||
			mode == FM_VIEW_MB_OUT_SATURATE) {
		m_additive = true;
	}

	m_doZoomTo = false;
	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_AND_OUT_ALPHA ) {
		m_doZoomTo = true;
	}
	if (mode >= FM_VIEW_MB_PAN_ALPHA)	{
		m_panFactor = (int)mode - FM_VIEW_MB_PAN_ALPHA;
		if (m_panFactor<1) m_panFactor = DEFAULT_PAN_FACTOR;
	}
	m_skipRender = false;
	if (mode != FM_VIEW_MB_END_PAN_ALPHA)
		m_maxCount = 0;
	m_decrement = false;
	m_skipRender = false;
	switch (mode) {
		case FM_VIEW_MB_OUT_SATURATE:
		case FM_VIEW_MB_OUT_ALPHA:
			m_maxCount = MAX_COUNT;
			m_decrement = TRUE;
			break;
	}
	return true;
}

Int ScreenMotionBlurFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface motion blurred

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		WW3D::Get_Render_Backend()->Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		WW3D::Get_Render_Backend()->Set_Shader(ShaderClass::_PresetOpaqueShader);
		WW3D::Get_Render_Backend()->Set_Texture(0,nullptr);
		WW3D::Get_Render_Backend()->Set_Texture(1,nullptr);
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices

		WW3D::Get_Render_Backend()->Set_Depth_Function(RenderBackendCompareFunction::Always);;
		WW3D::Get_Render_Backend()->Set_Depth_Write_Enabled(false);;
		WW3D::Get_Render_Backend()->Apply_Render_State_Changes();	//force update of view and projection matrices
	}
	return TRUE;
}

void ScreenMotionBlurFilter::reset()
{
	WW3D::Get_Render_Backend()->Set_Texture_Resource(0, nullptr);	//previously rendered frame inside this texture
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();
}

Int ScreenMotionBlurFilter::shutdown()
{
	return TRUE;
}


/** List of all custom filter lists - each list in this list contains variations of the same
	filter to allow it to work on different hardware configurations.
*/
W3DFilterInterface **MasterFilterList[]=
{
	ScreenDefaultFilterList,
	ScreenBWFilterList,
	ScreenMotionBlurFilterList,
	ScreenCrossFadeFilterList,
	nullptr
};

// W3DShaderManager::W3DShaderManager =========================================
/** Constructor - just clears some variables */
//=============================================================================
W3DShaderManager::W3DShaderManager()
{
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
		m_renderTexture = nullptr;
			m_renderingToTexture = false;
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++)
	{	W3DShaders[i]=nullptr;
		W3DShadersPassCount[i]=0;
	}
	for (i=0; i<FT_MAX; i++)
	{	W3DFilters[i]=nullptr;
	}
	for (i=0; i<8; i++)
	{
		m_Textures[i]=nullptr;
	}
	m_currentShader=(W3DShaderManager::ShaderTypes)-1;
}

// W3DShaderManager::init =======================================================
/** Walk through all shaders and find versions suitable for current hardware */
//=============================================================================
void W3DShaderManager::init()
{
	int i,j;
	SurfaceClass *back_buffer = WW3D::Get_Render_Backend()->Get_Back_Buffer_Surface();
	if (back_buffer != nullptr)
	{
		SurfaceClass::SurfaceDescription desc;
		back_buffer->Get_Description(desc);
		back_buffer->Release_Ref();
		m_renderTexture = WW3D::Get_Render_Backend()->Create_Render_Target(desc.Width, desc.Height, desc.Format);
	}

	W3DFilterInterface **filters;

	for (i=0; MasterFilterList[i] != nullptr; i++)
	{
		filters=MasterFilterList[i];
		for (j=0; filters[j] != nullptr; j++)
		{
			if (filters[j]->init())
				break;	//found a working shader
		}
	}

	DEBUG_LOG(("ShaderManager ChipsetID %d", W3DShaderManager::getChipset()));
}

// W3DShaderManager::shutdown =======================================================
/** Any shaders which allocate resources will be allowed to free them */
//=============================================================================
void W3DShaderManager::shutdown()
{
		REF_PTR_RELEASE(m_renderTexture);
			m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	//release any assets associated with a shader (vertex/pixel shaders, textures, etc.)
	Int i=0;
	for (; i<W3DShaderManager::ST_MAX; i++) {
		if (W3DShaders[i]) {
			W3DShaders[i]->shutdown();
		}
	}

	for (i=0; i < FT_MAX; i++)
	{
		if (W3DFilters[i])
		{
			W3DFilters[i]->shutdown();
		}
	}
}

//=============================================================================

Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (W3DFilters[filter])
	{	Bool result=W3DFilters[filter]->preRender(skipRender,scenePassMode);
		if (result)
			m_currentFilter = filter;
		return result;
	}
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterPostRender(FilterTypes filter, FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->postRender(mode, scrollDelta,doExtraRender);

	m_currentFilter = FT_NULL_FILTER;
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
	static Bool filterSetup(FilterTypes filter, FilterModes mode);
Bool W3DShaderManager::filterSetup(FilterTypes filter, FilterModes mode)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->setup(mode);
	return FALSE;
}

/*Draws 2 triangles covering the viewport given the current render states*/
void W3DShaderManager::drawViewport(Int color)
{

	
	struct _TRANS_LIT_TEX_VERTEX {
		Vector4 p;
		std::uint32_t color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = Vector4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = Vector4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = Vector4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = Vector4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = color;
	v[1].color = color;
	v[2].color = color;
	v[3].color = color;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	WW3D::Get_Render_Backend()->Set_Vertex_Format(RenderBackendVertexFormat::TransformedPositionDiffuseTexture);

#ifdef RTS_ZEROHOUR
    Graphics::ScreenFilterParameters parameters; parameters.textured=0;
    Graphics::ScreenFilterStyle style; style.color_write_mask=8;
    Draw_Screen_Filter_Quad(v,nullptr,parameters,style);
#else
	WW3D::Get_Render_Backend()->Draw_Primitive_Up(RenderBackendPrimitiveType::TriangleStrip, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX), RenderBackendVertexFormat::TransformedPositionDiffuseTexture);
#endif
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Starts rendering to a texture.
 */
//=============================================================================
void W3DShaderManager::startRenderToTexture()
{
	DEBUG_ASSERTCRASH(!m_renderingToTexture, ("Already rendering to texture - cannot nest calls."));

	if (m_renderingToTexture || m_renderTexture == nullptr) return;
	WW3D::Get_Render_Backend()->Set_Render_Target(m_renderTexture);
	m_renderingToTexture = true;
	if (TheGlobalData->m_showSoftWaterEdge)
	{	//Soft water edges use frame buffer destination alpha so we must clear it to a known value.
		if (m_currentFilter == FT_VIEW_MOTION_BLUR_FILTER || m_currentFilter == FT_VIEW_CROSSFADE)
		{	//these filters rely on the previous frame being visible so we must be careful about clearing
			//frame buffer.  Only clear the alpha channel
			WW3D::Get_Render_Backend()->Set_Color_Write_Mask(RenderBackendColorWriteMask::Alpha);;	//only clear alpha
			ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;
			shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			WW3D::Get_Render_Backend()->Set_Shader(shader);

			VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			WW3D::Get_Render_Backend()->Set_Material(vmat);
			REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

			drawViewport(0x00ffffff | (((Int)(TheWaterTransparency->m_minWaterOpacity*255.0f)) <<24));
			WW3D::Get_Render_Backend()->Set_Color_Write_Mask(RenderBackendColorWriteMask::RGB);;	//disable writes to alpha
		}
		else	//normal clear that overwrites everything.
			WW3D::Get_Render_Backend()->Clear(true, false, Vector3( 0.0f, 0.0f, 0.0f ), TheWaterTransparency->m_minWaterOpacity);
	}
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Ends rendering to a texture.
 */
//=============================================================================
TextureClass *W3DShaderManager::endRenderToTexture(void)
{
	DEBUG_ASSERTCRASH(m_renderingToTexture, ("Not rendering to texture."));
	if (!m_renderingToTexture) return nullptr;
	WW3D::Get_Render_Backend()->Set_Render_Target(nullptr);
	{
		//assume render target texture will be in stage 0.  Most hardware has "conditional" support for
		//non-power-of-2 textures so we must force some required states:
		WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(0, true, RenderBackendTextureAddressMode::Clamp);;
		WW3D::Get_Render_Backend()->Set_Texture_Address_Mode(0, false, RenderBackendTextureAddressMode::Clamp);;
		/* Texture W addressing is unused for 2D resources. */;
		WW3D::Get_Render_Backend()->Set_Texture_Filter(0, RenderBackendTextureFilterType::Magnification, RenderBackendTextureFilter::Linear);;
		WW3D::Get_Render_Backend()->Set_Texture_Filter(0, RenderBackendTextureFilterType::Minification, RenderBackendTextureFilter::Linear);;
		WW3D::Get_Render_Backend()->Set_Texture_Filter(0, RenderBackendTextureFilterType::MipMap, RenderBackendTextureFilter::None);;

		m_renderingToTexture = false;
	}
	return m_renderTexture;
}

/**Returns texture containing the image that was last rendered using any of the effects requiring render target
textures.  Used mostly for cross-fading effects that need an unmodified version of the view before the effect
was applied.  NOTE: This texture does not survive device reset.. so quit effect on reset!*/
TextureClass *W3DShaderManager::getRenderTexture(void)
{
	return m_renderTexture;
}

enum GraphicsVenderID CPP_11(: Int)
{
	DC_NVIDIA_VENDOR_ID	= 0x10DE,
	DC_3DFX_VENDOR_ID	= 0x121A,
	DC_ATI_VENDOR_ID	= 0x1002
};

// W3DShaderManager::ChipsetType =======================================================
/** Returns the chipset used by the currently active rendering device.  Can be useful
	for coding around specific driver bugs.
 */
//=============================================================================
ChipsetType W3DShaderManager::getChipset()
{
	//check if globaldata has an override for current chipset
	if (TheGlobalData && TheGlobalData->m_chipSetType != DC_UNKNOWN)
		return (ChipsetType)TheGlobalData->m_chipSetType;

	ChipsetType chip=DC_UNKNOWN;
	RenderBackendAdapterInfo info;

	if (WW3D::Get_Render_Backend()->Get_Adapter_Info(info))
	{
		m_driverVersion = (static_cast<std::int64_t>(info.driver_version_high) << 32) | info.driver_version_low;

		if(info.vendor_id == DC_NVIDIA_VENDOR_ID)
		{
			m_currentVendor = DC_NVIDIA_VENDOR_ID;

			if (info.device_id == 0x20)
				return DC_TNT;

			if (info.device_id >= 0x28 && info.device_id < 0x100)
				return DC_TNT2;

			if ( (info.device_id >= 0x100 && info.device_id <= 0x103) ||	//GeForce
				 (info.device_id >= 0x110 && info.device_id <= 0x113) ||	//GeForce2 MX
						 (info.device_id >= 0x150 && info.device_id <= 0x153) )	//GeForce2
           		return DC_GEFORCE2;

			if (info.device_id >= 0x200 && info.device_id < 0x250)
				return DC_GEFORCE3;

			if (info.device_id >= 0x250)
				return DC_GEFORCE4;
		}
		else
		if(info.vendor_id == DC_3DFX_VENDOR_ID)
		{
			m_currentVendor = DC_3DFX_VENDOR_ID;

			if (info.device_id == 0x0002)
				return DC_VOODOO2;
			if (info.device_id == 0x0005)
				return DC_VOODOO3;
			if (info.device_id == 0x0008)	///@todo: Just guessing on this one - find actual Voodoo4 deviceID.
				return DC_VOODOO4;
			if (info.device_id == 0x0009)
				return DC_VOODOO5;
		}
		else
		if(info.vendor_id == DC_ATI_VENDOR_ID)
		{
			m_currentVendor = DC_ATI_VENDOR_ID;

			if (info.device_id == 0x5144)
				return DC_RADEON;
			if (info.device_id == 0x514C)
				return DC_RADEON_8500;
			if (info.device_id == 0x4e44)
				return DC_RADEON_9700;
		}

		//None of the vendor specific ID's matched so use generic means to classify the card
		Int maxTextures=WW3D::Get_Render_Backend()->Get_Max_Textures_Per_Pass();
		Real pixelShaderVersion;

		char buf[256];

		//Convert version to Real
		sprintf(buf,"%d.%d",WW3D::Get_Render_Backend()->Get_Pixel_Shader_Major_Version(),WW3D::Get_Render_Backend()->Get_Pixel_Shader_Minor_Version());
		sscanf(buf,"%f",&pixelShaderVersion);

		if (maxTextures >= 4)
		{	if (pixelShaderVersion >= 1.1f)
				chip=DC_GENERIC_PIXEL_SHADER_1_1;
			if (pixelShaderVersion >= 1.4f)
				chip=DC_GENERIC_PIXEL_SHADER_1_4;
			if (maxTextures >= 8 && pixelShaderVersion >= 2.0f)
				chip=DC_GENERIC_PIXEL_SHADER_2_0;
		}
	}

	return chip;
}

//=============================================================================
// W3DShaderManager::LoadAndCreateShader
//=============================================================================
/** Loads and creates a backend-owned pixel or vertex shader.*/
//=============================================================================
Bool W3DShaderManager::LoadAndCreateShader(const char* file_path, Bool vertex_shader,
	uintptr_t* handle, const RenderBackendVertexShaderInputLayout * input_layout)
{
	if (getChipset() < DC_GENERIC_PIXEL_SHADER_1_1)
		return false;	//don't allow loading any shaders if hardware can't handle it.

	try
	{
		File *file = TheFileSystem->openFile(file_path, File::READ | File::BINARY);
		if (file == nullptr)
		{
			SDL_Log("Could not find shader file: %s", file_path != nullptr ? file_path : "(null)");
			return false;
		}

		FileInfo fileInfo;
		TheFileSystem->getFileInfo(AsciiString(file_path), &fileInfo);
		std::vector<unsigned char> shader(fileInfo.sizeLow);
		if (shader.empty())
		{
			file->close();
			SDL_Log("Shader file is empty: %s", file_path != nullptr ? file_path : "(null)");
			return false;
		}

		file->read(shader.data(), static_cast<unsigned>(shader.size()));

		file->close();

		IRenderBackend *backend = WW3D::Get_Render_Backend();
		const bool created = vertex_shader
			? backend != nullptr && backend->Create_Vertex_Shader(
				shader.data(), static_cast<unsigned>(shader.size()), handle, input_layout)
			: backend != nullptr && backend->Create_Pixel_Shader(
				shader.data(), static_cast<unsigned>(shader.size()), handle);

		if (!created)
		{
			SDL_Log("Failed to create shader: %s", file_path != nullptr ? file_path : "(null)");
			return false;
		}
	}
	catch(...)
	{
		SDL_Log("Error opening shader file: %s", file_path != nullptr ? file_path : "(null)");
		return false;
	}

	return true;
}

//For the MP test, we're enforcing high min-spec requirements that need to be verified.
#define MIN_INTEL_CPU_FREQ	1300
#define MIN_AMD_CPU_FREQ	1100
#define MIN_ACCEPTED_FREQUENCY	1300
#define MIN_ACCEPTED_MEMORY	(1024*1024*256)	//256 MB
#define MIN_ACCEPTED_TEXTURE_MEMORY	(1024*1024*30)	//30 MB

/**Hack to give gameengine access to this function*/
Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	return W3DShaderManager::testMinimumRequirements(videoChipType,cpuType,cpuFreq,numRAM,intBenchIndex,floatBenchIndex,memBenchIndex);
}

Bool W3DShaderManager::testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	if (videoChipType)
		*videoChipType = getChipset();

	if (cpuType)
	{
		*cpuType = XX;	//unknown

		//Check if it's an Athlon
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_AMD &&
				CPUDetectClass::Get_AMD_Processor() >= CPUDetectClass::AMD_PROCESSOR_ATHLON_025)
				*cpuType = K7;

		//Check if it's a P3
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM_III_MODEL_7)
				*cpuType = P3;
		//Check if it's a P4
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM4)
				*cpuType = P4;
	}

	if (cpuFreq)
		*cpuFreq=CPUDetectClass::Get_Processor_Speed();

	if (numRAM)
		*numRAM=CPUDetectClass::Get_Total_Physical_Memory();

	if (intBenchIndex && floatBenchIndex && memBenchIndex)
	{
		// TheSuperHackers @tweak Aliendroid1 19/06/2025 Legacy benchmarking code was removed.
		// Since modern hardware always meets the minimum requirements, we preset the benchmark "results" to a high value.
		*intBenchIndex = 10.0f;
		*floatBenchIndex = 10.0f;
		*memBenchIndex = 10.0f;
	}

	return TRUE;
}

/**Try to guess how well the video card will handle the game assuming very fast CPU*/
StaticGameLODLevel W3DShaderManager::getGPUPerformanceIndex()
{
	ChipsetType	chipType;
	StaticGameLODLevel detailSetting=STATIC_GAME_LOD_LOW;	//assume lowest settings for now.

	if ((chipType=getChipset()) != DC_UNKNOWN)
	{	//a known video card so we can make some assumptions
		if (chipType >=	DC_GEFORCE2)
			detailSetting=STATIC_GAME_LOD_LOW;	//these cards need multiple terrain passes.
		if (chipType >= DC_GENERIC_PIXEL_SHADER_1_1)	//these cards can do terrain in single pass.
			detailSetting=STATIC_GAME_LOD_VERY_HIGH;
	}

	return detailSetting;
}

/**We need a hardware independent method to compare different CPU's.  For lack of anything better, we'll
use time to calculate PIE using a slow random number algorithm.*/

/**Used to test function call overhead*/
void add(float *sum,float *addend)
{
	*sum = *sum + *addend;
}

/**Returns seconds needed to run the test*/
Real W3DShaderManager::GetCPUBenchTime()
{
	float ztot, yran, ymult, ymod, x, y, z, pi, prod;
    long int low, ixran, itot, j, iprod;

	const std::uint64_t frequency = SDL_GetPerformanceFrequency();
	const std::uint64_t start_time = SDL_GetPerformanceCounter();

    ztot = 0.0;
    low = 1;
    ixran = 1907;
    yran = 5813.0;
    ymult = 1307.0;
    ymod = 5471.0;
    itot = 560000;	//total iterations. This value ends up running at ~30 fps on our P4-2.2Ghz.

    for(j=1; j<=itot; j++)
    {
		iprod = 27611 * ixran;
		ixran = iprod - 74383*(long int)(iprod/74383);
		x = (float)ixran / 74383.0;
		prod = ymult * yran;
		yran = (prod - ymod*(long int)(prod/ymod));
		y = yran / ymod;
		z = x*x + y*y;
		add(&ztot,&z);
		if ( z <= 1.0 )
		{
		  low = low + 1;
		}
	}
	pi = 4.0 * (float)low/(float)itot;

	const std::uint64_t end_time = SDL_GetPerformanceCounter();
	return frequency != 0
		? static_cast<Real>(static_cast<double>(end_time - start_time) /
			static_cast<double>(frequency))
		: 0.0f;
}



// W3DShaderManager::setShroudTex =======================================================
/** Puts the shroud texture into a texture stage.
 */
//=============================================================================
