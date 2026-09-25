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

// FILE: Heightmap.cpp ////////////////////////////////////////////////
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
// File name: Heightmap.cpp
//
// Created:   Mark W., John Ahlquist, April/May 2001
//
// Desc:      Draw the terrain and scorchmarks in a scene.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------
#include "WBHeightMap.h"
#include "Common/GlobalData.h"
#include <WW3D2/ColTest.h>

import Engine.Core.Math.Triangle3;
import Engine.Core.Math.OrientedBox3;
import Engine.Core.Math.Vector3;

#define dontUSE_FLAT_HEIGHT_MAP
//-----------------------------------------------------------------------------
//         Private Data
//-----------------------------------------------------------------------------

//=============================================================================
// WBHeightMap::WBHeightMap
//=============================================================================
WBHeightMap::WBHeightMap() :
	m_drawEntireMap(false),
	m_flattenHeights(false)
{
}

//=============================================================================
// WBHeightMap::Render
//=============================================================================
/** Renders (draws) the terrain. */
//=============================================================================
void WBHeightMap::setFlattenHeights(Bool flat)
{
	if (m_flattenHeights != flat) {
		m_flattenHeights = flat;
        scheduleFullUpdate();
	}
}

// THE_Z is just above the water plane, so the flattened terrain doesn't draw
// under water.
#define THE_Z (10)
//=============================================================================
// WBHeightMap::flattenHeights
//=============================================================================
/** Flattens the terrain for the top down view.. */
//=============================================================================
float WBHeightMap::Get_Surface_Height(int x, int y) const
{
    return m_flattenHeights ? THE_Z : W3DTerrainGraphics::Get_Surface_Height(x, y);
}

//=============================================================================
// WBHeightMap::getMaxCellHeight
//=============================================================================
/** Returns maximum height of the 4 corners containing the given point */
//=============================================================================
Real WBHeightMap::getMaxCellHeight(Real x, Real y)
{
	if (!m_flattenHeights) {
		return BaseHeightMapRenderObjClass::getMaxCellHeight(x,y);
	}
	// If we are flattening the height, all z values aret THE_Z.  jba.
	return THE_Z;
}


//=============================================================================
// WBHeightMap::getHeight
//=============================================================================
/** return the height and normal of the triangle plane containing given location within heightmap. */
//=============================================================================
Real WBHeightMap::getHeightMapHeight(Real x, Real y, Coord3D* normal)
{
	if (!m_flattenHeights) {
		return BaseHeightMapRenderObjClass::getHeightMapHeight(x,y,normal);
	}
	// If we are flattening the height, all z values aret THE_Z.  jba.
	if (normal) {
		normal->x = 0;
		normal->y = 0;
		normal->z = 1;
	}
	return THE_Z;
}



