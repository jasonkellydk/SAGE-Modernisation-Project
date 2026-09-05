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

// FILE: W3DWater.cpp /////////////////////////////////////////////////////////////////////////////
// Created:   Mark Wilczynski, June 2001
// Desc:      Draw reflective water surface.  Also handles drawing of waves/ripples
//			  on the surface.
///////////////////////////////////////////////////////////////////////////////////////////////////

#define SCROLL_UV

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DWaterTracks.h"
#include "W3DDevice/GameClient/WaterResources.h"
#include "W3DDevice/GameClient/WaterSkyboxSystem.h"
#include "WW3D2/Texture.h"
#include "WW3D2/RInfo.h"
#include "WW3D2/Camera.h"
#include "WW3D2/Scene.h"
#include "WW3D2/Backend/RenderBackend.h"
#include "WW3D2/WW3D.h"
#include "WW3D2/Light.h"
#include "WWMath/matrix4.h"
#include "WWLib/simplevec.h"
#include "WW3D2/Statistics.h"

#include "Common/FramePacer.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "Common/GameLOD.h"

#include "GameClient/Color.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DPoly.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <SDL3/SDL.h>
import Graphics.Scene.Lighting.Environment;



// DEFINES ////////////////////////////////////////////////////////////////////////////////////////
#define SKYPLANE_SIZE	(384.0f*MAP_XY_FACTOR)
#define SKYPLANE_HEIGHT	(30.0f)

#define SKYBODY_TEXTURE	"TSMoonLarg.tga"
#define SKYBODY_SIZE	45.0f		//extent or radius of sky body

#define SKYBODY_X	150.0f	//location of skybody
#define SKYBODY_Y	550.0f	//location of skybody

/* in the bay
#define SKYBODY_X	120.0f			//location of skybody
#define SKYBODY_Y	75.0f			//location of skybody
*/

#define SKYBODY_HEIGHT	SKYPLANE_HEIGHT	//altitude of sky body (z-buffer disabled, so can equal sky height).

//GeForce3 water system defines
#define PATCH_SIZE 15		//number of vertices on patch edge.  Large patches may waste vertices off edge of screen.
#define PATCH_UV_TILES	42	//number of times the bump map texture is tiled across patch (must be integer!).
#define PATCH_SCALE (4.0f * MAP_XY_FACTOR)	//horizontal scale factor. Adjust this and size to get desired vertex density.
#define SEA_REFLECTION_SIZE 256		//dimensions of reflection texture

#define BUMP_SIZE (50.f)
#define REFLECTION_FACTOR 0.1f

#define PATCH_WIDTH (PATCH_SIZE-1)	//internal defines
#define PATCH_UV_SCALE	((Real)PATCH_UV_TILES/(Real)PATCH_WIDTH)

//3D Grid Mesh Water defines.
#define WATER_MESH_OPACITY		0.5f
#define WATER_MESH_X_VERTICES	128
#define WATER_MESH_Y_VERTICES	128
#define WATER_MESH_SPACING	MAP_XY_FACTOR	//same as terrain

#define WATER_MESH_FVF	RenderBackendVertexFormat::PositionNormalDiffuseTexture2

#define DRAW_WATER_WAKES
/// @todo: Fix clipping of objects that intersect the mirror surface

WaterRenderSystem *TheWaterRenderSystem=nullptr; ///<global water rendering system

void doSkyBoxSet(Bool startDraw)
{
	if (TheWritableGlobalData)
		TheWritableGlobalData->m_drawSkyBox = startDraw;
}


#define DONUT_SIDES	90
#define INNER_RADIUS 200.0f
#define OUTER_RADIUS 250.0f
#define TEXTURE_REPEAT_COUNT 16
#define DONUT_HEIGHT	15.0f
//#define DO_FLAT_DONUT
#define AMP_SCALE	(30.0f/120.0f)
#define WAVE_FREQ	0.3f
#define AMP_SCALE2	(10.0f/120.0f)
#define NOISE_FREQ	(2.0f*PI/WAVE_FREQ)

#define NOISE_REPEAT_FACTOR ((float)(1.0f/(16.0f)))


static Bool wireframeForDebug = 0;

