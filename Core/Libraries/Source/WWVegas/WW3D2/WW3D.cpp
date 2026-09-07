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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/WW3D.cpp                               $*
 *                                                                                             *
 *                   Org Author:: Greg_h                                                       *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *								$Modtime:: 08/05/02 10:03a                                             $*
 *                                                                                             *
 *                    $Revision:: 98                                                          $*
 *                                                                                             *
 * 07/01/02 KM Scalable shader library integration				                               *
 * 08/05/02 KM Texture class redesign
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   WW3D::Init -- Initialize the WW3D Library                                                 *
 *   WW3D::Shutdown -- shutdown the WW3D Library                                               *
 *   WW3D::Begin_Render -- mark the start of rendering for a new frame                         *
 *   WW3D::Render -- Render a 3D Scene using the given camera                                  *
 *   WW3D::Render -- Render a single render object                                             *
 *   WW3D::End_Render -- Mark the completion of a frame                                        *
 *   WW3D::Sync -- Time synchronization                                                        *
 *   WW3D::Get_Polygon_Mode -- returns the current rendering mode                              *
 *   WW3D::Update_Render_Device_Description -- updates the description of the current render d *
 *   WW3D::Flush_Texture_Cache -- dump all textures from the texture cache                     *
 *   WW3D::Allocate_Debug_Resources -- allocates the debug resources					              *
 *   WW3D::Release_Debug_Resources -- releases the debug resources									  *
 *   WW3D::Flush -- Process all pending rendering tasks                                        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


import Graphics.Frame.AttachmentBindings;
import Graphics.Scene.DrawParameters;
import Graphics.Scene.Props.Submission;
#include "WW3D.h"
#include "RInfo.h"
#include "AssetMgr.h"
#include "Camera.h"
#include "Scene.h"
#include "SegLine.h"
#include "Shader.h"
#include "VertMaterial.h"
#include "WWDebug/wwdebug.h"
#include "WWDebug/wwprofile.h"
#include "WWDebug/wwmemlog.h"
import Graphics.Resources.Textures.Sampling;
#include "WWLib/ffactory.h"
#include "WWLib/INI.h"
#include "Dazzle.h"
#include "MeshMdl.h"
#include "WW3D2/GraphicsGeometry.h"
#include "WWLib/bound.h"
#include "WWMath/Vector3i.h"
#include "WWLib/thread.h"
#include "WWLib/cpudetect.h"
import Graphics.Scene.OrderedDraws;
#include "ShdLib.h"
#include "Lib/BaseType.h"
#include <cstdint>
import Graphics.Backends.DX11.FrameRuntime;


const char* DAZZLE_INI_FILENAME="DAZZLE.INI";

#define DEFAULT_DEBUG_SHADER_BITS	(		SHADE_CNST(\
												ShaderClass::PASS_LEQUAL,\
												ShaderClass::DEPTH_WRITE_ENABLE,\
												ShaderClass::COLOR_WRITE_ENABLE,\
												ShaderClass::SRCBLEND_ONE,\
												ShaderClass::DSTBLEND_ZERO,\
												ShaderClass::FOG_DISABLE,\
												ShaderClass::GRADIENT_MODULATE,\
												ShaderClass::SECONDARY_GRADIENT_DISABLE,\
												ShaderClass::TEXTURING_DISABLE,\
												ShaderClass::ALPHATEST_DISABLE,\
												ShaderClass::CULL_MODE_ENABLE, \
												ShaderClass::DETAILCOLOR_DISABLE,\
												ShaderClass::DETAILALPHA_DISABLE) )

#define LIGHTMAP_DEBUG_SHADER_BITS	(		SHADE_CNST(\
												ShaderClass::PASS_LEQUAL,\
												ShaderClass::DEPTH_WRITE_ENABLE,\
												ShaderClass::COLOR_WRITE_ENABLE,\
												ShaderClass::SRCBLEND_ONE,\
												ShaderClass::DSTBLEND_ZERO,\
												ShaderClass::FOG_DISABLE,\
												ShaderClass::GRADIENT_DISABLE,\
												ShaderClass::SECONDARY_GRADIENT_DISABLE,\
												ShaderClass::TEXTURING_ENABLE,\
												ShaderClass::ALPHATEST_DISABLE,\
												ShaderClass::CULL_MODE_ENABLE, \
												ShaderClass::DETAILCOLOR_DISABLE,\
												ShaderClass::DETAILALPHA_DISABLE) )



