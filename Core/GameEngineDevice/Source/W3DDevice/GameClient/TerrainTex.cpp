import Assets.Images.PixelEncoding;
import Graphics.RHI;
import Graphics.Resources.Textures.Atlas;
import Graphics.Resources.Textures.Upload;
import Graphics.Frame.RenderServices;
#include <future>
#include <chrono>
#include "GameLogic/GameLogic.h"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <vector>

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

// FILE: TerrainTex.cpp ////////////////////////////////////////////////
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
// File name: TerrainTex.cpp
//
// Created:   John Ahlquist, April 2001
//
// Desc:      W3DTextureHandle overrides to perform custom texturing for the terrain.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <cstring>

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TileData.h"
#include "Common/GlobalData.h"

namespace
{
Graphics::AtlasTile Atlas_Tile(const UnsignedByte* pixels,unsigned extent,unsigned x,unsigned y)
{
    return {{{reinterpret_cast<const std::byte*>(pixels),std::size_t(extent)*extent*4},
        extent,extent,std::size_t(extent)*4,Assets::PixelEncoding::BGRA8},x,y,true};
}
}

/******************************************************************************
						TerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// TerrainTextureClass::TerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 16 bit per pixel
texture of the desired height and mip level. */
//=============================================================================
TerrainTextureClass::TerrainTextureClass(int height) :
	W3DTextureHandle(TERRAIN_TEXTURE_WIDTH, height,
		Assets::PixelEncoding::BGRA8, MIP_LEVELS_3 ),
	m_sourceHeightMap(nullptr),
	m_isFlatTexture(false),
	m_flatXCell(0),
	m_flatYCell(0),
	m_flatCellWidth(0),
	m_flatPixelsPerCell(0)
{
}

TerrainTextureClass::TerrainTextureClass(int height, Assets::MaterialTextureRole role)
    : TerrainTextureClass(height)
{
    m_surfaceRole = role == Assets::MaterialTextureRole::Normal ? 0
        : role == Assets::MaterialTextureRole::Roughness ? 1 : 2;
}

//=============================================================================
// TerrainTextureClass::TerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 16 bit per pixel
texture of the desired height and mip level. */
//=============================================================================
TerrainTextureClass::TerrainTextureClass(int height, int width) :
	W3DTextureHandle(width, height,
		Assets::PixelEncoding::BGRA5551, MIP_LEVELS_1 ),
	m_sourceHeightMap(nullptr),
	m_isFlatTexture(true),
	m_flatXCell(0),
	m_flatYCell(0),
	m_flatCellWidth(0),
	m_flatPixelsPerCell(0)
{
}

bool TerrainTextureClass::Recreate_Procedural_Texture()
{
	if (!W3DTextureHandle::Recreate_Procedural_Texture()) {
		return false;
	}

	const bool populated = m_isFlatTexture
		? updateFlat(m_sourceHeightMap, m_flatXCell, m_flatYCell, m_flatCellWidth, m_flatPixelsPerCell) != 0
		: update(m_sourceHeightMap) != 0;
	if (!populated) {
		Set_Render_Backend_Texture(0);
	}
	return populated;
}


//=============================================================================
// TerrainTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
float TerrainTextureClass::Sample_Height(float u, float v) const
{
    if (m_heightSamples.empty() || !std::isfinite(u) || !std::isfinite(v)) return 1;
    const float x=std::clamp(u*m_heightWidth-.5f,0.f,float(m_heightWidth-1));
    const float y=std::clamp(v*m_heightHeight-.5f,0.f,float(m_heightHeight-1));
    const unsigned ix=static_cast<unsigned>(x), iy=static_cast<unsigned>(y);
    const auto sample=[&](unsigned sx,unsigned sy) { return m_heightSamples[sy*m_heightWidth+sx]/255.f; };
    const unsigned nx=std::min(ix+1,m_heightWidth-1),ny=std::min(iy+1,m_heightHeight-1);
    return std::lerp(std::lerp(sample(ix,iy),sample(nx,iy),x-ix),
        std::lerp(sample(ix,ny),sample(nx,ny),x-ix),y-iy);
}