static Matrix4x4 Make_Scaling(float x, float y, float z)
{
	return Matrix4x4(
		x, 0.0f, 0.0f, 0.0f,
		0.0f, y, 0.0f, 0.0f,
		0.0f, 0.0f, z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
}

static Matrix4x4 Make_Translation(float x, float y, float z)
{
	return Matrix4x4(
		1.0f, 0.0f, 0.0f, x,
		0.0f, 1.0f, 0.0f, y,
		0.0f, 0.0f, 1.0f, z,
		0.0f, 0.0f, 0.0f, 1.0f);
}

//-------------------------------------------------------------------------------------------------
/** Destructor. Releases w3d assets. */
//-------------------------------------------------------------------------------------------------
WaterRenderSystem::~WaterRenderSystem()
{
	REF_PTR_RELEASE(m_skyBodyTexture);
	delete m_skyBox;
	m_skyBox = nullptr;

	REF_PTR_RELEASE (m_riverTexture);
	REF_PTR_RELEASE (m_whiteTexture);
	REF_PTR_RELEASE (m_waterNoiseTexture);
	REF_PTR_RELEASE (m_waterOceanHeightTexture);
	REF_PTR_RELEASE (m_waterOceanNormalTexture);
	REF_PTR_RELEASE (m_waterEnvironmentTexture);
	REF_PTR_RELEASE (m_waterCausticsTexture);
	REF_PTR_RELEASE (m_waterDepthLutTexture);
	REF_PTR_RELEASE (m_riverAlphaEdge);
	REF_PTR_RELEASE (m_waterSparklesTexture);

	Int i;

	for(i=0; i<TIME_OF_DAY_COUNT; i++)
	{	REF_PTR_RELEASE(m_settings[i].skyTexture);
		REF_PTR_RELEASE(m_settings[i].waterTexture);
	}

	// Bump-map handles are released by ReleaseResources while the backend is
	// still available.

	//Release strings allocated inside global water settings.
	for  (i=0; i<TIME_OF_DAY_COUNT; i++)
	{	WaterSettings[i].m_skyTextureFile.clear();
		WaterSettings[i].m_waterTextureFile.clear();
	}
	deleteInstance((WaterTransparencySetting*)TheWaterTransparency.getNonOverloadedPointer());
	TheWaterTransparency = nullptr;
	ReleaseResources();

	delete m_waterTrackSystem;
}

//-------------------------------------------------------------------------------------------------
/** Constructor. Just nulls out some variables. */
//-------------------------------------------------------------------------------------------------
WaterRenderSystem::WaterRenderSystem()
{
	memset( &m_settings, 0, sizeof( m_settings ) );
	m_dx=0;
	m_dy=0;
	m_worldPositionX=0;
	m_worldPositionY=0;
	m_worldPositionZ=0;
	m_waterTrackSystem = nullptr;
	m_skyBodyTexture=nullptr;
	m_useCloudLayer=true;
	m_waterType = WATER_TYPE_SURFACE;
	m_tod=TIME_OF_DAY_AFTERNOON;
	m_pReflectionTexture=nullptr;
	m_pRefractionTexture=nullptr;
	m_pSceneDepthTexture=nullptr;
	m_pDisplacementTexture=nullptr;
	m_renderingOffscreen=FALSE;
	m_reflectionRenderer=nullptr;
	m_skyBox=nullptr;

	m_riverVOrigin=0;
	m_waterTime=0;
	m_riverTexture=nullptr;
	m_whiteTexture=nullptr;
	m_waterNoiseTexture=nullptr;
	m_waterOceanHeightTexture=nullptr;
	m_waterOceanNormalTexture=nullptr;
	m_waterEnvironmentTexture=nullptr;
	m_waterCausticsTexture=nullptr;
	m_waterDepthLutTexture=nullptr;
	m_riverAlphaEdge=nullptr;
	m_waterSparklesTexture=nullptr;
	m_riverXOffset=0;
	m_riverYOffset=0;
}

//-------------------------------------------------------------------------------------------------
/** WW3D method that returns object bounding sphere used in frustum culling*/
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::Set_Surface_Geometry(const WaterGeometry &geometry)
{
	m_surfaceGeometry = geometry;
}

void WaterRenderSystem::Set_Grid_Render_Data(const WaterGridRenderData &data)
{
	const bool geometry_changed =
		m_gridRenderData.cells_x != data.cells_x ||
		m_gridRenderData.cells_y != data.cells_y ||
		m_gridRenderData.enabled != data.enabled;
	m_gridRenderData = data;
	if (geometry_changed)
		Rebuild_Grid_Geometry();
}

void WaterRenderSystem::Set_World_Position(Real x, Real y, Real z)
{
	m_worldPositionX = x;
	m_worldPositionY = y;
	m_worldPositionZ = z;
}

void WaterRenderSystem::Rebuild_Grid_Geometry()
{
    m_gridVertices.clear();
    m_gridIndices.clear();
    m_numVertices = m_numIndices = 0;
    generateIndexBuffer(m_gridRenderData.cells_x+1,m_gridRenderData.cells_y+1);
    generateVertexBuffer(m_gridRenderData.cells_x+1,m_gridRenderData.cells_y+1,false);
}

//-------------------------------------------------------------------------------------------------
/** Creates and optionally fills the backend-owned water vertex buffer. */
//-------------------------------------------------------------------------------------------------
bool WaterRenderSystem::generateVertexBuffer(Int sizeX, Int sizeY, Bool doStatic)
{
    if (sizeX < 2 || sizeY < 2 || sizeX > 65535/sizeY) return false;
    m_numVertices = sizeX*sizeY;
    m_gridVertices.resize(m_numVertices);
    if (!doStatic) return true;
    for (Int z=0;z<sizeY;++z) for (Int x=0;x<sizeX;++x) {
        auto& vertex = m_gridVertices[z*sizeX+x];
        vertex = {};
        vertex.x = static_cast<float>(x);
        vertex.y = m_level;
        vertex.z = static_cast<float>(z);
        vertex.nz = 1;
        vertex.u1 = static_cast<float>(x)*PATCH_UV_SCALE;
        vertex.v1 = static_cast<float>(z)*PATCH_UV_SCALE;
        vertex.diffuse = m_settings[m_tod].transparentWaterDiffuse;
    }
    return Upload_Water_Geometry(m_gridMesh,m_gridVertices,m_gridIndices,true);
}

//-------------------------------------------------------------------------------------------------
/** Creates and fills the backend-owned water index buffer. */
//-------------------------------------------------------------------------------------------------
bool WaterRenderSystem::generateIndexBuffer(Int sizeX, Int sizeY)
{
    if (sizeX < 2 || sizeY < 2 || sizeX > 65535/sizeY) return false;
    m_numIndices = (sizeY-1)*(sizeX*2+2)-2;
    m_gridIndices.resize(m_numIndices);
    UnsignedShort* indices = m_gridIndices.data();
	Int index = 0;
	Int next_row_index = 0;
	for (Int row = 0; index < m_numIndices; ++row)
	{
		for (; next_row_index < sizeX * (row + 1) && index < m_numIndices;
			++next_row_index, index += 2)
		{
			indices[index] = static_cast<UnsignedShort>(next_row_index + sizeX);
			indices[index + 1] = static_cast<UnsignedShort>(next_row_index);
		}
		if (index < m_numIndices)
		{
			indices[index] = static_cast<UnsignedShort>(next_row_index - 1);
			indices[index + 1] = static_cast<UnsignedShort>(next_row_index + sizeX);
			index += 2;
		}
	}

    return true;
}

std::uint32_t WaterRenderSystem::getSurfaceDiffuse(bool reduce_alpha) const
{
	// Surface water starts at the authored transparent-water opacity.  Keep the
	// normal water RGB so the material still uses the time-of-day color,
	// but do not seed the thickness interpolation with the opaque diffuse alpha.
	const std::uint32_t diffuse =
		m_settings[m_tod].waterDiffuse & 0x00ffffffu;
	unsigned alpha = (m_settings[m_tod].transparentWaterDiffuse >> 24) & 0xffu;
	if (reduce_alpha)
	{
		alpha = alpha > 0x20u ? alpha - 0x20u : 0u;
	}
	return diffuse | (alpha << 24);
}

//-------------------------------------------------------------------------------------------------
/**
 * Ensures that the shared surface submission buffers can hold one complete
 * river, trapezoid, or sky draw.  The buffers belong to the backend and are
 * deliberately independent from the old WW3D dynamic access classes.
 */


//-------------------------------------------------------------------------------------------------
/** Uploads one complete water draw packet into the shared buffers. */
bool WaterRenderSystem::uploadSurfaceGeometry(const WaterSurfaceVertex* vertices, unsigned vertex_count,
    const UnsignedShort* indices, unsigned index_count)
{
    return Upload_Water_Geometry(m_surfaceMesh,{vertices,vertex_count},{indices,index_count});
}



//-------------------------------------------------------------------------------------------------
/** Releases all backend resources, to prepare for a reset. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::ReleaseResources()
{
	m_waterMaterial.Shutdown();

	REF_PTR_RELEASE(m_pReflectionTexture);
	REF_PTR_RELEASE(m_pRefractionTexture);
	REF_PTR_RELEASE(m_pSceneDepthTexture);
	REF_PTR_RELEASE(m_pDisplacementTexture);
	m_renderingOffscreen = FALSE;

    auto& renderer = Graphics::Get_Water_Renderer();
    for (auto mesh : {m_gridMesh,m_surfaceMesh,m_displacementMesh}) renderer.Destroy_Mesh(mesh);
    m_gridMesh = m_surfaceMesh = m_displacementMesh = {};
    m_gridVertices.clear();
    m_gridIndices.clear();

	if (m_waterTrackSystem)
		m_waterTrackSystem->ReleaseResources();

}

//-------------------------------------------------------------------------------------------------
/** Recreates all backend-owned water resources after a device reset. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::ReAcquireResources()
{
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
	{
		return;
	}
	m_waterMaterial.ReacquireResources();

	// Both water mesh variants share the same grid index buffer.
	if (m_gridRenderData.enabled)
	{
		if (!generateIndexBuffer(m_gridRenderData.cells_x + 1,
			m_gridRenderData.cells_y + 1) ||
			!generateVertexBuffer(m_gridRenderData.cells_x + 1,
				m_gridRenderData.cells_y + 1, false))
		{
			return;
		}
	}
	else if (m_waterType == WATER_TYPE_OCEAN)
	{
		if (!generateIndexBuffer(PATCH_SIZE, PATCH_SIZE) ||
			!generateVertexBuffer(PATCH_SIZE, PATCH_SIZE, true))
		{
			return;
		}

	}

	// The water type selects geometry only. Every mode uses the same // reflection/refraction material contract.
	m_pReflectionTexture = backend->Create_Render_Target(
		SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE);
	m_pDisplacementTexture = backend->Create_Render_Target(
		SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, WW3D_FORMAT_A8R8G8B8);

	int target_width = 0;
	int target_height = 0;
	int target_bits = 0;
	bool target_windowed = false;
	backend->Get_Render_Target_Resolution(target_width, target_height,
		target_bits, target_windowed);
	if (target_width > 0 && target_height > 0)
	{
		m_pRefractionTexture = backend->Create_Render_Target(
			target_width, target_height);
	}
	if (m_pSceneDepthTexture == nullptr)
	{
		m_pSceneDepthTexture = backend->Create_Scene_Depth_Texture();
	}

	if (m_waterTrackSystem != nullptr)
	{
		m_waterTrackSystem->ReAcquireResources();
	}

	// Textures are managed by the W3D texture layer, but may need to be
	// initialized again after the backend recreates its resources.
	if (m_riverTexture != nullptr && !m_riverTexture->Is_Initialized())
		m_riverTexture->Init();
	if (m_waterNoiseTexture != nullptr && !m_waterNoiseTexture->Is_Initialized())
		m_waterNoiseTexture->Init();
	if (m_waterOceanHeightTexture != nullptr &&
		!m_waterOceanHeightTexture->Is_Initialized())
		m_waterOceanHeightTexture->Init();
	if (m_waterOceanNormalTexture != nullptr &&
		!m_waterOceanNormalTexture->Is_Initialized())
		m_waterOceanNormalTexture->Init();
	if (m_waterEnvironmentTexture != nullptr &&
		!m_waterEnvironmentTexture->Is_Initialized())
		m_waterEnvironmentTexture->Init();
	if (m_waterCausticsTexture != nullptr &&
		!m_waterCausticsTexture->Is_Initialized())
		m_waterCausticsTexture->Init();
	Reinitialize_Water_Procedural_Texture(m_waterDepthLutTexture, true);
	if (m_riverAlphaEdge != nullptr && !m_riverAlphaEdge->Is_Initialized())
		m_riverAlphaEdge->Init();
	if (m_waterSparklesTexture != nullptr && !m_waterSparklesTexture->Is_Initialized())
		m_waterSparklesTexture->Init();
	Reinitialize_Water_Procedural_Texture(m_whiteTexture, false);
}



void WaterRenderSystem::load()
{
	if (m_waterTrackSystem)
		m_waterTrackSystem->loadTracks();
}

//-------------------------------------------------------------------------------------------------
/** Initializes water with dimensions and parent scene.
	* During rendering, we will render a water surface of given dimensions
	* and reflect the parent scene in its surface.  For now, waters are
	* forced to be rectangles. */
//-------------------------------------------------------------------------------------------------
Int WaterRenderSystem::init(Real waterLevel, Real dx, Real dy,
	WaterReflectionRenderer *reflectionRenderer, WaterType type)
{

	m_dx=dx;
	m_dy=dy;
	m_level=waterLevel;

	m_LastUpdateTime=SDL_GetTicks();
	m_uScrollPerMs=0.001f;
	m_vScrollPerMs=0.001f;
	m_uOffset=0;
	m_vOffset=0;

	m_reflectionRenderer=reflectionRenderer;
	m_waterType = type;
	m_waterTime = 0.0f;

	/// Hack for now
	// WaterType now selects geometry only; the material is always .

	//
	// assign the data from the WaterSettings[] global to the data for this
	// render system (we at present only have one water plane)
	//
	loadSetting( &m_settings[ TIME_OF_DAY_MORNING ], TIME_OF_DAY_MORNING );
	loadSetting( &m_settings[ TIME_OF_DAY_AFTERNOON ], TIME_OF_DAY_AFTERNOON );
	loadSetting( &m_settings[ TIME_OF_DAY_EVENING ], TIME_OF_DAY_EVENING );
	loadSetting( &m_settings[ TIME_OF_DAY_NIGHT ], TIME_OF_DAY_NIGHT );

	ReAcquireResources();
// The legacy bump-map loading path was removed; resources are backend-owned.


	//Assets used for all types of water
	m_skyBodyTexture=Load_Water_Texture(SKYBODY_TEXTURE);

	m_skyBox = NEW WaterSkyboxSystem;
	m_skyBox->Initialize(TheGlobalData->m_skyBoxScale);

	m_riverTexture=Load_Water_Texture(TheWaterTransparency->m_standingWaterTexture.str());

	//For some reason setting a null texture does not result in 0xffffffff for pixel shaders so using explicit "white" texture.
	m_whiteTexture = Create_Water_White_Texture();

	m_waterNoiseTexture=Load_Water_Texture("Noise0000.dds");
	m_waterOceanHeightTexture=Load_Water_Texture("wave256_height.dds");
	m_waterOceanNormalTexture=Load_Water_Texture("wave256_normalmap.dds");
	m_waterEnvironmentTexture=Load_Water_Texture("WaterOceanEnvironment.tga");
	m_waterCausticsTexture=Load_Water_Texture("caust00.tga");
	m_waterDepthLutTexture = Create_Water_Depth_Lut_Texture();
	m_riverAlphaEdge=Load_Water_Texture("TWAlphaEdge.dds");
	m_waterSparklesTexture=Load_Water_Texture("WaterOceanFoam.dds");
#ifdef DRAW_WATER_WAKES
	m_waterTrackSystem = NEW WaterTracksRenderSystem;
	m_waterTrackSystem->init();
#endif

	return 0;
}

void WaterRenderSystem::updateMapOverrides()
{
	if (m_riverTexture && TheWaterTransparency->m_standingWaterTexture.compareNoCase(m_riverTexture->Get_Texture_Name()) != 0)
	{
		REF_PTR_RELEASE(m_riverTexture);
		m_riverTexture = Load_Water_Texture(TheWaterTransparency->m_standingWaterTexture.str());
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void WaterRenderSystem::reset()
{
	if (m_waterTrackSystem)
		m_waterTrackSystem->reset();
}

// ------------------------------------------------------------------------------------------------
/** Update phase for water if we need it. */
// ------------------------------------------------------------------------------------------------
void WaterRenderSystem::update()
{
	// TheSuperHackers @tweak The water movement time step is now decoupled from the render update.
	const Real timeScale = TheFramePacer->getActualLogicTimeScaleOverFpsRatio();
	constexpr const Real MagicOffset = 0.0125f * 33 / 5000;

	m_waterTime += WW3D::Get_Logic_Frame_Time_Seconds();
	m_riverVOrigin += 0.002f * timeScale;
	m_riverXOffset += static_cast<Real>(MagicOffset * timeScale);
	m_riverYOffset += static_cast<Real>(2 * MagicOffset * timeScale);
	m_riverXOffset -= static_cast<Int>(m_riverXOffset);
	m_riverYOffset -= static_cast<Int>(m_riverYOffset);

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::replaceSkyboxTexture(const AsciiString& oldTexName, const AsciiString& newTextName)
{
	if (m_skyBox != nullptr)
		m_skyBox->Replace_Texture(oldTexName.str(), newTextName.str());

	// Texture sampler policy is applied by the resource boundary.


}

//-------------------------------------------------------------------------------------------------
/** Adjusts various water/sky rendering settings that depend on time of day. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::setTimeOfDay(TimeOfDay tod)
{
	m_tod=tod;
	if (m_waterType == WATER_TYPE_OCEAN)
		generateVertexBuffer(PATCH_SIZE,PATCH_SIZE,true);	//update the water mesh with new lighting/alpha
}

//-------------------------------------------------------------------------------------------------
/**Copies GDF settings dealing with a particular time of day into our own
	* structures.  Also allocates any required W3D assets (textures). */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::loadSetting( Setting *setting, TimeOfDay timeOfDay )
{
	// sanity
	DEBUG_ASSERTCRASH( setting, ("WaterRenderSystem::loadSetting, null setting") );

	// textures
	setting->skyTexture = Load_Water_Texture(
		WaterSettings[ timeOfDay ].m_skyTextureFile.str());
	setting->waterTexture = Load_Water_Texture( WaterSettings[ timeOfDay ].m_waterTextureFile.str() );

	// texelss per unit
	setting->skyTexelsPerUnit = WaterSettings[ timeOfDay ].m_skyTexelsPerUnit;
	setting->skyTexelsPerUnit /= static_cast<Real>(Get_Water_Texture_Width(setting->waterTexture));

	// water repeat
	setting->waterRepeatCount = WaterSettings[ timeOfDay ].m_waterRepeatCount;

	// U and V scroll per ms
	setting->uScrollPerMs = WaterSettings[ timeOfDay ].m_uScrollPerMs;
	setting->vScrollPerMs = WaterSettings[ timeOfDay ].m_vScrollPerMs;

	//
	// vertex colors
	//
	// bottom left
	setting->vertex00Diffuse = (WaterSettings[ timeOfDay ].m_vertex00Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex00Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex00Diffuse.blue;
	// top left
	setting->vertex01Diffuse = (WaterSettings[ timeOfDay ].m_vertex01Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex01Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex01Diffuse.blue;
	// bottom right
	setting->vertex10Diffuse = (WaterSettings[ timeOfDay ].m_vertex10Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex10Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex10Diffuse.blue;
	// top right
	setting->vertex11Diffuse = (WaterSettings[ timeOfDay ].m_vertex11Diffuse.red << 16) |
														 (WaterSettings[ timeOfDay ].m_vertex11Diffuse.green << 8) |
														  WaterSettings[ timeOfDay ].m_vertex11Diffuse.blue;

	// diffuse water color
	setting->waterDiffuse = (WaterSettings[ timeOfDay ].m_waterDiffuseColor.alpha << 24) |
												  (WaterSettings[ timeOfDay ].m_waterDiffuseColor.red		<< 16) |
													(WaterSettings[ timeOfDay ].m_waterDiffuseColor.green << 8) |
												   WaterSettings[ timeOfDay ].m_waterDiffuseColor.blue;

	// transparent water color
	setting->transparentWaterDiffuse = (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.alpha << 24) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.red	 << 16) |
																		 (WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.green << 8) |
																		  WaterSettings[ timeOfDay ].m_transparentWaterDiffuse.blue;

}

//-------------------------------------------------------------------------------------------------
/** Our water may use effects that require run-time rendered textures.  These
	*	textures need to be updated before we start rendering to the main screen
	* render target because the active backend exposes one render target at a time. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::updateRenderTargetTextures(CameraClass *cam)
{
	updateDisplacementTexture();
	if (m_pReflectionTexture != nullptr && getClippedWaterPlane(cam, nullptr) &&
		TheTerrainRenderObject && TheTerrainRenderObject->getMap())
		renderMirror(cam);	//generate texture containing reflected scene
}

bool WaterRenderSystem::updateDisplacementTexture()
{
	if (m_waterType != WATER_TYPE_OCEAN)
	{
		return true;
	}

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr || m_pDisplacementTexture == nullptr ||
		m_waterOceanHeightTexture == nullptr || m_dx <= 0.0f || m_dy <= 0.0f)
	{
		return false;
	}

	// The producer is a complete fullscreen programmable pass.  Its output
	// is the RA3 OceanDisplacement field consumed by the ocean vertex shader.
    if (!m_displacementMesh.Is_Valid()) {
        WaterSurfaceVertex quad[4] = {};
        const float xy[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        const float uv[4][2] = {{0,1},{0,0},{1,1},{1,0}};
        for (unsigned i=0;i<4;++i) {
            quad[i].x = xy[i][0]; quad[i].y = xy[i][1];
            quad[i].u1 = uv[i][0]; quad[i].v1 = uv[i][1];
            quad[i].diffuse = 0xffffffff;
        }
        const UnsignedShort indices[6] = {0,1,2,2,1,3};
        if (!Upload_Water_Geometry(m_displacementMesh,quad,indices)) return false;
    }

	const WaterMaterialParameters parameters =
		makeWaterMaterialParameters(false, false, false);
	RenderBackendPassScope displacement_pass(backend,
		m_pDisplacementTexture, nullptr, RenderBackendCullMode::None, true,
		false, Vector3(0.0f, 0.0f, 0.0f));
	if (!displacement_pass.Is_Active())
	{
		return false;
	}

	const bool applied = m_waterMaterial.Apply_Displacement(
		m_waterOceanHeightTexture, parameters.animation,
		parameters.displacement_domain);
	if (applied)
	{
		m_waterMaterial.Draw(m_displacementMesh);
	}
	m_waterMaterial.Reset();
	return applied;
}

void WaterRenderSystem::Capture_Refraction_Texture()
{
	if ((m_pRefractionTexture == nullptr && m_pSceneDepthTexture == nullptr) ||
		m_renderingOffscreen)
	{
		return;
	}

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend != nullptr)
	{
		// This is called after the opaque scene and before the water sort list.
		// The backend copies the active swap-chain color into a same-sized
		// shader resource, so the water shader can refract the actual scene below
		// the surface without rendering the scene a second time.
		if (m_pRefractionTexture != nullptr)
		{
			backend->Copy_Back_Buffer_To_Texture(
				m_pRefractionTexture->Peek_Render_Backend_Texture());
		}
		if (m_pSceneDepthTexture != nullptr)
		{
			backend->Capture_Scene_Depth();
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** Renders the reflected scene into an offscreen texture. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::renderMirror(CameraClass *cam)
{
#ifdef EXTENDED_STATS
	if (WW3D::Get_Render_Backend()->Get_Debug_Settings().m_disableWater) {
		return;
	}
#endif
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr || cam == nullptr || m_reflectionRenderer == nullptr ||
		m_pReflectionTexture == nullptr)
		return;

	const Matrix3D OldCameraMatrix = cam->Get_Transform();
	const Matrix4x4 camera_world(OldCameraMatrix);
	const Graphics::WaterView water_view(
		std::span<const float,16>(&camera_world[0][0],16),m_level);
	const auto& reflected = water_view.reflected_camera;
	Matrix3D reflectedTransform(
		reflected[0],reflected[1],reflected[2],reflected[3],
		reflected[4],reflected[5],reflected[6],reflected[7],
		reflected[8],reflected[9],reflected[10],reflected[11]);

	const RenderBackendCullMode old_cull_mode = backend->Get_Cull_Mode();
	const RenderBackendCullMode reflected_cull_mode =
		old_cull_mode == RenderBackendCullMode::Clockwise ?
		RenderBackendCullMode::CounterClockwise :
		old_cull_mode == RenderBackendCullMode::CounterClockwise ?
		RenderBackendCullMode::Clockwise : RenderBackendCullMode::None;
	RenderBackendPassScope reflection_pass(backend,
		m_pReflectionTexture, nullptr, reflected_cull_mode, false, true,
		Vector3(0.0f, 0.0f, 0.0f));
	if (!reflection_pass.Is_Active())
	{
		return;
	}

	cam->Set_Transform( reflectedTransform );

	//Force reflected image to be drawn into full texture size - not a viewport inside texture.
	Vector2 vMin,vMax,vOldMax,vOldMin;
 	cam->Get_Viewport(vOldMin,vOldMax);
 	vMax.X=vMax.Y=1.0f;
	vMin.X=vMin.Y=0.0f;
 	cam->Set_Viewport(vMin,vMax);
	const RenderBackendViewport &pass_viewport =
		reflection_pass.Get_Pass_Viewport();
	// Projective sampling uses the main camera's normalized coordinates.
	// Preserve its projection even when the reflection texture is square:
	// target dimensions change sampling density, not the camera's field of view.

	cam->Apply();	//force an update of all the camera dependent parameters like frustum clip planes
	RenderBackendViewport reflected_viewport = pass_viewport;
	cam->Get_Depth_Range(&reflected_viewport.min_z,
		&reflected_viewport.max_z);
	backend->Set_Viewport(reflected_viewport);

	// Submit the reflected scene inside the existing frame. The pass owns the
	// off-screen target and winding; scene submission never begins/ends a
	// frame, presents, or recursively switches render targets.
	m_renderingOffscreen = TRUE;
	const auto saved_clip_plane = Graphics::Get_Environment_Lighting().parameters.clip_plane;
	Graphics::Get_Environment_Lighting().parameters.clip_plane = water_view.reflection_clip_plane;
	renderSky();
	if (m_tod == TIME_OF_DAY_NIGHT)
		renderSkyBody(&reflectedTransform);

	m_reflectionRenderer->Render_Water_Reflection(cam, reflected_viewport);
	Graphics::Get_Environment_Lighting().parameters.clip_plane = saved_clip_plane;
	m_renderingOffscreen = FALSE;

	cam->Set_Transform(OldCameraMatrix);	//restore original non-reflected matrix
 	cam->Set_Viewport(vOldMin,vOldMax);
	reflection_pass.End();
	cam->Apply();	//restore camera-dependent parameters for the main target
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water.
	*	Algorithm:
	*	Draw reflected scene.
	*	Draw reflected sky layer(s) and bodies.
	*	Clear Zbuffer
	*	Fill Zbuffer by drawing water surface (allows proper sorting into regular scene).
	*	Draw non-reflected scene (done in regular app render loop).
	*
	*	This algorithm doesn't apply to translucent water, which is rendered into a
	*   texture and rendered at end of scene. */
//-------------------------------------------------------------------------------------------------
//DECLARE_PERF_TIMER(Water)
void WaterRenderSystem::Render(RenderInfoClass & rinfo)
{
    m_waterMaterial.Set_Fog(TheTerrainRenderObject ? TheTerrainRenderObject->Peek_Scene() : nullptr);
	//USE_PERF_TIMER(Water)
	if (TheTerrainRenderObject && !TheTerrainRenderObject->getMap())
		return;	//no map has been loaded yet.

#ifdef EXTENDED_STATS
	if (WW3D::Get_Render_Backend()->Get_Debug_Settings().m_disableWater) {
		return;
	}
#endif
	if (m_renderingOffscreen)
		return;	//the water object must not recursively render into its reflection.

	if (m_waterType == WATER_TYPE_OCEAN)
	{
		drawSea(rinfo);
	}
	else
	{
		// The remaining modes describe polygon/grid geometry only. All of them
		// use the same explicit RA3-style surface material.
		renderWater();
		if (!m_drawingRiver || m_gridRenderData.surface_override)
			renderWaterMesh();
	}

	if (TheGlobalData && TheGlobalData->m_drawSkyBox)
	{	//center skybox around camera
		Vector3 pos=rinfo.Camera.Get_Position();
		pos.Z = TheGlobalData->m_skyBoxPositionZ;
		if (m_skyBox != nullptr)
			m_skyBox->Render(rinfo, pos.X, pos.Y, pos.Z);
	}

	//Clean up after any pixel shaders.
	//Force render state application so the null texture releases the shroud reference.
	WW3D::Get_Render_Backend()->Apply_Render_State_Changes();
	WW3D::Get_Render_Backend()->Invalidate_Cached_Render_States();

	if (m_waterTrackSystem)
		m_waterTrackSystem->flush(rinfo);

//	renderWaterMesh();
//	renderWaterWave();
}

//-------------------------------------------------------------------------------------------------
/** Clips the water plane to the current camera frustum and returns a bounding
	* box enclosing the clipped plane.  Returns false if water plane is not visible. */
//-------------------------------------------------------------------------------------------------
Bool WaterRenderSystem::getClippedWaterPlane(CameraClass *cam, AABoxClass *box)
{
	const FrustumClass & frustum = cam->Get_Frustum();

	ClipPolyClass	ClippedPoly0;
	ClipPolyClass	ClippedPoly1;

	///@todo: generate proper sized polygon
	ClippedPoly0.Reset();
	ClippedPoly0.Add_Vertex(Vector3(0,0,m_level));
	ClippedPoly0.Add_Vertex(Vector3(0,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,m_dy,m_level));
	ClippedPoly0.Add_Vertex(Vector3(m_dx,0,m_level));

	//clip against all 6 frustum planes
	ClippedPoly0.Clip(frustum.Planes[0],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[1],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[2],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[3],ClippedPoly0);
	ClippedPoly0.Clip(frustum.Planes[4],ClippedPoly1);
	ClippedPoly1.Clip(frustum.Planes[5],ClippedPoly0);

	Int final_vcount = ClippedPoly0.Verts.Count();

	//make sure the polygon is visible
	if (final_vcount >= 3)
	{
		//find axis aligned bounding box around visible polygon
		if (box)
  			box->Init(&(ClippedPoly0.Verts[0]),final_vcount);
		return TRUE;
	}

	return FALSE;	//water plane is not visible
}

WaterMaterialParameters WaterRenderSystem::makeWaterMaterialParameters(
	bool river, bool reflection, bool underwater) const
{
	(void)river;
	WaterMaterialParameters parameters = {
		Vector4(0.0f, 0.0f, 0.0f, 0.0f),
		Vector4(m_uOffset, m_vOffset, m_waterTime, m_level),
		Vector4(0.0f, 0.0f, 1.0f, 1.0f),
		Vector4(m_worldPositionX, m_worldPositionY, m_dx, m_dy),
		Vector4(1.0f, 1.0f, 1.0f, 1.0f),
		Vector4(reflection ? REFLECTION_FACTOR : 0.0f,
			0.0f, m_pRefractionTexture != nullptr ? 1.0f : 0.0f,
			underwater ? 1.0f : 0.0f),
		Vector4(river ? 1.0f : 0.0f,
			TheWaterTransparency != nullptr ?
				TheWaterTransparency->m_transparentWaterDepth : 0.0f,
			TheWaterTransparency != nullptr ?
				TheWaterTransparency->m_minWaterOpacity : 1.0f,
			m_pSceneDepthTexture != nullptr ? 1.0f : 0.0f)};

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend != nullptr)
	{
		Matrix4x4 view;
		backend->Get_Transform(RenderBackendTransform::View, view);
		const Matrix4x4 camera_transform = view.Inverse();
		const Graphics::WaterView water_view(
			std::span<const float,16>(&camera_transform[0][0],16),m_level);
		const auto& position = water_view.camera_position;
		parameters.camera_position = Vector4(position[0],position[1],position[2],1);
		if (water_view.underwater)
			parameters.effects[3] = 1.0f;
	}

	W3DShroud *shroud = TheTerrainRenderObject == nullptr ? nullptr :
		TheTerrainRenderObject->getShroud();
	if (shroud != nullptr && shroud->getShroudTexture() != nullptr &&
		shroud->getCellWidth() > 0.0f && shroud->getCellHeight() > 0.0f &&
		shroud->getTextureWidth() > 0 && shroud->getTextureHeight() > 0)
	{
		const float scale_x = 1.0f /
			(static_cast<float>(shroud->getCellWidth()) *
				static_cast<float>(shroud->getTextureWidth()));
		const float scale_y = 1.0f /
			(static_cast<float>(shroud->getCellHeight()) *
				static_cast<float>(shroud->getTextureHeight()));
		parameters.shroud_projection = Vector4(scale_x, scale_y,
			(-static_cast<float>(shroud->getDrawOriginX()) +
				static_cast<float>(shroud->getCellWidth())) * scale_x,
			(-static_cast<float>(shroud->getDrawOriginY()) +
				static_cast<float>(shroud->getCellHeight())) * scale_y);
		parameters.effects[1] = 1.0f;
	}

	return parameters;
}

//-------------------------------------------------------------------------------------------------
/** Draws the water surface using custom backend vertex/pixel shaders and a
	* reflection texture.  Only tested to work on GeForce3. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::drawSea(RenderInfoClass & rinfo)
{
	AABoxClass sea_box;
	if (!getClippedWaterPlane(&rinfo.Camera, &sea_box))
	{
		return;
	}

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr || m_gridVertices.empty() ||
		m_gridIndices.empty())
	{
		return;
	}

	const Matrix4x4 coordinate_transform(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	const WaterMaterialParameters parameters =
		makeWaterMaterialParameters(false, m_pReflectionTexture != nullptr, false);
	W3DShroud *shroud = TheTerrainRenderObject == nullptr ? nullptr :
		TheTerrainRenderObject->getShroud();
	TextureBaseClass *foam_or_caustics = parameters.effects[3] > 0.5f &&
		m_waterCausticsTexture != nullptr ? m_waterCausticsTexture :
		m_waterSparklesTexture;
	TextureBaseClass *environment_or_depth = parameters.effects[3] > 0.5f &&
		m_waterDepthLutTexture != nullptr ? m_waterDepthLutTexture :
		m_waterEnvironmentTexture;
	if (environment_or_depth == nullptr)
		environment_or_depth = m_settings[m_tod].skyTexture;

	const Matrix4x4 patch_scale =
		Make_Scaling(PATCH_SCALE, 1.0f, PATCH_SCALE);
	for (Int patch_y = static_cast<Int>((sea_box.Center.Y - sea_box.Extent.Y) /
		(PATCH_WIDTH * PATCH_SCALE));
		patch_y * PATCH_WIDTH * PATCH_SCALE <
		sea_box.Center.Y + sea_box.Extent.Y; ++patch_y)
	{
		for (Int patch_x = static_cast<Int>((sea_box.Center.X - sea_box.Extent.X) /
			(PATCH_WIDTH * PATCH_SCALE));
			patch_x * PATCH_WIDTH * PATCH_SCALE <
			sea_box.Center.X + sea_box.Extent.X; ++patch_x)
		{
			Matrix4x4 patch_matrix = patch_scale;
			patch_matrix[3][0] =
				static_cast<float>(patch_x * PATCH_WIDTH * PATCH_SCALE);
			patch_matrix[3][2] =
				static_cast<float>(patch_y * PATCH_WIDTH * PATCH_SCALE);
			backend->Set_Transform(RenderBackendTransform::World,
		patch_matrix * coordinate_transform * Make_Translation(
			m_worldPositionX, m_worldPositionY, m_worldPositionZ));
			if (m_waterMaterial.Apply_Ocean(m_settings[m_tod].waterTexture,
				m_pDisplacementTexture,
				m_waterOceanNormalTexture != nullptr ? m_waterOceanNormalTexture :
					m_waterNoiseTexture,
				foam_or_caustics, m_pReflectionTexture, m_pRefractionTexture,
				environment_or_depth, shroud == nullptr ? nullptr :
					shroud->getShroudTexture(), m_pSceneDepthTexture, parameters,
				TheWaterTransparency != nullptr &&
					TheWaterTransparency->m_additiveBlend))
			{
				m_waterMaterial.Draw(m_gridMesh);
			}
		}
	}
	m_waterMaterial.Reset();
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface.*/
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::renderWater()
{
	for (const WaterSurfacePolygon &polygon : m_surfaceGeometry.polygons)
	{
		const Int point_count = static_cast<Int>(polygon.points.size());
		if (point_count <= 2)
			continue;

		if (polygon.river)
		{
			drawRiverWater(polygon);
			continue;
		}

		for (Int k = 1; k < point_count - 1; k += 2)
		{
			WaterGeometryPoint points[4] = {
				polygon.points[k + 1],
				polygon.points[k + 1],
				polygon.points[k],
				polygon.points[0]
			};
			if (k + 2 < point_count)
				points[0] = polygon.points[k + 2];

			drawTrapezoidWater(points);
		}
	}

}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky plane.  Will apply current time-of-day settings including
	* some simple UV scrolling animation. */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::renderSky()
{
	Int timeNow,timeDiff;
	Real fu,fv;

	Setting *setting=&m_settings[m_tod];

	timeNow=SDL_GetTicks();

	timeDiff=timeNow-m_LastUpdateTime;
	m_LastUpdateTime=timeNow;

	m_uOffset += timeDiff * setting->uScrollPerMs * setting->skyTexelsPerUnit;
	m_vOffset += timeDiff * setting->vScrollPerMs * setting->skyTexelsPerUnit;

	//clamp uv coordinate into 0,1 range
	m_uOffset = m_uOffset - (Real)((Int) m_uOffset);
	m_vOffset = m_vOffset - (Real)((Int) m_vOffset);

	fu= m_uOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;
	fv= m_vOffset + (SKYPLANE_SIZE * 2) * setting->skyTexelsPerUnit;


	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
		return;

	// Draw the reflection sky through the same explicit programmable material
	// boundary as the water surface.  Sky geometry uses the shared backend
	// buffer because it has the same stream contract as surface water.
	WaterSurfaceVertex vertices[4] = {};
	vertices[0].x=-SKYPLANE_SIZE;
	vertices[0].y=SKYPLANE_SIZE;
	vertices[0].z=SKYPLANE_HEIGHT;
	vertices[0].nx=0.0f;
	vertices[0].ny=0.0f;
	vertices[0].nz=1.0f;
	vertices[0].u1=m_uOffset;
	vertices[0].v1=fv;
	vertices[0].diffuse=setting->vertex01Diffuse;

	vertices[1].x=SKYPLANE_SIZE;
	vertices[1].y=SKYPLANE_SIZE;
	vertices[1].z=SKYPLANE_HEIGHT;
	vertices[1].nx=0.0f;
	vertices[1].ny=0.0f;
	vertices[1].nz=1.0f;
	vertices[1].u1=fu;
	vertices[1].v1=fv;
	vertices[1].diffuse=setting->vertex11Diffuse;

	vertices[2].x=SKYPLANE_SIZE;
	vertices[2].y=-SKYPLANE_SIZE;
	vertices[2].z=SKYPLANE_HEIGHT;
	vertices[2].nx=0.0f;
	vertices[2].ny=0.0f;
	vertices[2].nz=1.0f;
	vertices[2].u1=fu;
	vertices[2].v1=m_vOffset;
	vertices[2].diffuse=setting->vertex10Diffuse;

	vertices[3].x=-SKYPLANE_SIZE;
	vertices[3].y=-SKYPLANE_SIZE;
	vertices[3].z=SKYPLANE_HEIGHT;
	vertices[3].nx=0.0f;
	vertices[3].ny=0.0f;
	vertices[3].nz=1.0f;
	vertices[3].u1=m_uOffset;
	vertices[3].v1=m_vOffset;
	vertices[3].diffuse=setting->vertex00Diffuse;

	const UnsignedShort quad_indices[6] = {3, 0, 2, 2, 0, 1};
	if (!uploadSurfaceGeometry(vertices, 4, quad_indices, 6))
	{
		return;
	}

	Matrix3D tm(1);
	tm.Set_Translation(Vector3(0,0,0));
	backend->Set_Transform(RenderBackendTransform::World,tm);

	if (m_waterMaterial.Apply_Sky(setting->skyTexture, false, false))
	{
		m_waterMaterial.Draw(m_surfaceMesh);
	}
	m_waterMaterial.Reset();
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the sky body.  Used for moon and sun.  We rotate the image
	* so that it always faces the camera.  This removes perspective and helps hide that
	* it's a flat image. */
//-------------------------------------------------------------------------------------------------
///	@todo: Add code to render properly sorted sun sky body.
void WaterRenderSystem::renderSkyBody(Matrix3D *mat)
{
	Vector3 cPos;

	Vector3 pView,pRight,pUp,pPos(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT);

	mat->Get_Translation(&cPos);

	pView=cPos-pPos;	//billboard to camera
	pView.Normalize();	//particle view direction

	Vector3 WorldUp(0,0,-1);	///@todo: hacked so only works for reflections across xy plane

#ifdef ALLOW_TEMPORARIES
	Vector3 rotAxis=Vector3::Cross_Product(WorldUp,pView);	//get axis of rotation.
	rotAxis.Normalize();
#else
	Vector3 rotAxis;
	Vector3::Normalized_Cross_Product(WorldUp, pView, &rotAxis);
#endif

	Real angle=Vector3::Dot_Product(WorldUp,pView);

	angle = acos(angle);


	Matrix3D tm(1);
	tm.Set(rotAxis,angle);
	tm.Adjust_Translation(Vector3(SKYBODY_X,SKYBODY_Y,SKYBODY_HEIGHT));


	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
		return;

	backend->Set_Transform(RenderBackendTransform::World,tm);

	// The body uses the same explicit sky material, with alpha blending for
	// the moon/sun texture.
	WaterSurfaceVertex vertices[4] = {};
	for (WaterSurfaceVertex &vertex : vertices)
	{
		vertex.nx = 0.0f;
		vertex.ny = 0.0f;
		vertex.nz = 1.0f;
		vertex.diffuse = 0xffffffffu;
	}
	vertices[0].x=-SKYBODY_SIZE;
	vertices[0].y=SKYBODY_SIZE;
	vertices[0].u1=0;
	vertices[0].v1=1;

	vertices[1].x=SKYBODY_SIZE;
	vertices[1].y=SKYBODY_SIZE;
	vertices[1].u1=1;
	vertices[1].v1=1;

	vertices[2].x=SKYBODY_SIZE;
	vertices[2].y=-SKYBODY_SIZE;
	vertices[2].u1=1;
	vertices[2].v1=0;

	vertices[3].x=-SKYBODY_SIZE;
	vertices[3].y=-SKYBODY_SIZE;
	vertices[3].u1=0;
	vertices[3].v1=0;

	const UnsignedShort quad_indices[6] = {3, 0, 2, 2, 0, 1};
	if (!uploadSurfaceGeometry(vertices, 4, quad_indices, 6))
	{
		return;
	}

	if (m_waterMaterial.Apply_Sky(m_skyBodyTexture, true, true))
	{
		m_waterMaterial.Draw(m_surfaceMesh);
	}
	m_waterMaterial.Reset();
}

//-------------------------------------------------------------------------------------------------
/** Renders (draws) the water surface mesh geometry.
	*	This is a work-in-progress!  Do not use this code! */
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::renderWaterMesh()
{
	if (!m_gridRenderData.enabled)
		return;	//the water grid is disabled.

	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr || m_gridVertices.empty() ||
		m_gridIndices.empty())
		return;

	// Start each mesh update with a discard so the dynamic buffer does not
	// overwrite vertices still in use by the previous draw.

	Setting *setting=&m_settings[m_tod];

	const float *pData;
	Int	mx=m_gridRenderData.cells_x+1;
	Int my=m_gridRenderData.cells_y+1;
	Int i,j;

	Real cellSizeX=m_gridRenderData.cell_size;
	Real cellSizeY=m_gridRenderData.cell_size;
//	Real	uScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f;
//	Real	vScale2=5.0f*setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f;

	//Old waterRepeatCount settings in INI were based on 128x128 water grid of cellsize=10
	//Scale values to correct size.
	Real	uScale=setting->waterRepeatCount/(128.0f)*cellSizeX/10.0f*0.2f;
	Real	vScale=setting->waterRepeatCount/(128.0f)*cellSizeY/10.0f*0.2f;

	Vector3	nx(cellSizeX*2.0f,0,0);
	Vector3 ny(0,cellSizeY*2.0f,0);
	Vector3 C;
	const std::vector<float> &samples = m_gridRenderData.heights;
	const std::size_t required_sample_count =
		static_cast<std::size_t>(m_gridRenderData.cells_x + 3) *
		static_cast<std::size_t>(m_gridRenderData.cells_y + 3);
	if (samples.size() < required_sample_count)
		return;
	pData = samples.data();

    WaterSurfaceVertex* vb = m_gridVertices.data();
	const std::uint32_t diffuse = getSurfaceDiffuse(true);

	//I pulled some of these constants out of the loops for speed:
	Real uvCosScale=0.02*cos(3*m_riverVOrigin);
	Real sinOffset=25*m_riverVOrigin;
	Real originScale=m_riverVOrigin/vScale;
	Real bumpSizeDiv=cellSizeY/BUMP_SIZE;
	Real bumpSizeDiv2=0.3f*cellSizeY/BUMP_SIZE;

	//Data has a 1 vertex padding all around it so we don't need to special-case edges.  Improves performance
	for (j=0,pData=samples.data()+mx+2+1; j<my; j++,pData+=2)	//skip 2 horizontal border samples after each row
	{
		Real y=(float)j*cellSizeY;
		Real v1Offset=m_riverVOrigin+(float)j*vScale + uvCosScale*WWMath::Fast_Sin(sinOffset+y*PI/(8*MAP_XY_FACTOR));
		Real v2Offset=((float)j+originScale)*bumpSizeDiv + (float)j*bumpSizeDiv2;

		for (i=0; i<mx; i++)
		{
			//compute normal by looking at 4 vertex neightbors
			nx.Z=pData[1] - pData[-1];
			ny.Z=pData[mx+2] - pData[-(mx+2)];
			Vector3::Cross_Product(nx,ny,&C);
			C.Normalize();
			vb->nx = C.X;
			vb->ny = C.Y;
			vb->nz = C.Z;
			Real x = (float)i*cellSizeX;
			vb->x=	x;
			vb->y=	y;
			vb->z=  *pData;//WATER_OFFSET+WATER_AMP*(sin((float)i*WATER_FREQ+PhasePerFrame)+cos((float)j*WATER_FREQ+PhasePerFrame));

			vb->diffuse = diffuse;
#ifdef SCROLL_UV
//			vb->diffuse=0x80ffffff;
			vb->u1=(float)i*uScale;
			vb->v1=v1Offset;

			//old slow version
			//vb->v1=m_riverVOrigin+(float)j*vScale + 0.02*cos(3*m_riverVOrigin)*sin(25*m_riverVOrigin+y*PI/(8*MAP_XY_FACTOR));

//			vb->u2=m_initialGridU2+(float)i*uScale2;
//			vb->v2=m_initialGridV2+(float)j*vScale2;
#else
			vb->u1=(float)i*uScale;
			vb->v1=(float)j*vScale;
#endif
			vb->u2=(float)(i)*cellSizeX/BUMP_SIZE;
			vb->v2=v2Offset;
			//old slow code
			//vb->v2=(float)(j+m_riverVOrigin/vScale )*cellSizeY/BUMP_SIZE+ 0.3f*(float)j*cellSizeY/BUMP_SIZE;
			vb++;
			pData++;
		}
	}

	if (!Upload_Water_Geometry(m_gridMesh,m_gridVertices,m_gridIndices,true)) return;

	backend->Set_Transform(RenderBackendTransform::World,
		m_gridRenderData.transform);
	W3DShroud *shroud = TheTerrainRenderObject == nullptr ? nullptr :
		TheTerrainRenderObject->getShroud();
	const WaterMaterialParameters parameters =
		makeWaterMaterialParameters(true, m_pReflectionTexture != nullptr, false);
	TextureBaseClass *normal_texture = m_waterOceanNormalTexture != nullptr ?
		m_waterOceanNormalTexture : m_waterNoiseTexture;
	TextureBaseClass *foam_or_caustics = parameters.effects[3] > 0.5f &&
		m_waterCausticsTexture != nullptr ? m_waterCausticsTexture :
		m_waterSparklesTexture;
	TextureBaseClass *environment_or_depth = parameters.effects[3] > 0.5f &&
		m_waterDepthLutTexture != nullptr ? m_waterDepthLutTexture :
		m_waterEnvironmentTexture;
	if (environment_or_depth == nullptr)
		environment_or_depth = m_settings[m_tod].skyTexture;
	if (m_waterMaterial.Apply_Surface(m_riverTexture, normal_texture,
		foam_or_caustics, m_riverAlphaEdge, m_pReflectionTexture,
		m_pRefractionTexture, environment_or_depth,
		shroud == nullptr ? nullptr : shroud->getShroudTexture(),
		m_pSceneDepthTexture, parameters,
		TheWaterTransparency != nullptr &&
			TheWaterTransparency->m_additiveBlend))
	{
		m_waterMaterial.Draw(m_gridMesh);
	}

	Debug_Statistics::Record_Polys_And_Vertices(m_numIndices-2,mx*my);

	m_waterMaterial.Reset();

}

/**Utility function used to query water heights in a manner that works in both RTS and WB.*/
Real WaterRenderSystem::getWaterHeight(Real x, Real y)
{
	Real waterZ = 0.0f;
	const Real query_x = static_cast<Real>(REAL_TO_INT_FLOOR(x + 0.5f));
	const Real query_y = static_cast<Real>(REAL_TO_INT_FLOOR(y + 0.5f));

	for (const WaterSurfacePolygon &polygon : m_surfaceGeometry.polygons)
	{
		const std::size_t point_count = polygon.points.size();
		if (point_count < 3)
			continue;

		bool inside = false;
		for (std::size_t i = 0, j = point_count - 1; i < point_count;
			++i)
		{
			const WaterGeometryPoint &point_i = polygon.points[i];
			const WaterGeometryPoint &point_j = polygon.points[j];
			const bool crosses_query =
				((point_i.y > query_y) != (point_j.y > query_y)) &&
				(query_x < (point_j.x - point_i.x) *
					(query_y - point_i.y) / (point_j.y - point_i.y) + point_i.x);
			if (crosses_query)
				inside = !inside;
			j = i;
		}

		if (inside && polygon.points[0].z >= waterZ)
			waterZ = polygon.points[0].z;
	}

	if (waterZ != 0.0f)
		return waterZ;
	return INVALID_WATER_HEIGHT;	//point not underwater
}

//-------------------------------------------------------------------------------------------------
//Draw a many sided river polygon.
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::drawRiverWater(const WaterSurfacePolygon &polygon)
{
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
		return;

	const Int pointCount = static_cast<Int>(polygon.points.size());
	if (pointCount < 4 || (pointCount & 1) != 0)
		return;
	const Int rectangleCount = pointCount / 2 - 1;

	m_drawingRiver = true;

	const unsigned vertex_count = static_cast<unsigned>(pointCount);
	const unsigned index_count = static_cast<unsigned>(rectangleCount * 6);
	std::vector<UnsignedShort> indices(index_count);
	for (Int i=0; i<rectangleCount; ++i)
	{
		UnsignedShort *curIb = indices.data() + i * 6;
		curIb[0] = static_cast<UnsignedShort>(i * 2);
		curIb[1] = static_cast<UnsignedShort>(i * 2 + 1);
		curIb[2] = static_cast<UnsignedShort>(i * 2 + 3);
		curIb[3] = static_cast<UnsignedShort>(i * 2);
		curIb[4] = static_cast<UnsignedShort>(i * 2 + 3);
		curIb[5] = static_cast<UnsignedShort>(i * 2 + 2);
	}


	// Lighting is evaluated by the water shader. The vertex color is
	// limited to the configured water material color and opacity.
	const std::uint32_t diffuse = getSurfaceDiffuse(false);

	Int innerNdx = polygon.river_start;
	Int outerNdx = innerNdx+1;

	Real endLen=0;
	Real totalLen=0;
	Int i;
	for (i=0; i<pointCount-1; i++) {
		const WaterGeometryPoint &innerPt = polygon.points[i];
		const WaterGeometryPoint &outerPt = polygon.points[i + 1];
		Real dx = innerPt.x-outerPt.x;
		Real dy = innerPt.y-outerPt.y;
		Real curLen = sqrt(dx*dx+dy*dy);
		totalLen += curLen;
		if ( i==innerNdx) {
			endLen = curLen;
		}
	}
	if (endLen <= 0.0f)
		return;

	Real lengthOfRiver = (totalLen/2)-endLen;
	Real repeatCount = lengthOfRiver / (endLen);

	Real vScale=(Real)repeatCount/(Real)rectangleCount;

#define HEIGHT_TO_USE (0.5f)
	if (innerNdx >= pointCount-1) return;
	std::vector<WaterSurfaceVertex> vertices(vertex_count);
	WaterSurfaceVertex *vb = vertices.data();
	Real constA=3*m_riverVOrigin;

	for (i=0; i<(pointCount/2); ++i)
	{
		const WaterGeometryPoint &innerPt = polygon.points[outerNdx];
		const WaterGeometryPoint &outerPt = polygon.points[innerNdx];
		outerNdx++;
		innerNdx--;
		if (innerNdx<0) {
			innerNdx = pointCount-1;
		}
		if (outerNdx >= pointCount) {
			outerNdx = 0;
		}

		const Real wobbleConst=-m_riverVOrigin+vScale*(Real)i +
			WWMath::Fast_Sin(2*PI*(vScale*(Real)i) - constA)/22.0f;

		vb->x=innerPt.x;
		vb->y=innerPt.y;
		vb->z=innerPt.z;
		vb->diffuse = diffuse;
		vb->v1=wobbleConst;
		vb->u1=HEIGHT_TO_USE;
		vb->v2=wobbleConst;
		vb->u2=1.0f;
		vb->nx = 0.0f;
		vb->ny = 0.0f;
		vb->nz = 1.0f;
		++vb;

		vb->x=outerPt.x;
		vb->y=outerPt.y;
		vb->z=outerPt.z;
		vb->diffuse = diffuse;
		vb->v1=wobbleConst;
		vb->u1=0.0f;
		vb->v2=wobbleConst;
		vb->u2=0.0f;
		vb->nx = 0.0f;
		vb->ny = 0.0f;
		vb->nz = 1.0f;
		++vb;
	}

	if (!uploadSurfaceGeometry(vertices.data(), vertex_count,
		indices.data(), index_count))
	{
		return;
	}

	Matrix3D tm(1);

	backend->Set_Transform(RenderBackendTransform::World,tm);	//position the water surface
	W3DShroud *shroud = TheTerrainRenderObject == nullptr ? nullptr :
		TheTerrainRenderObject->getShroud();
	const WaterMaterialParameters parameters =
		makeWaterMaterialParameters(true, m_pReflectionTexture != nullptr,
			false);
	TextureBaseClass *normal_texture = m_waterOceanNormalTexture != nullptr ?
		m_waterOceanNormalTexture : m_waterNoiseTexture;
	TextureBaseClass *foam_or_caustics = parameters.effects[3] > 0.5f &&
		m_waterCausticsTexture != nullptr ? m_waterCausticsTexture :
		m_waterSparklesTexture;
	TextureBaseClass *environment_or_depth = parameters.effects[3] > 0.5f &&
		m_waterDepthLutTexture != nullptr ? m_waterDepthLutTexture :
		m_waterEnvironmentTexture;
	if (environment_or_depth == nullptr)
		environment_or_depth = m_settings[m_tod].skyTexture;
	if (m_waterMaterial.Apply_Surface(m_riverTexture, normal_texture,
		foam_or_caustics, m_riverAlphaEdge, m_pReflectionTexture,
		m_pRefractionTexture,
		environment_or_depth, shroud == nullptr ? nullptr :
			shroud->getShroudTexture(), m_pSceneDepthTexture,
		parameters, TheWaterTransparency != nullptr &&
			TheWaterTransparency->m_additiveBlend))
	{
		m_waterMaterial.Draw(m_surfaceMesh,wireframeForDebug);
	}
	m_waterMaterial.Reset();
}

//-------------------------------------------------------------------------------------------------
//Draw a 4 sided flat water area.
//-------------------------------------------------------------------------------------------------
void WaterRenderSystem::drawTrapezoidWater(const WaterGeometryPoint points[4])
{
	IRenderBackend *backend = WW3D::Get_Render_Backend();
	if (backend == nullptr)
		return;

	Vector3 origin(points[0].x, points[0].y, points[0].z);
	Vector3 uVec1(points[1].x, points[1].y, points[1].z);
	Vector3 vVec1(points[3].x, points[3].y, points[3].z);
	Vector3 uVec2(points[2].x, points[2].y, points[2].z);
	Vector3 vVec2(points[2].x, points[2].y, points[2].z);
	uVec2 -= vVec1;
	vVec2	-= uVec1;
	uVec1 -= origin;
	vVec1 -= origin;
	Int uCount = (uVec1.Length()+uVec2.Length()) / (8*MAP_XY_FACTOR);
	if (uCount<1) uCount = 1;
	Int vCount = (vVec1.Length()+vVec2.Length()) / (8*MAP_XY_FACTOR);
	if (vCount<1) vCount = 1;

	if (uCount>50) uCount = 50;
	if (vCount>50) vCount = 50;

	Int rectangleCount = uCount*vCount;

	uCount++;
	vCount++;

	Int i, j;
	const unsigned vertex_count = static_cast<unsigned>(uCount * vCount);
	const unsigned index_count = static_cast<unsigned>(rectangleCount * 6);
	std::vector<UnsignedShort> indices(index_count);
	UnsignedShort *curIb = indices.data();
	for (j=0; j<vCount-1; ++j)
	{
		for (i=0; i<uCount-1; ++i)
		{
			curIb[0] = static_cast<UnsignedShort>(j*uCount + i);
			curIb[1] = static_cast<UnsignedShort>((j+1)*uCount + i+1);
			curIb[2] = static_cast<UnsignedShort>((j+1)*uCount + i);
			curIb[3] = static_cast<UnsignedShort>(j*uCount + i);
			curIb[4] = static_cast<UnsignedShort>(j*uCount + i+1);
			curIb[5] = static_cast<UnsignedShort>((j+1)*uCount + i+1);
			curIb += 6;
		}
	}

	const Real waterFactor = 150.0f;
	const std::uint32_t diffuse = getSurfaceDiffuse(false);
	std::vector<WaterSurfaceVertex> vertices(vertex_count);
	WaterSurfaceVertex *vb = vertices.data();
	const Real inv_u_count = 1.0f / static_cast<Real>(uCount - 1);
	const Real inv_v_count = 1.0f / static_cast<Real>(vCount - 1);
	for (j = 0; j < vCount; ++j)
	{
		const Real dv = static_cast<Real>(j) * inv_v_count;
		for (i = 0; i < uCount; ++i)
		{
			const Real du = static_cast<Real>(i) * inv_u_count;
			Vector3 vertex = origin;
			vertex += uVec1 * du;
			vertex += vVec1 * dv;
			vertex += (dv) * (du) * (vVec2 - vVec1);

			vb->x = vertex.X;
			vb->y = vertex.Y;
			vb->z = vertex.Z;
			vb->diffuse = diffuse;
			vb->u1 = vertex.X / waterFactor;
			vb->v1 = vertex.Y / waterFactor;
			vb->u2 = vertex.X / BUMP_SIZE;
			vb->v2 = (vertex.Y + 0.3f * vertex.X) / BUMP_SIZE;
			vb->nx = 0.0f;
			vb->ny = 0.0f;
			vb->nz = 1.0f;
			++vb;
		}
	}

	if (!uploadSurfaceGeometry(vertices.data(), vertex_count,
		indices.data(), index_count))
	{
		return;
	}



	Matrix3D tm(1);
	backend->Set_Transform(RenderBackendTransform::World,tm);

	W3DShroud *shroud = TheTerrainRenderObject == nullptr ? nullptr :
		TheTerrainRenderObject->getShroud();
	const WaterMaterialParameters parameters =
		makeWaterMaterialParameters(false, m_pReflectionTexture != nullptr,
			false);
	TextureBaseClass *normal_texture = m_waterOceanNormalTexture != nullptr ?
		m_waterOceanNormalTexture : m_waterNoiseTexture;
	TextureBaseClass *foam_or_caustics = parameters.effects[3] > 0.5f &&
		m_waterCausticsTexture != nullptr ? m_waterCausticsTexture :
		m_waterSparklesTexture;
	TextureBaseClass *environment_or_depth = parameters.effects[3] > 0.5f &&
		m_waterDepthLutTexture != nullptr ? m_waterDepthLutTexture :
		m_waterEnvironmentTexture;
	if (environment_or_depth == nullptr)
		environment_or_depth = m_settings[m_tod].skyTexture;
	if (!m_waterMaterial.Apply_Surface(m_riverTexture, normal_texture,
		foam_or_caustics, m_whiteTexture, m_pReflectionTexture,
		m_pRefractionTexture,
		environment_or_depth, shroud == nullptr ? nullptr :
			shroud->getShroudTexture(), m_pSceneDepthTexture,
		parameters, TheWaterTransparency != nullptr &&
			TheWaterTransparency->m_additiveBlend))
	{
		m_waterMaterial.Reset();
		return;
	}

	m_waterMaterial.Draw(m_surfaceMesh);
	m_waterMaterial.Reset();
}