/**********************************************************************************
**
**  WW3D Static Globals
**
***********************************************************************************/

float														WW3D::LogicFrameTimeMs = 1000.0f / WWSyncPerSecond; // initialized to something to avoid division by zero on first use
float															WW3D::FractionalSyncMs = 0.0f;
unsigned int											WW3D::SyncTime = 0;
unsigned int											WW3D::PreviousSyncTime = 0;
bool														WW3D::IsSortingEnabled = true;

float														WW3D::PixelCenterX = 0.0f;
float														WW3D::PixelCenterY = 0.0f;



bool														WW3D::IsInitted = false;
bool														WW3D::PreserveFPU = false;
bool														WW3D::IsRendering = false;

bool														WW3D::MungeSortOnLoad = false;

bool														WW3D::OverbrightModifyOnLoad = false;


int														WW3D::FrameCount = 0;
long														WW3D::UserStat0 = 0;
long														WW3D::UserStat1 = 0;
long														WW3D::UserStat2 = 0;

float														WW3D::DefaultNativeScreenSize = 1.0f;



VertexMaterialClass *								WW3D::DefaultDebugMaterial  = nullptr;
ShaderClass												WW3D::DefaultDebugShader(DEFAULT_DEBUG_SHADER_BITS);
ShaderClass												WW3D::LightmapDebugShader(LIGHTMAP_DEBUG_SHADER_BITS);

WW3D::PrelitModeEnum									WW3D::PrelitMode = PRELIT_MODE_LIGHTMAP_MULTI_PASS;
bool														WW3D::ExposePrelit = false;

bool														WW3D::SnapshotActivated=false;

WW3D::MeshDrawModeEnum								WW3D::MeshDrawMode = MESH_DRAW_MODE_OLD;
WW3D::NPatchesGapFillingModeEnum					WW3D::NPatchesGapFillingMode = NPATCHES_GAP_FILLING_ENABLED;
unsigned													WW3D::NPatchesLevel=1;
bool														WW3D::IsTexturingEnabled=true;
bool										WW3D::IsColoringEnabled=false;

int														WW3D::LastFrameMemoryAllocations;
int														WW3D::LastFrameMemoryFrees;


bool														WW3D::Lite = false;

namespace
{
	bool RenderServicesInitialized = false;

	void Initialize_Render_Services()
	{
		if (RenderServicesInitialized)
		{
			return;
		}

		Graphics::Get_Prop_Submission().Clear();
		SHD_INIT;
		VertexMaterialClass::Init();
		Graphics::Get_Resource_Load_Queue().Start();
		RenderServicesInitialized = true;
	}

	void Shutdown_Render_Services()
	{
		if (!RenderServicesInitialized)
		{
			return;
		}

		Graphics::Get_Resource_Load_Queue().Shutdown();
		Graphics::Get_Prop_Submission().Clear();
		VertexMaterialClass::Shutdown();
		SHD_SHUTDOWN;
		Graphics::Get_Prop_Submission().Clear();
		RenderServicesInitialized = false;
	}


}

/**********************************************************************************
**
**  WW3D Static Functions
**
***********************************************************************************/

void WW3D::Set_NPatches_Gap_Filling_Mode(NPatchesGapFillingModeEnum mode)
{
	if (NPatchesGapFillingMode!=mode) {
		NPatchesGapFillingMode=mode;
		Graphics::Get_Prop_Submission().Clear();
	}
}

void WW3D::Set_NPatches_Level(unsigned level)
{
	if (level>8) level=8;
	if (level<1) level=1;
	if (NPatchesLevel==1 && level>1) Graphics::Get_Prop_Submission().Clear();
	if (NPatchesLevel>1 && level==1) Graphics::Get_Prop_Submission().Clear();
	NPatchesLevel = level;
}