int TerrainTextureClass::update(WorldHeightMap *htMap)
{
    if (!htMap) return 0;
    m_sourceHeightMap=htMap; m_isFlatTexture=false;
    auto* texture=Peek_Render_Backend_Texture();
    if (!texture || texture->Description().width<TERRAIN_TEXTURE_WIDTH) return 0;
    std::vector<Graphics::AtlasTile> tiles;
    std::vector<Graphics::AtlasRepeatBorder> borders;
    // Each channel reuses the diffuse layout, including periodic gutters and
    // mip levels. Missing maps contribute neutral values at every tile.
    std::vector<TileData*> surfaceTiles;
    std::vector<RefCountPtr<TileData>> surfaceOwners;
    std::vector<UnsignedByte> neutral;
    if (m_surfaceRole >= 0) {
        surfaceTiles.resize(htMap->m_numBitmapTiles);
        const char* suffixes[]{"_normalmap", "_roughness", "_height"};
        for (Int i = 0; i < htMap->m_numTextureClasses; ++i)
            htMap->readTexClass(&htMap->m_textureClasses[i], surfaceTiles.data(), suffixes[m_surfaceRole]);
        for (auto* tile : surfaceTiles)
            surfaceOwners.push_back(RefCountPtr<TileData>::Create_No_Add_Ref(tile));
        neutral.resize(TERRAIN_TILE_PIXEL_EXTENT*TERRAIN_TILE_PIXEL_EXTENT*4);
        for (std::size_t p = 0; p < neutral.size(); p += 4) {
            neutral[p] = m_surfaceRole == 0 ? 255 : m_surfaceRole == 1 ? 179 : 128;
            neutral[p+1] = neutral[p+2] = m_surfaceRole == 0 ? 128 : neutral[p];
            neutral[p+3] = 255;
        }
    }
    if (m_surfaceRole == 2) {
        m_heightWidth=texture->Description().width/4;
        m_heightHeight=texture->Description().height/4;
        m_heightSamples.assign(std::size_t(m_heightWidth)*m_heightHeight,255);
    }
    for (Int i=0;i<htMap->m_numBitmapTiles;++i) {
        auto* tile=htMap->getSourceTile(i);
        if (!tile || tile->m_tileLocationInTexture.x<=0) continue;
        const auto position=tile->m_tileLocationInTexture;
        const auto* pixels = m_surfaceRole < 0 ? tile->getRGBDataForWidth(TERRAIN_TILE_PIXEL_EXTENT)
            : surfaceTiles[i] ? surfaceTiles[i]->getRGBDataForWidth(TERRAIN_TILE_PIXEL_EXTENT) : neutral.data();
        tiles.push_back(Atlas_Tile(pixels,
            TERRAIN_TILE_PIXEL_EXTENT,position.x,position.y));
        if (m_surfaceRole == 2 && surfaceTiles[i]) {
            for (unsigned y=0;y<TERRAIN_TILE_PIXEL_EXTENT/4;++y)
                for (unsigned x=0;x<TERRAIN_TILE_PIXEL_EXTENT/4;++x) {
                    unsigned total=0;
                    for(unsigned dy=0;dy<4;++dy) for(unsigned dx=0;dx<4;++dx)
                        total+=pixels[((TERRAIN_TILE_PIXEL_EXTENT-1-(y*4+dy))*TERRAIN_TILE_PIXEL_EXTENT+x*4+dx)*4+2];
                    m_heightSamples[(position.y/4+y)*m_heightWidth+position.x/4+x]=static_cast<unsigned char>(total/16);
                }
        }
    }
    for (Int i=0;i<htMap->m_numTextureClasses;++i) {
        const auto& source=htMap->m_textureClasses[i];
        if (source.positionInTexture.x<=0) continue;
        const unsigned width=source.width*TERRAIN_TILE_PIXEL_EXTENT;
        borders.push_back({static_cast<unsigned>(source.positionInTexture.x),
            static_cast<unsigned>(source.positionInTexture.y),width,width,TERRAIN_TILE_OFFSET/2});
    }
    if (m_surfaceRole == 2) for (const auto& border : borders) {
        const unsigned bx=border.x/4,by=border.y/4,w=border.width/4,h=border.height/4,g=border.border/4;
        const auto pixel=[&](unsigned x,unsigned y)->unsigned char& { return m_heightSamples[y*m_heightWidth+x]; };
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<g;++x) {
            pixel(bx-1-x,by+y)=pixel(bx+w-1-x,by+y);
            pixel(bx+w+x,by+y)=pixel(bx+x,by+y);
        }
        for(unsigned y=0;y<g;++y) for(unsigned x=bx-g;x<bx+w+g;++x) {
            pixel(x,by-1-y)=pixel(x,by+h-1-y);
            pixel(x,by+h+y)=pixel(x,by+y);
        }
    }
    Graphics::TextureUpload prepared;
    const auto description=texture->Description();
    const auto encoding=texture->Encoding();
    auto prepare=std::async(std::launch::async,[&] {
        return Graphics::Prepare_Texture_Atlas(prepared,description,encoding,tiles,borders,
            Graphics::AtlasAlpha::Source,Graphics::AtlasBackground::Opaque);
    });
    while(prepare.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)
        if(TheGameLogic && !Graphics::Get_Render_Services().Is_Rendering()) TheGameLogic->refreshLoadScreen();
    if(!prepare.get() || !Graphics::Publish_Texture_Atlas(*texture,prepared,true)) return 0;
    if (m_surfaceRole < 0) {
        const Assets::MaterialTextureRole roles[]{Assets::MaterialTextureRole::Normal,
            Assets::MaterialTextureRole::Roughness, Assets::MaterialTextureRole::Height};
        for (unsigned i = 0; i < m_surfaceMaps.size(); ++i) {
            if (!m_surfaceMaps[i]) m_surfaceMaps[i] = RefCountPtr<TerrainTextureClass>::Create_No_Add_Ref(
                NEW_REF(TerrainTextureClass, (static_cast<int>(texture->Description().height), roles[i])));
            if (!m_surfaceMaps[i]->update(htMap)) return 0;
        }
    }
    return static_cast<int>(texture->Description().height);
}