//=============================================================================
// WBHeightMap::Cast_Ray
//=============================================================================
/** Return intersection of a ray with the heightmap mesh.

*/
//=============================================================================
Bool WBHeightMap::Cast_Ray(RayCollisionTestClass & raytest)
{
	if (!m_flattenHeights) {
		return BaseHeightMapRenderObjClass::Cast_Ray(raytest);
	}
	Real theZ = THE_Z;
	Bool hit = false;
	Int X,Y;
	Engine::Math::Vector3 P0, P1, P2, P3;

	if (!m_map)
		return false;	//need valid pointer to heightmap samples
//	HeightSampleType *pData = m_map->getDataPtr();
	//Clip ray to extents of heightfield
	Engine::Math::LineSegment3 lineseg,lineseg2;
	Engine::Math::CollisionResult3	result;
	Int StartCellX;
	Int EndCellX;
 	Int StartCellY;
	Int EndCellY;
	const Int overhang = 2*32; // Allow picking past the edge for scrolling & objects.
	Engine::Math::Vector3 minPt{MAP_XY_FACTOR*(-overhang), MAP_XY_FACTOR*(-overhang), -MAP_XY_FACTOR};
	Engine::Math::Vector3 maxPt{MAP_XY_FACTOR*(m_map->getXExtent()+overhang),
		MAP_XY_FACTOR*(m_map->getYExtent()+overhang), MAP_HEIGHT_SCALE*m_map->getMaxHeightValue()+MAP_XY_FACTOR};
	const auto to_box = [](const Engine::Math::Vector3 &minimum, const Engine::Math::Vector3 &maximum) {
		return Engine::Math::OrientedBox3{
			(minimum + maximum) * 0.5f,
			(maximum - minimum) * 0.5f};
	};
	auto math_hbox = to_box(minPt, maxPt);
	const auto clip_segment = [&](const Engine::Math::LineSegment3 &segment) {
		const auto intersection = math_hbox.Intersect_Segment(segment.start, segment.end);
		if (!intersection) return false;
		result.starts_overlapping = math_hbox.Contains(segment.start);
		result.fraction = intersection->fraction;
		result.normal = intersection->normal;
		result.contact_point = intersection->point;
		return true;
	};

	lineseg=raytest.Ray;

	//Set initial ray endpoints
	P0 = raytest.Ray.start;
	P1 = raytest.Ray.end;
	result.compute_contact_point=true;

	Int p;
	for (p=0; p<3; p++) {
		//find intersection point of ray and terrain bounding box
		if (clip_segment(lineseg))
		{	//ray intersects terrain or starts inside the terrain.
			if (!result.starts_overlapping)	//check if start point inside terrain
				P0 = result.contact_point;	//make intersection point the new start of the ray.

			//reverse direction of original ray and clip again to extent of
			//heightmap
			result.fraction=1.0f;	//reset the result
			result.starts_overlapping=false;
			lineseg2 = {lineseg.end, lineseg.start};	//reverse line segment
			if (clip_segment(lineseg2))
			{	if (!result.starts_overlapping)	//check if end point inside terrain
					P1 = result.contact_point;	//make intersection point the new end pont of ray
			}
		} else {
			return(false);
		}

		// Take the 2D bounding box of ray and check heights
		// inside this box for intersection.
		if (P0.x > P1.x) {	//flip start/end points
			StartCellX = floor(P1.x/MAP_XY_FACTOR);
			EndCellX = ceil(P0.x/MAP_XY_FACTOR);
		}	else {
			StartCellX = floor(P0.x/MAP_XY_FACTOR);
			EndCellX = ceil(P1.x/MAP_XY_FACTOR);
		}
		if (P0.y > P1.y) {	//flip start/end points
			StartCellY=floor(P1.y/MAP_XY_FACTOR);
			EndCellY=ceil(P0.y/MAP_XY_FACTOR);
		}	else {
			StartCellY = floor(P0.y/MAP_XY_FACTOR);
			EndCellY = ceil(P1.y/MAP_XY_FACTOR);
		}

		minPt = {MAP_XY_FACTOR*(StartCellX-1), MAP_XY_FACTOR*(StartCellY-1), theZ-1};
		maxPt = {MAP_XY_FACTOR*(EndCellX+1), MAP_XY_FACTOR*(EndCellY+1), theZ+1};
		math_hbox = to_box(minPt, maxPt);
	}

	raytest.Result->compute_contact_point=true;	// Request the contact point from the cast query.

	Int offset;
	for (offset = 1; offset < 5; offset *= 3) {
		for (Y=StartCellY-offset; Y<=EndCellY+offset; Y++) {
			//if (Y<0) continue;
			//if (Y>=m_map->getYExtent()-1) continue;

			for (X=StartCellX-offset; X<=EndCellX+offset; X++) {
				//test the 2 triangles in this cell
				//	3-----2
				//  |    /|
				//  |  /  |
				//	|/    |
				//  0-----1

				//bottom triangle first
				P0 = {X*MAP_XY_FACTOR, Y*MAP_XY_FACTOR, THE_Z};

				P1 = {(X+1)*MAP_XY_FACTOR, Y*MAP_XY_FACTOR, THE_Z};

				P2 = {(X+1)*MAP_XY_FACTOR, (Y+1)*MAP_XY_FACTOR, THE_Z};

				P3 = {X*MAP_XY_FACTOR, (Y+1)*MAP_XY_FACTOR, THE_Z};


				const auto intersect_triangle = [&raytest](const Engine::Math::Vector3 &first,
					const Engine::Math::Vector3 &second, const Engine::Math::Vector3 &third) {
					const auto intersection = Engine::Math::Triangle3::Intersect_Segment(
						raytest.Ray.start, raytest.Ray.end,
						first, second, third);
					if (!intersection || intersection->fraction >= raytest.Result->fraction)
						return false;
					raytest.Result->fraction = intersection->fraction;
					raytest.Result->normal = intersection->normal;
					if (raytest.Result->compute_contact_point) {
						raytest.Result->contact_point = intersection->point;
					}
					return true;
				};

				hit = hit | intersect_triangle(P0, P1, P2);

				if (raytest.Result->starts_overlapping)
					return true;

				//top triangle
				hit = hit | intersect_triangle(P2, P3, P0);

				if (hit)
					raytest.Result->surface_type = SURFACE_TYPE_DEFAULT;	///@todo: WW3D uses this to return dirt, grass, etc.  Do we need this?
			}
			if (hit) break;
		}
		if (hit) break;
	}
	return hit;
}


//=============================================================================
// WBHeightMap::Render
//=============================================================================
/** Renders (draws) the terrain. */
//=============================================================================
void WBHeightMap::Render(RenderInfoClass & rinfo)
{
    W3DTerrainGraphics::Render(rinfo);
}