/***********************************************************************************************
 * WW3D::Init -- Initialize the WW3D Library                                                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Init(bool lite)
{
	assert(IsInitted == false);
	Lite = lite;
	WWDEBUG_SAY(("Allocate Debug Resources"));
	Allocate_Debug_Resources();

	/*
	** Initialize the dazzle system
	*/
	if (!lite) {
		WWDEBUG_SAY(("Init Dazzles"));
		FileClass * dazzle_ini_file = _TheFileFactory->Get_File(DAZZLE_INI_FILENAME);
		if (dazzle_ini_file) {
			INIClass dazzle_ini(*dazzle_ini_file);
			DazzleRenderObjClass::Init_From_INI(&dazzle_ini);
			_TheFileFactory->Return_File(dazzle_ini_file);
		}
	}
    Graphics::Get_Scene_Draw_Queue().Clear();

	if (!lite) {
		IsInitted = true;
	}
	Initialize_Render_Services();
	WWDEBUG_SAY(("WW3D Init completed"));
	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Shutdown -- shutdown the WW3D Library                                                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Shutdown()
{
	assert(Lite || IsInitted == true);
//	WWDEBUG_SAY(("WW3D::Shutdown"));

	/*
	** Free memory in predictive LOD optimizer
	*/

	/*
	** Free the DazzleRenderObject class stuff. Whatever it is. ST - 6/11/2001 8:20PM
	*/
	if (!Lite) {
		DazzleRenderObjClass::Deinit ();
	}

	/*
	** Release all of our assets
	*/
	Release_Debug_Resources();
	if (WW3DAssetManager::Get_Instance()) {
		WW3DAssetManager::Get_Instance()->Free_Assets();
	}

	Shutdown_Render_Services();


    Graphics::Get_Scene_Draw_Queue().Clear();
    Graphics::Get_Scene_Draw_Queue().Set_Enabled(false);


	IsInitted = false;
	return WW3D_ERROR_OK;
}





















































void WW3D::_Invalidate_Mesh_Cache()
{
	Graphics::Get_Prop_Submission().Clear();
}

void WW3D::_Invalidate_Textures()
{
	if (!WW3DAssetManager::Get_Instance()) return;

	Graphics::Get_Resource_Load_Queue().Drain();

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());

	// Loop through all the textures in the manager
	for (ite.First();!ite.Is_Done();ite.Next()) {
		// Get the current texture
		TextureClass* tex=ite.Peek_Value();
		tex->Invalidate();
	}
}

/***********************************************************************************************
 * WW3D::Begin_Render -- mark the start of rendering for a new frame                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Begin_Render(bool clear,bool clearz,const Vector3 & color, float dest_alpha, void(*network_callback)())
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Begin_Render");
	WWASSERT(IsInitted);

	SNAPSHOT_SAY(("=========================================="));
	SNAPSHOT_SAY(("========== WW3D::Begin_Render ============"));
	SNAPSHOT_SAY(("==========================================\n"));

	if (!Graphics::Frame_Device_Ready()) return WW3D_ERROR_GENERIC;

	// Memory allocation statistics
	LastFrameMemoryAllocations=WWMemoryLogClass::Get_Allocate_Count();
	LastFrameMemoryFrees=WWMemoryLogClass::Get_Free_Count();
	WWMemoryLogClass::Reset_Counters();

	Graphics::Get_Resource_Load_Queue().Update(network_callback);
	TextureBaseClass::Invalidate_Old_Unused_Textures(0);
//	TextureClass::_Reset_Time_Stamp();



	WWASSERT(!IsRendering);
	IsRendering = true;

	// If we want to clear the screen, we need to set the viewport to include the entire screen:
	if (clear || clearz) {
		const auto vp = Graphics::Get_Attachment_Bindings().Default().viewport;
		Graphics::Get_Attachment_Bindings().Set_Viewport(vp);
		Graphics::Get_Attachment_Bindings().Clear(clear, clearz, {color.X,color.Y,color.Z,dest_alpha});
	}

	// The graphics frame is already active. Restore its selected main attachments.
	if (!Graphics::Get_Attachment_Bindings().Offscreen()
		&& !Graphics::Get_Attachment_Bindings().Rebind()) {
		IsRendering = false;
		return WW3D_ERROR_GENERIC;
	}

	return WW3D_ERROR_OK;
}

/***********************************************************************************************
 * WW3D::Render -- Render a 3D Scene using the given camera                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(SceneClass * scene,CameraClass * cam,bool clear,bool clearz,const Vector3 & color)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Render");
	WWMEMLOG(MEM_GAMEDATA);
	WWASSERT(IsInitted);
	WWASSERT(IsRendering);
	WWASSERT(scene);
	WWASSERT(cam);

	if (clear || clearz) {
		Graphics::Get_Attachment_Bindings().Clear(clear, clearz, {color.X,color.Y,color.Z,0});
	}

	return Render_Scene_Pass(scene, cam);
}


/***********************************************************************************************
 * WW3D::Render_Scene_Pass -- Submit a scene inside an existing backend frame                *
 *                                                                                             *
 * This is the explicit scene-submission half of an off-screen render pass. The caller owns  *
 * the render target, viewport, camera scope, and pass state. No frame lifecycle or swap-chain *
 * operation is performed here.                                                               *
 *=============================================================================================*/