//=============================================================================
// TerrainTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
Bool TerrainTextureClass::updateFlat(WorldHeightMap *htMap, Int xCell, Int yCell, Int cellWidth, Int pixelsPerCell)
{
    if (!htMap || cellWidth<=0 || pixelsPerCell<=0) return false;
    m_sourceHeightMap=htMap; m_isFlatTexture=true;
    m_flatXCell=xCell; m_flatYCell=yCell; m_flatCellWidth=cellWidth; m_flatPixelsPerCell=pixelsPerCell;
    auto* texture=Peek_Render_Backend_Texture();
    const auto extent=std::uint64_t(cellWidth)*pixelsPerCell;
    if (!texture || texture->Description().width!=extent || texture->Description().height!=extent) return false;
    std::vector<Graphics::AtlasTile> tiles;
    // The map's tile reader reuses scratch storage. Snapshot before reading the next cell.
    const std::size_t tile_bytes=std::size_t(pixelsPerCell)*pixelsPerCell*4;
    std::vector<std::byte> pixels(std::size_t(cellWidth)*cellWidth*tile_bytes);
    for (Int x=0;x<cellWidth;++x) for (Int y=0;y<cellWidth;++y) {
        const auto* source=htMap->getPointerToTileData(xCell+x,yCell+y,pixelsPerCell);
        if (!source) continue;
        auto* destination=pixels.data()+(std::size_t(x)*cellWidth+y)*tile_bytes;
        std::memcpy(destination,source,tile_bytes);
        tiles.push_back(Atlas_Tile(reinterpret_cast<const UnsignedByte*>(destination),pixelsPerCell,
            x*pixelsPerCell,(cellWidth-y-1)*pixelsPerCell));
    }
    return Graphics::Upload_Texture_Atlas(*texture,tiles,{},Graphics::AtlasAlpha::Source,
        Graphics::AtlasBackground::Opaque,false);
}


/******************************************************************************
						AlphaTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// AlphaTerrainTextureClass::AlphaTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to creat a throw away 8x8 texture,
then shares the base texture. This way the base tiles pass, drawn
using TerrainTextureClass shares the same texture with the blended edges pass,
saving lots of texture memory, and preventing seams between blended tiles. */
//=============================================================================
AlphaTerrainTextureClass::AlphaTerrainTextureClass( W3DTextureHandle *pBaseTex ):
	W3DTextureHandle(8, 8,
		Assets::PixelEncoding::BGRA5551, MIP_LEVELS_1, W3DTextureHandle::POOL_DEFAULT ),
	m_baseTexture(nullptr)
{
	// Keep the scoped registration from the parent. Its recreation callback
	// dispatches to this alias's override, which retains the base atlas.
	Set_Render_Backend_Texture(0);

	REF_PTR_SET(m_baseTexture, pBaseTex);
	if (m_baseTexture != nullptr) {
		m_baseTexture->Ensure_Render_Backend_Texture();
	}

	// Share the base texture's backend resource.
	Set_Render_Backend_Texture(
		Graphics::Retain_Texture_Resource(
			m_baseTexture != nullptr ? m_baseTexture->Peek_Render_Backend_Texture() : 0));
	Residency().Set_Initialized(Peek_Render_Backend_Texture() != 0);
}

AlphaTerrainTextureClass::~AlphaTerrainTextureClass()
{
	REF_PTR_RELEASE(m_baseTexture);
}

bool AlphaTerrainTextureClass::Recreate_Procedural_Texture()
{
	if (m_baseTexture == nullptr || !m_baseTexture->Ensure_Render_Backend_Texture()) {
		return false;
	}

	Graphics::TextureResource* const base_texture = m_baseTexture->Peek_Render_Backend_Texture();
	if (base_texture == 0) {
		return false;
	}

	Set_Render_Backend_Texture(Graphics::Retain_Texture_Resource(base_texture));
	return Peek_Render_Backend_Texture() != 0;
}



