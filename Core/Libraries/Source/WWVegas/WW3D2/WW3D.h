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
 *                     $Archive:: /Commando/Code/ww3d2/WW3D.h                                 $*
 *                                                                                             *
 *                      $Author:: Steve_t                                                     $*
 *                                                                                             *
 *                     $Modtime:: 1/02/02 4:17p                                               $*
 *                                                                                             *
 *                    $Revision:: 42                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "WWLib/always.h"
#include "WWMath/vector3.h"
#include "WW3D2/W3DErr.h"
import Graphics.RHI;
import Graphics.Scene.ObjectList;
class RenderObjClass;

class		SceneClass;
class		CameraClass;
class		ShaderClass;

struct	RenderStatistics;
class		VertexMaterialClass;
class		ExtraMaterialPassClass;
class		RenderInfoClass;
class		StringClass;
class		MaterialPassClass;

#define MESH_RENDER_SNAPSHOT_ENABLED
#define SNAPSHOT_SAY(x) if (WW3D::Is_Snapshot_Activated()) { WWDEBUG_SAY(x); }
//#define SNAPSHOT_SAY(x)

/**
** WW3D
**
** This is the collection of static functions and data which initialize and
** control the behavior of the WW3D library.
*/
class WW3D
{
public:


	enum PrelitModeEnum {
		PRELIT_MODE_VERTEX,
		PRELIT_MODE_LIGHTMAP_MULTI_PASS,
		PRELIT_MODE_LIGHTMAP_MULTI_TEXTURE
	};

	enum MeshDrawModeEnum {
		MESH_DRAW_MODE_OLD,
		MESH_DRAW_MODE_NEW,
		MESH_DRAW_MODE_DEBUG_DRAW,
		MESH_DRAW_MODE_DEBUG_CLIP,
		MESH_DRAW_MODE_DEBUG_BOX,
		MESH_DRAW_MODE_NONE
	};

	enum NPatchesGapFillingModeEnum {
		NPATCHES_GAP_FILLING_DISABLED,
		NPATCHES_GAP_FILLING_ENABLED,
		NPATCHES_GAP_FILLING_FORCE
	};

	static WW3DErrorType		Init(bool lite = false);
	static WW3DErrorType		Shutdown();
	static bool					Is_Initted()								{ return IsInitted; }




	static void					Get_Pixel_Center(float &x, float &y);

	static void					Set_Preserve_FPU(bool preserve) { PreserveFPU = preserve; }
	static bool					Get_Preserve_FPU() { return PreserveFPU; }



	// 0 = bilinear, 1 = trilinear, 2 = anisotropic


	/*
	** Rendering functions
	** Each frame should be bracketed by a Begin_Render and End_Render call.  Between these two calls you will
	** normally render scenes.  The render function which accepts a single render object is implemented for
	** special cases like generating a shadow texture for an object.  Basically this function will have the
	** entire scene rendering overhead.
	*/
	static WW3DErrorType		Begin_Render(bool clear = false,bool clearz = true,const Vector3 & color = Vector3(0,0,0), float dest_alpha=0.0f, void(*network_callback)() = nullptr);
	static WW3DErrorType		Render(SceneClass * scene,CameraClass * cam,bool clear = false,bool clearz = false,const Vector3 & color = Vector3(0,0,0));
	// Submit one scene to the already-active backend frame. This is used by
	// off-screen passes and deliberately does not begin/end a frame, present,
	// or change render attachments.
	static WW3DErrorType		Render_Scene_Pass(SceneClass * scene,CameraClass * cam,
		const Graphics::RHIViewport *viewport_override = nullptr);
	static WW3DErrorType		Render(RenderObjClass & obj,RenderInfoClass & rinfo);
	static void					Flush(RenderInfoClass & rinfo);	// NOTE: "normal" usage should *NEVER* require the user to call this function

	static WW3DErrorType		End_Render();

	static bool					Is_Rendering() { return( IsRendering ); }
	static unsigned &			Reflection_Pass_Depth()
	{
		static unsigned depth = 0;
		return depth;
	}
	static bool					Is_Reflection_Render_Pass() { return Reflection_Pass_Depth() != 0; }

	// Scoped render context used by off-screen reflection submission. This is
	// intentionally separate from ShaderClass state so scene policy and
	// raster winding cannot leak across frames or materials.
	class ReflectionRenderPassScope final
	{
	public:
		ReflectionRenderPassScope() { ++WW3D::Reflection_Pass_Depth(); }
		~ReflectionRenderPassScope() { --WW3D::Reflection_Pass_Depth(); }
		ReflectionRenderPassScope(const ReflectionRenderPassScope &) = delete;
		ReflectionRenderPassScope &operator=(const ReflectionRenderPassScope &) = delete;
	};