WW3DErrorType WW3D::Render_Scene_Pass(SceneClass * scene,CameraClass * cam,
	const Graphics::RHIViewport *viewport_override)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Render_Scene_Pass");
	WWMEMLOG(MEM_GAMEDATA);
	WWASSERT(IsInitted);
	WWASSERT(IsRendering);
	WWASSERT(scene);
	WWASSERT(cam);
	if (scene == nullptr || cam == nullptr || Graphics::Shared_Frame_Device() == nullptr)
	{
		return WW3D_ERROR_GENERIC;
	}

	cam->On_Frame_Update();
	RenderInfoClass rinfo(*cam);

	// Apply the camera and viewport (including depth range)
	cam->Apply();
	if (viewport_override != nullptr)
	{
		Graphics::Get_Attachment_Bindings().Set_Viewport(*viewport_override);
	}

	Graphics::SceneDrawScope draw_scope(Graphics::Get_Scene_Draw_Parameters());

	// set the rendering mode
	switch(scene->Get_Polygon_Mode()) {
		case SceneClass::POINT:
			Graphics::Get_Scene_Draw_Parameters().wireframe = false;
			break;
		case SceneClass::LINE:
			Graphics::Get_Scene_Draw_Parameters().wireframe = true;
			break;
		case SceneClass::FILL:
			Graphics::Get_Scene_Draw_Parameters().wireframe = false;
			break;
	}


	// render the scene


	scene->Render(rinfo);

	Flush(rinfo);

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Render -- Render a single render object                                               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/4/2001   gth : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::Render(
	RenderObjClass & obj,
	RenderInfoClass & rinfo
)
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::Render");
	WWASSERT(IsInitted);
	WWASSERT(IsRendering);

	{
		WWPROFILE("On_Frame_Update");
		rinfo.Camera.On_Frame_Update();
	}

	// Apply the camera and viewport (including depth range)
	rinfo.Camera.Apply();

	Graphics::SceneDrawScope draw_scope(Graphics::Get_Scene_Draw_Parameters());

	// set the rendering mode
	Graphics::Get_Scene_Draw_Parameters().wireframe = false;


	// Render the object

	obj.Render(rinfo);

	Flush(rinfo);

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * WW3D::Flush -- Process all pending rendering tasks                                          *
 *                                                                                             *
 *    NOTE: This normally happens AUTOMATICALLY. The user should almost *NEVER* have to call   *
 *    this function.  Anyway, this function causes all of the deferred rendering systems to    *
 *    actually perform all of their rendering tasks.  This includes the DX9MeshRenderer and    *
 *    the sorting system.                                                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *    Don't call this unless you know what you're doing                                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/17/2001  gth : Created.                                                                 *
 * 07/01/02 KM Scalable shader library integration				                               *
 *=============================================================================================*/
void WW3D::Flush(RenderInfoClass & rinfo)
{
	Graphics::Get_Prop_Submission().Flush_Materials();
	SHD_FLUSH;
	Graphics::Get_Scene_Draw_Queue().Drain(&rinfo, [] { Graphics::Get_Prop_Submission().Flush_Materials(); });	//draws things like water

	Graphics::Get_Prop_Submission().Flush_Transparent();

}