/******************************************************************************
						LightMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// LightMapTerrainTextureClass::LightMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
LightMapTerrainTextureClass::LightMapTerrainTextureClass(AsciiString name, MipCountType mipLevelCount) :
W3DTextureHandle(name.isEmpty()?"TSNoiseUrb.tga":name.str(),name.isEmpty()?"TSNoiseUrb.tga":name.str(), mipLevelCount )
{
	Get_Sampling().minification = Graphics::SamplingFilter::Best;
	Get_Sampling().magnification = Graphics::SamplingFilter::Best;
	Get_Sampling().address[0] = Graphics::RHISamplerAddress::Wrap;
	Get_Sampling().address[1] = Graphics::RHISamplerAddress::Wrap;
}

#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */










/******************************************************************************
						AlphaEdgeTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

/**
* AlphaEdgeTextureClass - Generates the alpha edge blending for terrain.
*
*/
AlphaEdgeTextureClass::AlphaEdgeTextureClass( int height, MipCountType mipLevelCount) :
//	W3DTextureHandle("EdgingTemplate.tga","EdgingTemplate.tga", mipLevelCount )
	W3DTextureHandle(TERRAIN_TEXTURE_WIDTH, height, Assets::PixelEncoding::BGRA8, mipLevelCount ),
	m_sourceHeightMap(nullptr)
{

}

int AlphaEdgeTextureClass::update256(WorldHeightMap *htMap)
{
	return 1;
}

int AlphaEdgeTextureClass::update(WorldHeightMap *htMap)
{
    if (!htMap) return 0;
    m_sourceHeightMap=htMap;
    auto* texture=Peek_Render_Backend_Texture();
    if (!texture) return 0;
    std::vector<Graphics::AtlasTile> tiles;
    for (Int i=0;i<htMap->m_numEdgeTiles;++i) {
        auto* tile=htMap->getEdgeTile(i);
        if (!tile || tile->m_tileLocationInTexture.x<=0) continue;
        const auto position=tile->m_tileLocationInTexture;
        tiles.push_back(Atlas_Tile(tile->getRGBDataForWidth(TERRAIN_TILE_PIXEL_EXTENT),
            TERRAIN_TILE_PIXEL_EXTENT,position.x,position.y));
    }
    return Graphics::Upload_Texture_Atlas(*texture,tiles,{},Graphics::AtlasAlpha::EdgeMask,
        Graphics::AtlasBackground::EdgeGradient,true) ? static_cast<int>(texture->Description().height) : 0;
}

bool AlphaEdgeTextureClass::Recreate_Procedural_Texture()
{
	if (!W3DTextureHandle::Recreate_Procedural_Texture()) {
		return false;
	}

	if (update(m_sourceHeightMap) == 0) {
		Set_Render_Backend_Texture(0);
		return false;
	}
	return true;
}



/******************************************************************************
						CloudMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// CloudMapTerrainTextureClass::CloudMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture, and sets
up the "sliding" parameters for the clouds to slide over the terrain. */
//=============================================================================
//@todo - Allow adjustment of the cloud slide rate, and lose the hard coded "cloudmap.tga"
CloudMapTerrainTextureClass::CloudMapTerrainTextureClass(MipCountType mipLevelCount) :
	W3DTextureHandle("TSCloudMed.tga","TSCloudMed.tga", mipLevelCount )
{
	Get_Sampling().mipmap =  Graphics::SamplingFilter::Fast ;
	m_xSlidePerSecond = -0.02f;
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_curTick = 0;
	m_xOffset = 0;
	m_yOffset = 0;

}

void CloudMapTerrainTextureClass::Update_Animation(float frame_seconds)
{
	m_xOffset += m_xSlidePerSecond * frame_seconds;
	m_yOffset += m_ySlidePerSecond * frame_seconds;

	// Keep the projected cloud coordinates in one texture period. The state is
	// owned by the cloud texture consumed by the terrain material.
	m_xOffset -= static_cast<Int>(m_xOffset);
	m_yOffset -= static_cast<Int>(m_yOffset);
}


/******************************************************************************
						ScorchTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// ScorchTextureClass::ScorchTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
/// @todo - get "EXScorch01.tga" from not hard coded location.
ScorchTextureClass::ScorchTextureClass(MipCountType mipLevelCount) :
	W3DTextureHandle("EXScorch01.tga","EXScorch01.tga", mipLevelCount )
// Hack to disable texture reduction.
//	W3DTextureHandle("EXScorch01.tga","EXScorch01.tga", mipLevelCount,Assets::PixelEncoding::Unknown,true,false)
{
}
