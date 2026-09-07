/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#pragma once

#include <array>
#include <span>
#include <vector>
#include "WWLib/always.h"
#include "WW3D2/W3DFile.h"
#include "Lib/BaseType.h"
#include "Common/GameType.h"
#include "W3DDevice/GameClient/WaterMaterial.h"
#include "W3DDevice/GameClient/WaterGeometry.h"
#include "W3DDevice/GameClient/WaterReflectionRenderer.h"

#define INVALID_WATER_HEIGHT 0.0f

class CameraClass;
class AABoxClass;
class RenderInfoClass;
class TextureBaseClass;
class WaterTracksRenderSystem;
class WaterSkyboxSystem;

// Water render system. It is submitted explicitly by RTS3DScene after
// opaque scene rendering; it is not a legacy RenderObjClass scene node.
class WaterRenderSystem
{
public:
	enum WaterType
	{
		WATER_TYPE_SURFACE = 0,
		WATER_TYPE_SURFACE_VARIANT,
		WATER_TYPE_OCEAN,
		WATER_TYPE_GRID,
	};

	WaterRenderSystem();
	~WaterRenderSystem();

	void Render(RenderInfoClass &rinfo);
	void renderWater();
	void Set_Surface_Geometry(const WaterGeometry &geometry);
	void Set_Grid_Render_Data(const WaterGridRenderData &data);
	void Set_World_Position(Real x, Real y, Real z);
	void Rebuild_Grid_Geometry();
	Int init(Real waterLevel, Real dx, Real dy,
		WaterReflectionRenderer *reflectionRenderer, WaterType type);
	void reset();
	void load();
	void update();
	void updateMapOverrides();
	void setTimeOfDay(TimeOfDay tod);
	void toggleCloudLayer(Bool state) { m_useCloudLayer = state; }
	void updateRenderTargetTextures(CameraClass *cam);
	void Capture_Refraction_Texture();
	void ReleaseResources();
	void ReAcquireResources();
	Real getWaterHeight(Real x, Real y);
	void replaceSkyboxTexture(const AsciiString &oldTexName,
		const AsciiString &newTextName);

protected:
	WaterReflectionRenderer *m_reflectionRenderer;
	TextureBaseClass *m_skyBodyTexture;
	Real m_dx;
	Real m_dy;
	Real m_level;
	Real m_worldPositionX;
	Real m_worldPositionY;
	Real m_worldPositionZ;
	Real m_uOffset;
	Real m_vOffset;
	Real m_uScrollPerMs;
	Real m_vScrollPerMs;
	Int m_LastUpdateTime;
	Bool m_useCloudLayer;
	WaterType m_waterType;

    std::vector<WaterSurfaceVertex> m_gridVertices;
    std::vector<UnsignedShort> m_gridIndices;
    Graphics::WaterMeshHandle m_gridMesh;
    Graphics::WaterMeshHandle m_surfaceMesh;
    Graphics::WaterMeshHandle m_displacementMesh;

	Int m_numVertices;
	Int m_numIndices;
	TextureBaseClass *m_pReflectionTexture;
	Graphics::RHITextureHandle m_sceneColorTexture;
	Graphics::RHITextureHandle m_sceneDepthTexture{};
	TextureBaseClass *m_pDisplacementTexture;
	WaterSkyboxSystem *m_skyBox;
	WaterTracksRenderSystem *m_waterTrackSystem;

	Real m_riverVOrigin;
	Real m_waterTime;
	TextureBaseClass *m_riverTexture;
	TextureBaseClass *m_whiteTexture;
	TextureBaseClass *m_waterNoiseTexture;
	TextureBaseClass *m_waterOceanHeightTexture;
	TextureBaseClass *m_waterOceanNormalTexture;
	TextureBaseClass *m_waterEnvironmentTexture;
	TextureBaseClass *m_waterCausticsTexture;
	TextureBaseClass *m_waterDepthLutTexture;
	TextureBaseClass *m_waterSparklesTexture;
	Real m_riverXOffset;
	Real m_riverYOffset;
	Bool m_drawingRiver;
	Bool m_renderingOffscreen;
	TextureBaseClass *m_riverAlphaEdge;
	WaterGeometry m_surfaceGeometry;
	WaterGridRenderData m_gridRenderData;
	WaterMaterialClass m_waterMaterial;

	TimeOfDay m_tod;

	struct Setting
	{
		TextureBaseClass *skyTexture;
		TextureBaseClass *waterTexture;
		Int waterRepeatCount;
		Real skyTexelsPerUnit;
		std::uint32_t vertex00Diffuse;
		std::uint32_t vertex10Diffuse;
		std::uint32_t vertex11Diffuse;
		std::uint32_t vertex01Diffuse;
		std::uint32_t waterDiffuse;
		std::uint32_t transparentWaterDiffuse;
		Real uScrollPerMs;
		Real vScrollPerMs;
	};

	Setting m_settings[TIME_OF_DAY_COUNT];
	void drawRiverWater(const WaterSurfacePolygon &polygon);
	void drawTrapezoidWater(const WaterGeometryPoint points[4]);
	void loadSetting(Setting *skySetting, TimeOfDay timeOfDay);
	void renderSky();
	void testCurvedWater();
	void renderSkyBody(Matrix3D *mat);
	void renderWaterMesh();
	void renderMirror(CameraClass *cam);
	void drawSea(RenderInfoClass &rinfo);
	bool updateDisplacementTexture();
	Bool getClippedWaterPlane(CameraClass *cam, AABoxClass *box);
	WaterMaterialParameters makeWaterMaterialParameters(bool river,
		bool reflection, bool underwater) const;

	bool generateIndexBuffer(int sizeX, int sizeY);
	bool generateVertexBuffer(Int sizeX, Int sizeY, Bool doFill);
	std::uint32_t getSurfaceDiffuse(bool reduce_alpha) const;
	bool uploadSurfaceGeometry(const WaterSurfaceVertex *vertices,
		unsigned vertex_count, const UnsignedShort *indices, unsigned index_count);
};

extern WaterRenderSystem *TheWaterRenderSystem;