	// TheSuperHackers @info Add amount of milliseconds that the simulation has advanced in this render frame.
	// This can be a fraction of a logic step.
	static void Update_Logic_Frame_Time(float milliseconds);
	/*
	** Timing
	** By calling the Sync function, the application can move the ww3d library time forward.  This
	** will control things like animated uv-offset mappers and render object animations.
	*/
	static void						Sync(bool step);

	// Total sync time in milliseconds. Advances in full logic time steps only.
	static unsigned int		Get_Sync_Time() { return SyncTime; }

	// Current sync frame time in milliseconds. Can be zero when the logic has not stepped forward in the current render update.
	static unsigned int		Get_Sync_Frame_Time() { return SyncTime - PreviousSyncTime; }

	// Fractional sync frame time. Accumulates for as long as the sync frame is not stepped forward.
	static unsigned int		Get_Fractional_Sync_Milliseconds() { return (unsigned int)FractionalSyncMs; }

	// Total logic time in milliseconds. Can include fractions of a logic step. Is rounded to integer.
	static unsigned int		Get_Logic_Time_Milliseconds() { return SyncTime + (unsigned int)FractionalSyncMs; }

	// Logic time step in milliseconds. Can be a fraction of a logic step.
	static float					Get_Logic_Frame_Time_Milliseconds() { return LogicFrameTimeMs; }

	// Logic time step in seconds. Can be a fraction of a logic step.
	static float					Get_Logic_Frame_Time_Seconds() { return LogicFrameTimeMs * 0.001f; }

	// Returns the render frame count.
	static unsigned int		Get_Frame_Count() { return FrameCount; }


	static void					_Invalidate_Mesh_Cache();
	static void					_Invalidate_Textures();


	static void					Enable_Sorting(bool onoff);
	static bool					Is_Sorting_Enabled()					{ return IsSortingEnabled; }


	static void					Set_Default_Native_Screen_Size(float dnss)	{ DefaultNativeScreenSize = dnss; }
	static float				Get_Default_Native_Screen_Size()			{ return DefaultNativeScreenSize; }


	static VertexMaterialClass *	Peek_Default_Debug_Material();
	static ShaderClass		Peek_Default_Debug_Shader();
	static ShaderClass		Peek_Backface_Debug_Shader();
	static ShaderClass		Peek_Lightmap_Debug_Shader();

	static void					Set_Prelit_Mode (PrelitModeEnum mode)			{ PrelitMode = mode; }
	static PrelitModeEnum 	Get_Prelit_Mode ()									{ return (PrelitMode); }
	static bool					Supports_Prelit_Mode (PrelitModeEnum mode)	{ return (true); }
	static void					Expose_Prelit (bool onoff)							{ ExposePrelit = onoff; }
	static bool					Expose_Prelit ()										{ return (ExposePrelit); }



	static void					Set_Mesh_Draw_Mode (MeshDrawModeEnum mode)	{ MeshDrawMode = mode; }
	static MeshDrawModeEnum Get_Mesh_Draw_Mode ()								{ return (MeshDrawMode); }

	static void					Set_NPatches_Gap_Filling_Mode (NPatchesGapFillingModeEnum mode);
	static NPatchesGapFillingModeEnum 	Get_NPatches_Gap_Filling_Mode () { return (NPatchesGapFillingMode); }

	static void					Set_NPatches_Level(unsigned level);
	static unsigned			Get_NPatches_Level() { return NPatchesLevel; }

	static void					Enable_Texturing(bool b);
	static bool					Is_Texturing_Enabled() { return IsTexturingEnabled; }
	static bool					Is_Coloring_Enabled() { return (IsColoringEnabled == 0) ? false : true; }
	static void					Enable_Coloring(unsigned int color);	///<when non-zero color is passed, it will override vertex colors

	static int					Get_Last_Frame_Memory_Allocation_Count() { return LastFrameMemoryAllocations; }
	static int					Get_Last_Frame_Memory_Free_Count() { return LastFrameMemoryFrees; }

	// Preserve authored sort levels while importing materials.
	static void					Enable_Munge_Sort_On_Load(bool onoff)	{ MungeSortOnLoad=onoff; }
	static bool					Is_Munge_Sort_On_Load_Enabled()		{ return MungeSortOnLoad; }