/***********************************************************************************************
 * WW3D::End_Render -- Mark the completion of a frame                                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType WW3D::End_Render()
{
	if (!IsInitted) {
		return(WW3D_ERROR_OK);
	}

	WWPROFILE("WW3D::End_Render");

	WWASSERT(IsRendering);
	WWASSERT(IsInitted);

	// Finish deferred transparent submissions before presentation.
	Graphics::Get_Prop_Submission().Flush_Transparent();

	IsRendering = false;


	FrameCount++;


	SNAPSHOT_SAY(("=========================================="));
	SNAPSHOT_SAY(("========== WW3D::End_Render =============="));
	SNAPSHOT_SAY(("==========================================\n"));

	Activate_Snapshot(false);

	// (gth) I've found some cases where its not safe to rely on our "shadow" copy (of
	// matrices for example) across multiple frames.  So even though this is slightly
	// less "optimal", lets just reset the caches each frame.

	return WW3D_ERROR_OK;
}


void WW3D::Update_Logic_Frame_Time(float milliseconds)
{
	LogicFrameTimeMs = milliseconds;
	FractionalSyncMs += milliseconds;
}


/***********************************************************************************************
 * WW3D::Sync -- Time synchronization                                                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Sync(bool step)
{
	PreviousSyncTime = SyncTime;

	if (step)
	{
		unsigned int integralSyncMs = (unsigned int)FractionalSyncMs;
		FractionalSyncMs -= integralSyncMs;
		SyncTime += integralSyncMs;
	}
}







void WW3D::Enable_Texturing(bool b)
{
	if (b==IsTexturingEnabled) return;
	IsTexturingEnabled=b;
//	_Invalidate_Textures();
}

void WW3D::Enable_Coloring(unsigned int color)
{
	IsColoringEnabled = (color == 0) ? false : true;
}

/***********************************************************************************************
 * WW3D::Peek_Default_Debug_Material -- returns a pointer to the default debug mtl				  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
VertexMaterialClass * WW3D::Peek_Default_Debug_Material()
{
#ifdef WWDEBUG
	WWASSERT(DefaultDebugMaterial);
	return DefaultDebugMaterial;
#else
	return nullptr;
#endif
}

/***********************************************************************************************
 * WW3D::Peek_Default_Debug_Shader -- returns the default shader for debugging.	              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
ShaderClass	WW3D::Peek_Default_Debug_Shader()
{
	return DefaultDebugShader;
}

/***********************************************************************************************
 * WW3D::Peek_Lightmap_Debug_Shader -- returns the shader for lightmap debugging.              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
ShaderClass	WW3D::Peek_Lightmap_Debug_Shader()
{
	return LightmapDebugShader;
}

/***********************************************************************************************
 * WW3D::Allocate_Debug_Resources -- allocates the debug resources									  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Allocate_Debug_Resources()
{
#ifdef WWDEBUG
	WWASSERT(DefaultDebugMaterial == nullptr);
	DefaultDebugMaterial = W3DNEW VertexMaterialClass;
	DefaultDebugMaterial->Set_Shininess(0.0f);
	DefaultDebugMaterial->Set_Opacity(1.0f);
	DefaultDebugMaterial->Set_Ambient(0,0,0);
	DefaultDebugMaterial->Set_Diffuse(0,0,0);
	DefaultDebugMaterial->Set_Specular(0,0,0);
	DefaultDebugMaterial->Set_Emissive(0,0,0);
#endif
}

/***********************************************************************************************
 * WW3D::Release_Debug_Resources -- releases the debug resources										  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/21/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3D::Release_Debug_Resources()
{
#ifdef WWDEBUG
	WWASSERT(DefaultDebugMaterial);
	REF_PTR_RELEASE(DefaultDebugMaterial);
#endif
}








void WW3D::Get_Pixel_Center(float &x, float &y)
{
	x = PixelCenterX; y = PixelCenterY;
}


void WW3D::Update_Pixel_Center()
{
	// The active backend uses the half-pixel convention of the original W3D
	// renderer.  Keep this policy in the W3D coordinate contract rather than
	// querying a graphics API-specific device name.
	PixelCenterX = 0.5f;
	PixelCenterY = 0.5f;
}





void WW3D::Enable_Sorting(bool onoff)
{
	IsSortingEnabled = onoff;
	// Have to invalidate mesh rendering system because
	// meshes are put into different fvfs depending on their sort state
	Graphics::Get_Prop_Submission().Clear();
}