	/*
	** Overbright modify on load - when this mode is set meshes will be
	** modified at load time. All shaders which originally had the primary
	** gradient set to MODULATE will be changed to MODULATE2X instead.
	*/
	static void					Enable_Overbright_Modify_On_Load(bool onoff)	{ OverbrightModifyOnLoad = onoff; }
	static bool					Is_Overbright_Modify_On_Load_Enabled()	{ return OverbrightModifyOnLoad; }

	static bool					Is_Snapshot_Activated()						{ return SnapshotActivated; }
	static void					Activate_Snapshot(bool b)					{ SnapshotActivated=b; }

	// These clock all the time under user control, and are used to update
   // Stats.UserStat* when performance sampling is enabled.
   static long             UserStat0;
   static long             UserStat1;
   static long             UserStat2;

	// Gamma control

private:

	enum
	{
		DEFAULT_RESOLUTION_WIDTH =			640,
		DEFAULT_RESOLUTION_HEIGHT =		480,
		DEFAULT_BIT_DEPTH =					16
	};

	static void					Update_Pixel_Center();
	static void					Allocate_Debug_Resources();
	static void					Release_Debug_Resources();

	// Logic frame time, in milliseconds
	static float LogicFrameTimeMs;

	// Accumulated synchronized frame time in milliseconds
	static float FractionalSyncMs;

	// Timing info:
	// The absolute synchronized frame time (in milliseconds) supplied by the
	// application at the start of every frame. Note that wraparound cases
	// etc. need to be considered.
	static unsigned int SyncTime;

	// The previously set absolute sync time - this is used to get the interval between
	// the most recently set sync time and the previous one. Assuming the
	// application sets sync time at the start of every frame, this represents
	// the frame interval.
	static unsigned int PreviousSyncTime;

	static float						PixelCenterX;
	static float						PixelCenterY;


	static bool							IsInitted;
	static bool					PreserveFPU;
	static bool							IsRendering;
	static bool							IsSortingEnabled;
	static bool							IsBackfaceDebugEnabled;

	static bool							MungeSortOnLoad;

	static bool							OverbrightModifyOnLoad;

	static int							FrameCount;

	static VertexMaterialClass *	DefaultDebugMaterial;
	static VertexMaterialClass *	BackfaceDebugMaterial;
	static ShaderClass				DefaultDebugShader;
	static ShaderClass				LightmapDebugShader;

	static PrelitModeEnum			PrelitMode;
	static bool							ExposePrelit;


	static bool							SnapshotActivated;

	static MeshDrawModeEnum			MeshDrawMode;
	static NPatchesGapFillingModeEnum NPatchesGapFillingMode;
	static unsigned NPatchesLevel;
	static bool							IsTexturingEnabled;
	static bool							IsColoringEnabled;

	static bool							Lite;

	// This is the default native screen size which will be set for each
	// RenderObject on construction. The native screen size is the screen size
	// at which the object was designed to be viewed, and it is used in the
	// texture resizing algorithm (may be used in future for other things).
	// If the default is overridden, it will usually be in the asset manager
	// post-load callback.
	static float						DefaultNativeScreenSize;

	// For meshes which have a static sorting order. These will get drawn
	// after opaque meshes and before normally sorted meshes. The 'current'
	// pointer is so the application can temporarily set a different set of
	// static sort lists to be used temporarily. This is for specialised uses.

	// Memory allocation statistics
	static int							LastFrameMemoryAllocations;
	static int							LastFrameMemoryFrees;
};


/*
** RenderStatistics
** This struct holds the results of a performance sampling.  The WW3D object returns
** its statistics packaged up in one of these structures.
*/
struct RenderStatistics
{
		// General statistics
		double	ElapsedSeconds;
      int      FramesRendered;

		// Geometry engine statistics
		double	TrianglesReceived;
		double	TrianglesSubmitted;
		double	TrianglesSorted;
		double	VerticesReceived;
		double	VerticesSubmitted;

		// State change statistics
		double	ViewStateChanges;
		double	DrawStateChanges;
		double	TextureChanges;
		double	TextureParameterChanges;
		double	TexturesCreated;
		double	PaletteChanges;
		double	ShaderChanges;
		double	DrawCommands;
		double	TrianglesClipRemoved;
		double	TrianglesClipCreated;
		double	DeviceDriverCalls;

		// Rendering device statistics
		double	TextureTransfers;
		double	PixelsDrawn;
		double	PixelsRejected;

		// Surface cache statistics
		long		Hits;
		long		Misses;
		long		Insertions;
		long		Removals;
		long		MemUsed;
		long		MaxMemory;

      // User stats (can be used to see how often a function is called, etc.)
      long     UserStat0;
      long     UserStat1;
      long     UserStat2;
};
