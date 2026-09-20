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

// Modern engine navigation and deterministic movement integration.
module;

#include <cstdint>
#include <chrono>
#include <coroutine>
#include <cmath>
#include <array>
#include <algorithm>
#include <limits>
#include <memory>
#include <map>
#include <functional>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "Utility/CppMacros.h"
#include "PreRTS.h"

#include "engine/navigation/pathfinder_api.h"

#include "Common/PerfTimer.h"
#include "Common/Player.h"
#include "Common/CRCDebug.h"
#include "Common/GlobalData.h"
#include "Common/LatchRestore.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"

#include "GameClient/Line2D.h"

#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.

#define no_INTENSE_DEBUG

#define DEBUG_QPF

#ifdef INTENSE_DEBUG
#include "GameLogic/ScriptEngine.h"
#endif

#include "Common/Xfer.h"
#include "Common/XferCRC.h"

//------------------------------------------------------------------------------ Performance Timers
#include "Common/PerfMetrics.h"

export module engine.navigation.pathfinder;

import engine.navigation;
import engine.navigation.search.route_search;
import engine.navigation.path.reconstruction;
import engine.navigation.scheduling.continuation;
import engine.navigation.search.nearest_goal_bound;
import engine.navigation.search.unsigned_divisor;
import engine.navigation.search.cell_query_cache;
import engine.navigation.search.graph.weighted_steps;
import engine.navigation.movement.goal_projection;
import engine.navigation.movement.start_escape;
import engine.navigation.movement.following.route_target;
import engine.navigation.path;
import engine.navigation.topology.reachability_cache;
import engine.navigation.topology.footprint_reachability;
import engine.navigation.movement.occupancy_policy;
import engine.navigation.movement.terrain_policy;
import engine.navigation.movement.classification.weighted_cell;
import engine.navigation.movement.destination.reservations;
import engine.navigation.movement.destination.flee_goal;
import engine.navigation.search.graph.destination_rank;
import engine.navigation.movement.corridor.yield_paths;
import engine.navigation.movement.corridor.clearance;
import engine.navigation.movement.corridor.phase_line;
import engine.navigation.movement.snapshot.occupants;
import engine.navigation.movement.snapshot.cells;
import engine.navigation.search.graph.captured_weighted;
import engine.navigation.search.hpa_route;
import engine.navigation.search.cluster_topology;
import engine.navigation.search.graph.captured_flee;
import engine.navigation.diagnostics.frame_capture;
import engine.navigation.movement.relationship_query;

// Keep the game-facing type names while the search internals use indexed SoA
// storage. All GeneralsMD consumers rebuild against the updated declarations.
export namespace navigation {
using Path = ::Path;
using PathNode = ::PathNode;
using Pathfinder = ::Pathfinder;
using PathfindCell = ::PathfindCell;
using PathfindLayer = ::PathfindLayer;
using PathfindServicesInterface = ::PathfindServicesInterface;
}

// The game-facing navigation declarations come from the public API header
// in the global module fragment. Keep their out-of-line implementations in
// the global C++ linkage domain so Clang does not treat them as redeclarations
// in the named module.
extern "C++" {

#include "engine/navigation/search/ground_route_planner.h"

//-------------------------------------------------------------------------------------------------


static inline Bool IS_IMPASSABLE(PathfindCell::CellType type) {
	// Return true if cell is impassable to ground units. jba. [8/18/2003]
	if (type==PathfindCell::CELL_IMPASSABLE) {
		return true;
	}
	if (type==PathfindCell::CELL_OBSTACLE) {
		return true;
	}
	if (type==PathfindCell::CELL_BRIDGE_IMPASSABLE) {
		return true;
	}
	return false;
}


#include "engine/navigation/movement/movement_validator.cppm"
#include "engine/navigation/path/ground_path_builder.cppm"
#include "engine/navigation/search/ground_route_planner.cppm"

inline Int IABS(Int x) {	if (x>=0) return x; return -x;};

//-----------------------------------------------------------------------------------
static Int frameToShowObstacles;

constexpr const UnsignedInt MAX_CELL_COUNT = 500;
constexpr const UnsignedInt MAX_ADJUSTMENT_CELL_COUNT = 400;
constexpr const UnsignedInt MAX_SAFE_PATH_CELL_COUNT = 2000;


//-----------------------------------------------------------------------------------
#include "engine/navigation/topology/pathfind_cells.cppm"

#include "engine/navigation/topology/pathfind_layers.cppm"

//----------------------- Pathfinder ---------------------------------------

Pathfinder::Pathfinder() :m_map(nullptr), m_pathRequests(NEW navigation::PathRequestQueue), m_groundClearanceMemo(NEW navigation::GroundClearanceQueryMemo), m_groundPlanner(std::make_unique<navigation::GroundRoutePlanner>(*this))
{
	debugPath = nullptr;
	PathfindCell::s_search.reserveSlots(4096);
	reset();
}

Pathfinder::~Pathfinder()
{
	reset();
	delete m_pathRequests;
	delete m_groundClearanceMemo;
	PathfindCell::s_search.releaseStorage();
}

Pathfinder::NavigationStats Pathfinder::getNavigationStats() const {
    auto result=m_groundPlanner->stats();
    result.lastQueueNanoseconds=m_lastQueueNanoseconds;
    result.maximumQueueNanoseconds=m_maximumQueueNanoseconds;
    result.lastRequestNanoseconds=m_lastRequestNanoseconds;
    result.lastDispatchNanoseconds=m_lastDispatchNanoseconds;
    return result;
}
void Pathfinder::invalidateNavigationSnapshots() { m_groundPlanner->invalidate(); }
Bool Pathfinder::wasPathQueueLoaded() const { return m_loadedPathQueue; }
void Pathfinder::setGroundQueriesDeferred(Bool deferred) { m_deferGroundQueries=deferred; }
Bool Pathfinder::groundQueriesDeferred() const { return m_deferGroundQueries; }
void Pathfinder::invalidateNavigationTopology() { m_groundPlanner->invalidate(); }
void Pathfinder::invalidateNavigationTopology(const IRegion2D& region) { m_groundPlanner->invalidate(region); }
void Pathfinder::materializeGroundPath(Path* path, Bool center) {
    navigation::GroundPathBuilder(*this).materialize(path, center);
}

void Pathfinder::reset()
{
	m_groundPlanner->reset();
	frameToShowObstacles = 0;
	delete [] m_blockOfMapCells;
	m_blockOfMapCells = nullptr;

	delete [] m_map;
	m_map = nullptr;

	Int i;
	for (i=0; i<=LAYER_LAST; i++) {
		m_layers[i].reset();
	}

	// reset the pathfind grid
	m_extent.lo.x=m_extent.lo.y=m_extent.hi.x=m_extent.hi.y=0;
	m_logicalExtent.lo.x=m_logicalExtent.lo.y=m_logicalExtent.hi.x=m_logicalExtent.hi.y=0;

	m_ignoreObstacleID = INVALID_ID;
	m_isTunneling = false;

	m_moveAlliesDepth = 0;

	// pathfind grid cells have not been classified yet
	m_isMapReady = false;
	m_cumulativeCellsAllocated = 0;
	m_lastQueueNanoseconds = 0;
	m_maximumQueueNanoseconds = 0;
	m_lastRequestNanoseconds = 0;
	m_lastDispatchNanoseconds = 0;

	debugPathPos.x = 0.0f;
	debugPathPos.y = 0.0f;
	debugPathPos.z = 0.0f;

	deleteInstance(debugPath);
	debugPath = nullptr;

	m_frameToShowObstacles = 0;

	m_pathRequests->clear();
	m_playerCommandSequence=0;
	m_loadedPathQueue = FALSE;

	m_numWallPieces = 0;
	for (i=0; i<MAX_WALL_PIECES; ++i)
	{
		m_wallPieces[i] = INVALID_ID;
	}

	if (TheAI && TheAI->getAiData()) {
		m_wallHeight = TheAI->getAiData()->m_wallHeight;
	}
	else
	{
		m_wallHeight = 0.0f;
	}
}

/**
 * Adds a piece of a wall.
 */
void Pathfinder::addWallPiece(Object *wallPiece)
{
	invalidateNavigationTopology();
	if (m_numWallPieces<MAX_WALL_PIECES-1) {
		m_wallPieces[m_numWallPieces] = wallPiece->getID();
		m_numWallPieces++;
	}
}

/**
 * Removes a piece of a wall
 */
void Pathfinder::removeWallPiece(Object *wallPiece)
{
	invalidateNavigationTopology();

	// sanity
  if( wallPiece == nullptr )
		return;

	// find entry
	for( Int i = 0; i < m_numWallPieces; ++i )
	{

		// match by id
		if( m_wallPieces[ i ] == wallPiece->getID() )
		{

			// put the last id in the wall piece array here
			m_wallPieces[ i ] = m_wallPieces[ m_numWallPieces - 1 ];

			// we now have one less entry
			m_numWallPieces--;

			// all done
			return;

		}

	}

}

/**
 * Checks if a point is on the wall.
 */
Bool Pathfinder::isPointOnWall(const Coord3D *pos)
{
	if (m_numWallPieces==0) return false;
	if (m_layers[LAYER_WALL].isUnused()) return false;
	PathfindLayerEnum layer = (PathfindLayerEnum)LAYER_WALL;
	PathfindCell *cell = getCell(layer, pos);
	// make sure the layer matches, since getCell can return ground layer cells if the pos is 'off' the bridge/wall
	if (cell && cell->getLayer() == layer) {
		if (cell->getType() == PathfindCell::CELL_CLEAR) {
			return true;
		}
	}
	return false;
}

/**
 * Adds a bridge & returns the layer.
 */
PathfindLayerEnum Pathfinder::addBridge(Bridge *theBridge)
{
	invalidateNavigationTopology();
	Int layer = LAYER_GROUND+1;
	while (layer<=LAYER_WALL) {
		if (m_layers[layer].isUnused()) {
			if (m_layers[layer].init(theBridge, (PathfindLayerEnum)layer) ) {
				return (PathfindLayerEnum)layer;
			}
			DEBUG_LOG(("WARNING: Bridge failed to init in pathfinder"));
			return LAYER_GROUND; // failed to init, usually cause off of the map.  jba.
		}
		layer++;
	}
	DEBUG_CRASH(("Ran out of bridge layers."));
	return LAYER_GROUND;
}

/**
 * Updates an object's layer, making sure the object is actually on the bridge first.
 */
void Pathfinder::updateLayer(Object *obj, PathfindLayerEnum layer)
{
	if (layer != LAYER_GROUND) {
		if (!TheTerrainLogic->objectInteractsWithBridgeLayer(obj, layer)) {
			layer = LAYER_GROUND;
		}
	}
	//DEBUG_LOG(("Object layer is %d", layer));
	obj->setLayer(layer);
}

/**
 * Classify the cells under the given object
 * If 'insert' is true, object is being added
 * If 'insert' is false, object is being removed
 */
void Pathfinder::classifyFence( Object *obj, Bool insert )
{
	IRegion2D changedCells{{INT_MAX,INT_MAX},{INT_MIN,INT_MIN}};
	const Coord3D *pos = obj->getPosition();
  Real angle = obj->getOrientation();

 	Real halfsizeX = obj->getTemplate()->getFenceWidth()/2;
 	Real halfsizeY = PATHFIND_CELL_SIZE_F/10.0f;
 	Real fenceOffset = obj->getTemplate()->getFenceXOffset();

 	Real c = (Real)Cos(angle);
 	Real s = (Real)Sin(angle);

 	const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
 	Real ydx = s * STEP_SIZE;
 	Real ydy = -c * STEP_SIZE;
 	Real xdx = c * STEP_SIZE;
 	Real xdy = s * STEP_SIZE;

 	Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
 	Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

 	Real tl_x = pos->x - fenceOffset*c - halfsizeY*s;
 	Real tl_y = pos->y + halfsizeY*c - fenceOffset*s;

	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi.x = REAL_TO_INT_CEIL((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi.y = REAL_TO_INT_CEIL((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	Bool didAnything = false;

 	for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
 	{
 		Real x = tl_x;
 		Real y = tl_y;
 		for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
 		{
 			Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
 			Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);
            changedCells.lo.x=std::min(changedCells.lo.x,cx);
            changedCells.lo.y=std::min(changedCells.lo.y,cy);
            changedCells.hi.x=std::max(changedCells.hi.x,cx);
            changedCells.hi.y=std::max(changedCells.hi.y,cy);
 			if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
 			{
				if (insert) {
 					ICoord2D pos;
 					pos.x = cx;
 					pos.y = cy;
					if (m_map[cx][cy].setTypeAsObstacle( obj, true, pos )) {
						didAnything = true;
					}
 				}
				else {
					if (m_map[cx][cy].removeObstacle(obj)) {
						didAnything = true;
					}
				}
				if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 				if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 				if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 				if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
			}
 		}
 	}
	if (didAnything) {
	}
    if (changedCells.lo.x<=changedCells.hi.x && changedCells.lo.y<=changedCells.hi.y)
        invalidateNavigationTopology(changedCells);
}

/**
 * Classify the cells under the given object
 * If 'insert' is true, object is being added
 * If 'insert' is false, object is being removed
 */
void Pathfinder::classifyObjectFootprint( Object *obj, Bool insert )
{
	if (obj->isKindOf(KINDOF_MINE)) {
		return;  // don't pathfind around mines.
	}

	if (obj->isKindOf(KINDOF_PROJECTILE)) {
		return;  // don't care about projectiles.
	}

	if (obj->isKindOf(KINDOF_BRIDGE_TOWER)) {
		return;  // It is important to not abuse bridge towers.
	}

	if (obj->getTemplate()->getFenceWidth() > 0.0f)
	{
		if (!obj->isKindOf(KINDOF_DEFENSIVE_WALL))
		{
			classifyFence(obj, insert);
			return;
		}
	}

	if (!insert) {
		// Just in case, remove the object.  Remove checks that the object has been added before
		// removing, so it's safer to just remove it, as by the time some units "die", they've become
		// lifeless immobile husks of debris, but we still need to remove them.  jba.

#if !RTS_GENERALS
    if ( obj->isKindOf( KINDOF_BLAST_CRATER ) ) // since these footprints are permanent, never remove them
      return;
#endif

		removeUnitFromPathfindMap(obj);
		if (obj->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) {
            invalidateNavigationTopology();
			if (!m_layers[LAYER_WALL].isUnused()) {
				Int i;
				ObjectID curID = obj->getID();
				for (i=0; i<m_numWallPieces; i++) {
					if (curID == m_wallPieces[i]) {
						m_wallPieces[i]=INVALID_ID;
					}
				}
				// Kill anybody on the wall.
				Object *obj;
				for (obj = TheGameLogic->getFirstObject(); obj; obj=obj->getNextObject()) {
					if (obj->getLayer() == LAYER_WALL) {
						if (m_layers[LAYER_WALL].isPointOnWall(&curID, 1, obj->getPosition()))
						{
							// The object fell off the wall.
							// Destroy it.
							DamageInfo extraDamageInfo;
							extraDamageInfo.in.m_damageType = DAMAGE_FALLING;
							extraDamageInfo.in.m_deathType = DEATH_SPLATTED;
							extraDamageInfo.in.m_sourceID = obj->getID();
							extraDamageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
							obj->attemptDamage(&extraDamageInfo);
						}
					}
				}
				// recalc the wall.
				m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
			}
		}
	}
	if (!obj->isKindOf(KINDOF_STRUCTURE)) {
		return;  // Only path around structures.
	}
	if (obj->isMobile()) {
		return; // mobile aren't obstacles.
	}
	/// For now, all small objects will not be obstacles
	if (obj->getGeometryInfo().getIsSmall()) {
		return;
	}

#if RTS_GENERALS
	if (obj->getHeightAboveTerrain() > PATHFIND_CELL_SIZE_F) {
		return; // Don't add bounds that are up in the air.
	}
#else
	if (obj->getHeightAboveTerrain() > PATHFIND_CELL_SIZE_F && ( ! obj->isKindOf( KINDOF_BLAST_CRATER ) ) )
  {
		return; // Don't add bounds that are up in the air.... unless a blast crater wants to do just that
	}
#endif
	internal_classifyObjectFootprint(obj, insert);
}

void Pathfinder::internal_classifyObjectFootprint( Object *obj, Bool insert )
{
	const Coord3D *pos = obj->getPosition();
	IRegion2D cellBounds;
	cellBounds.lo.x = REAL_TO_INT_FLOOR((pos->x + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.lo.y = REAL_TO_INT_FLOOR((pos->y + 0.5f)/PATHFIND_CELL_SIZE_F);
	cellBounds.hi = cellBounds.lo;

	switch(obj->getGeometryInfo().getGeomType())
	{
		case GEOMETRY_BOX:
		{
			Real angle = obj->getOrientation();

			Real halfsizeX = obj->getGeometryInfo().getMajorRadius();
			Real halfsizeY = obj->getGeometryInfo().getMinorRadius();

			Real c = (Real)Cos(angle);
			Real s = (Real)Sin(angle);

			const Real STEP_SIZE = PATHFIND_CELL_SIZE_F * 0.5f;	// in theory, should be PATHFIND_CELL_SIZE_F exactly, but needs to be smaller to avoid aliasing problems
			Real ydx = s * STEP_SIZE;
			Real ydy = -c * STEP_SIZE;
			Real xdx = c * STEP_SIZE;
			Real xdy = s * STEP_SIZE;

			Int numStepsX = REAL_TO_INT_CEIL(2.0f * halfsizeX / STEP_SIZE);
			Int numStepsY = REAL_TO_INT_CEIL(2.0f * halfsizeY / STEP_SIZE);

			Real tl_x = pos->x - halfsizeX*c - halfsizeY*s;
			Real tl_y = pos->y + halfsizeY*c - halfsizeX*s;

			for (Int iy = 0; iy < numStepsY; ++iy, tl_x += ydx, tl_y += ydy)
			{
				Real x = tl_x;
				Real y = tl_y;
				for (Int ix = 0; ix < numStepsX; ++ix, x += xdx, y += xdy)
				{
					Int cx = REAL_TO_INT_FLOOR((x + 0.5f)/PATHFIND_CELL_SIZE_F);
					Int cy = REAL_TO_INT_FLOOR((y + 0.5f)/PATHFIND_CELL_SIZE_F);

					if (cx >= 0 && cy >= 0 && cx < m_extent.hi.x && cy < m_extent.hi.y)
					{
						if (insert) {
							ICoord2D pos;
							pos.x = cx;
							pos.y = cy;
							if (m_map[cx][cy].setTypeAsObstacle( obj, false, pos )) {
							}
						}
						else {
							if (m_map[cx][cy].removeObstacle(obj)) {
							}
						}
 						if (cellBounds.lo.x>cx) cellBounds.lo.x = cx;
 						if (cellBounds.lo.y>cy) cellBounds.lo.y = cy;
 						if (cellBounds.hi.x<cx) cellBounds.hi.x = cx;
 						if (cellBounds.hi.y<cy) cellBounds.hi.y = cy;
					}
				}
			}
		}
		break;

		case GEOMETRY_SPHERE:	// not quite right, but close enough
		case GEOMETRY_CYLINDER:
		{
			// fill in all cells that overlap as obstacle cells
			/// @todo This is a very inefficient circle-rasterizer
			ICoord2D topLeft, bottomRight;
			Coord2D center, delta;
			Real radius = obj->getGeometryInfo().getMajorRadius();
			Real r2, size;

			topLeft.x = REAL_TO_INT_FLOOR(0.5f + (pos->x - radius)/PATHFIND_CELL_SIZE_F)-1;
			topLeft.y = REAL_TO_INT_FLOOR(0.5f + (pos->y - radius)/PATHFIND_CELL_SIZE_F)-1;
			size = (radius/PATHFIND_CELL_SIZE_F);
			center.x = (pos->x/PATHFIND_CELL_SIZE_F);
			center.y = (pos->y/PATHFIND_CELL_SIZE_F);

			size += 0.4f;
			r2 = size*size;

			bottomRight.x = topLeft.x + 2*size + 2;
			bottomRight.y = topLeft.y + 2*size + 2;

			for( int j = topLeft.y; j < bottomRight.y; j++ )
			{
				for( int i = topLeft.x; i < bottomRight.x; i++ )
				{
					delta.x = i+0.5f - center.x;
					delta.y = j+0.5f - center.y;

					if (delta.x*delta.x + delta.y*delta.y <= r2)
					{
						if (i >= 0 && j >= 0 && i < m_extent.hi.x && j < m_extent.hi.y)
						{
							if (insert) {
								ICoord2D pos;
								pos.x = i;
								pos.y = j;
								if (m_map[i][j].setTypeAsObstacle( obj, false, pos )) {
								}
							}
							else {
								if (m_map[i][j].removeObstacle(obj)) {
								}
							}
 							if (cellBounds.lo.x>i) cellBounds.lo.x = i;
 							if (cellBounds.lo.y>j) cellBounds.lo.y = j;
 							if (cellBounds.hi.x<i) cellBounds.hi.x = i;
 							if (cellBounds.hi.y<j) cellBounds.hi.y = j;
						}
					}
				}
			}
		}
		break;
	}


	cellBounds.lo.x -= 2;
	cellBounds.lo.y -= 2;
	cellBounds.hi.x += 2;
	cellBounds.hi.y += 2;

	Int i, j;

	if (cellBounds.lo.x < m_extent.lo.x) {
		cellBounds.lo.x = m_extent.lo.x;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.lo.y < m_extent.lo.y) {
		cellBounds.lo.y = m_extent.lo.y;
	}
	if (cellBounds.hi.x > m_extent.hi.x) {
		cellBounds.hi.x = m_extent.hi.x;
	}
	if (cellBounds.hi.y > m_extent.hi.y) {
		cellBounds.hi.y = m_extent.hi.y;
	}

	if (!insert) {
		for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
		{
			for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
			{
				if (m_map[i][j].getType()==PathfindCell::CELL_IMPASSABLE) {
					m_map[i][j].setType(PathfindCell::CELL_CLEAR);
				}
			}
		}
	}
	// Check for pinched cells, and close them off.

	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			m_map[i][j].setPinched(false);
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Int totalCount = 0;
				Int orthogonalCount = 0;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (j==l)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							totalCount++;
							if ((k==i) || (l==j)) {
								orthogonalCount++;
							}
						}

					}
				}
				// If the total open cells are < 2 or total cells < 4, we are pinched.
				if (orthogonalCount<2 || totalCount<4) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}


	// Expand building bounds 1 cell.
	for( j=cellBounds.lo.y; j<=cellBounds.hi.y; j++ )
	{
		for( i=cellBounds.lo.x; i<=cellBounds.hi.x; i++ )
		{
			if (m_map[i][j].getType() == PathfindCell::CELL_CLEAR) {
				Bool objectAdjacent = false;
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if ((k==i) && (l==j)) continue;
						if ((k!=i) && (l!=j)) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_OBSTACLE) {
							objectAdjacent = true;
							break;
						}

					}
				}
				if (objectAdjacent) {
					m_map[i][j].setPinched(true);
				}
			}
		}
	}
	invalidateNavigationTopology(cellBounds);
}

/**
 * Classify the given map cell as WATER, CLIFF, etc.
 * Note that this does NOT classify cells as OBSTACLES.
 * OBSTACLE cells are classified only via objects.
 * @todo optimize this - lots of redundant computation
 */
void Pathfinder::classifyMapCell( Int i, Int j , PathfindCell *cell)
{
	Coord3D topLeftCorner, bottomRightCorner;


	Bool hasObstacle =  (cell->getType() == PathfindCell::CELL_OBSTACLE) ;

	topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F;

	topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;
	bottomRightCorner.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F;

	cell->setPinched(false);

	PathfindCell::CellType type = PathfindCell::CELL_CLEAR;
	if (TheTerrainLogic->isCliffCell(topLeftCorner.x, topLeftCorner.y))
	{
		type = PathfindCell::CELL_CLIFF;
	}

	//
	// If any corners are underwater, this is a water cell
	//
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( topLeftCorner.x, bottomRightCorner.y) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, bottomRightCorner.y ) ) type = PathfindCell::CELL_WATER;
	if (TheTerrainLogic->isUnderwater( bottomRightCorner.x, topLeftCorner.y ) ) type = PathfindCell::CELL_WATER;

	if (hasObstacle) {
		type =  PathfindCell::CELL_OBSTACLE;
	}
	cell->setType( type );
	cell->releaseInfo();
}

/**
 * Set up for a new map.
 */
void Pathfinder::newMap()
{
	m_wallHeight = TheAI->getAiData()->m_wallHeight; // may be updated by map.ini.
	Region3D terrainExtent;
	TheTerrainLogic->getMaximumPathfindExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	Bool dataAllocated = false;
	if (m_extent.hi.x==bounds.hi.x && m_extent.hi.y==bounds.hi.y) {
		if (m_blockOfMapCells != nullptr && m_map!=nullptr) {
			dataAllocated = true;
		}
	}
	// For map load from file, we have to call newMap twice to do sequencing issues.
	// so the second time through, dataAllocated==TRUE, so we skip the allocate.
	if (!dataAllocated) {
		m_extent = bounds;
		DEBUG_ASSERTCRASH(m_map == nullptr, ("Can't reallocate pathfind cells."));
		// Allocate cells.
		m_blockOfMapCells = MSGNEW("PathfindMapCells") PathfindCell[(bounds.hi.x+1)*(bounds.hi.y+1)];
		m_map = MSGNEW("PathfindMapCells") PathfindCellP[bounds.hi.x+1];
		Int i;
		for (i=0; i<=bounds.hi.x; i++) {
			m_map[i] = &m_blockOfMapCells[i*(bounds.hi.y+1)];
		}
		for (i=0; i<LAYER_LAST; i++) {
			if (!m_layers[i].isUnused()) {
				m_layers[i].allocateCells(&m_extent);
			}
		}
		if (m_numWallPieces>0) {
			m_layers[LAYER_WALL].init(nullptr, LAYER_WALL);
			m_layers[LAYER_WALL].allocateCellsForWallLayer(&m_extent, m_wallPieces, m_numWallPieces);
		}
	}
	classifyMap();
	// Add existing objects.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{
		classifyObjectFootprint(obj, true);
	}

	m_isMapReady = true;
	// Build the immutable HPA/terrain snapshot while the map-load dependency
	// boundary is already blocking. The first gameplay update then submits only
	// independent searches and cannot pay this full-grid construction cost.
	m_groundPlanner->warmStaticSnapshot();
}

/**
 * Classify all cells in grid as obstacles, etc.
 */
void Pathfinder::classifyMap()
{
	invalidateNavigationTopology();

	Int i, j;
	// for now, sample cell corners and classify cell accordingly
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			classifyMapCell( i, j, &m_map[i][j]);
		}
	}
#if 1
	// Expand all cliff cells one step (mark pinched)
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);
						}

					}
				}
			}
		}
	}
	// Convert pinched to cliff.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getPinched()) {
				if (m_map[i][j].getType()==PathfindCell::CELL_CLEAR) {
					m_map[i][j].setType(PathfindCell::CELL_CLIFF);
				}
			}
		}
	}
	// Add a border of pinched cells to cliffs.
	for( j=m_extent.lo.y; j<=m_extent.hi.y; j++ )
	{
		for( i=m_extent.lo.x; i<=m_extent.hi.x; i++ )
		{
			if (m_map[i][j].getType() & PathfindCell::CELL_CLIFF) {
				Int k, l;
				for (k=i-1; k<i+2; k++) {
					if (k<m_extent.lo.x || k> m_extent.hi.x) continue;
					for (l=j-1; l<j+2; l++) {
						if (l<m_extent.lo.y || l> m_extent.hi.y) continue;
						if (m_map[k][l].getType() == PathfindCell::CELL_CLEAR) {
							m_map[k][l].setPinched(true);
						}

					}
				}
			}
		}
	}
#endif
	for (i=0; i<LAYER_LAST; i++) {
		if (!m_layers[i].isUnused()) {
			m_layers[i].classifyCells();
		}
	}
	if (!m_layers[LAYER_WALL].isUnused()) {
		m_layers[LAYER_WALL].classifyWallCells(m_wallPieces, m_numWallPieces);
	}
}


/**
 * Force pathfind map recomputation.
 */
void Pathfinder::forceMapRecalculation()
{
	classifyMap();
	if (m_isMapReady) m_groundPlanner->warmStaticSnapshot();
}

Locomotor* Pathfinder::chooseBestLocomotorForPosition(PathfindLayerEnum layer, LocomotorSet* locomotorSet, const Coord3D* pos )
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	PathfindCell* cell = getCell(layer, x, y );
	// off the map? call it CELL_CLEAR...
	PathfindCell::CellType celltype = cell ? cell->getType() : PathfindCell::CELL_CLEAR;

	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(celltype);
	return locomotorSet->findLocomotor(acceptableSurfaces);
}

/*static*/ LocomotorSurfaceTypeMask Pathfinder::validLocomotorSurfacesForCellType(PathfindCell::CellType t)
{
    static_assert(unsigned(PathfindCell::CELL_CLEAR)==unsigned(navigation::TerrainKind::ground));
    static_assert(unsigned(PathfindCell::CELL_WATER)==unsigned(navigation::TerrainKind::water));
    static_assert(unsigned(PathfindCell::CELL_CLIFF)==unsigned(navigation::TerrainKind::cliff));
    static_assert(unsigned(PathfindCell::CELL_RUBBLE)==unsigned(navigation::TerrainKind::rubble));
    static_assert(unsigned(PathfindCell::CELL_OBSTACLE)==unsigned(navigation::TerrainKind::obstacle));
    static_assert(unsigned(PathfindCell::CELL_BRIDGE_IMPASSABLE)==unsigned(navigation::TerrainKind::blockedBridge));
    static_assert(unsigned(PathfindCell::CELL_IMPASSABLE)==unsigned(navigation::TerrainKind::impassable));
    static_assert(unsigned(LOCOMOTORSURFACE_GROUND)==navigation::groundSurface);
    static_assert(unsigned(LOCOMOTORSURFACE_WATER)==navigation::waterSurface);
    static_assert(unsigned(LOCOMOTORSURFACE_CLIFF)==navigation::cliffSurface);
    static_assert(unsigned(LOCOMOTORSURFACE_AIR)==navigation::airSurface);
    static_assert(unsigned(LOCOMOTORSURFACE_RUBBLE)==navigation::rubbleSurface);
    return navigation::terrainSurfaces(static_cast<navigation::TerrainKind>(t));
}

//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementTerrain( PathfindLayerEnum layer, const Locomotor* locomotor, const Coord3D *pos)
{
	Int x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);

	PathfindCell *toCell = nullptr;
	toCell = getCell( layer, x, y );

	if (toCell == nullptr)
		return false;
	// Only do terrain, not obstacle cells.  jba.
	if (toCell->getType()==PathfindCell::CELL_OBSTACLE) return true;
	if (toCell->getType()==PathfindCell::CELL_IMPASSABLE) return true;
	if (toCell->getLayer()!=LAYER_GROUND && toCell->getLayer() == PathfindCell::CELL_CLEAR) {
		return true;
	}
	// check validity of destination cell
	LocomotorSurfaceTypeMask acceptableSurfaces = validLocomotorSurfacesForCellType(toCell->getType());
	if ((locomotor->getLegalSurfaces() & acceptableSurfaces) == 0)
		return false;
	return true;
}

// Ray scans are part of the deterministic search work even when the scanned
// cells are deliberately kept out of the frontier.  Saturating the counter
// keeps a pathological map from wrapping the queue's work budget.
void Pathfinder::recordNavigationWork(Int amount)
{
	if (amount <= 0 || m_cumulativeCellsAllocated >= std::numeric_limits<Int>::max())
		return;
	if (amount > std::numeric_limits<Int>::max() - m_cumulativeCellsAllocated)
		m_cumulativeCellsAllocated = std::numeric_limits<Int>::max();
	else
		m_cumulativeCellsAllocated += amount;
}

//
// Return true if we can move onto this position
//
Bool Pathfinder::validMovementPosition( Bool isCrusher, LocomotorSurfaceTypeMask acceptableSurfaces,
																			 PathfindCell *toCell, PathfindCell *fromCell )
{
	if (toCell == nullptr)
		return false;

    return navigation::permitsTerrain(
        {std::uint32_t(acceptableSurfaces),std::uint32_t(m_ignoreObstacleID),isCrusher!=0},
        {static_cast<navigation::TerrainKind>(toCell->getType()),
            std::uint32_t(toCell->getObstacleID()),true,toCell->isObstacleFence()!=0});
}

/**
 * Checks to see if obj can occupy the pathfind cell at x,y.
 * Returns false if there is another unit's goal already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkDestination(const Object *obj, Int cellX, Int cellY, PathfindLayerEnum layer, Int iRadius, Bool centerInCell)
{
    const auto query=navigation::makeDestinationQuery(obj,iRadius,centerInCell);
    return navigation::permitsDestination(query,cellX,cellY,[&](int x,int y) {
        return navigation::NativeDestinationCell{getCell(layer,x,y)};
    },navigation::NativeDestinationUnits{obj});
}

/**
 * Checks to see if obj can move through the pathfind cell at x,y.
 * Returns false if there are other units already there.
 * Assumes your locomotor already said you can go there.
 */
Bool Pathfinder::checkForMovement(const Object *obj, TCheckMovementInfo &info)
{
	return navigation::MovementValidator(*this).check(
        navigation::MovementValidator::prepare(obj, info.radius, info.centerInCell), info);
}

/**
 * Adjusts a coordinate to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapPosition(Object *obj, Coord3D *pos)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
}

/**
 * Adjusts a goal position to the center of it's cell.
 */
// Snaps the current position to it's grid location.
void Pathfinder::snapClosestGoalPosition(Object *obj, Coord3D *pos)
{
	if (!m_isMapReady || !obj || !pos || !TheTerrainLogic) return;
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *pos;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	worldToCell( &adjustDest, &cell );
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	if (checkDestination(obj, cell.x, cell.y , layer, iRadius, center)) {
		return;
	}

	// Try adjusting by 1.
	Int i,j;
	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			if (checkDestination(obj, i, j, layer, iRadius, center)) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}

	if (iRadius > 0)
		return;

	// Try to find an unoccupied cell.
	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			PathfindCell* newCell = getCell(layer, i, j);
			if (!newCell)
				continue;

			if (newCell->getGoalUnit() == INVALID_ID || newCell->getGoalUnit() == obj->getID()) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}

	for (i = cell.x - 1; i < cell.x + 2; i++) {
		for (j = cell.y - 1; j < cell.y + 2; j++) {
			PathfindCell* newCell = getCell(layer, i, j);
			if (!newCell)
				continue;

			if (newCell->getFlags()!=PathfindCell::UNIT_PRESENT_FIXED) {
				adjustCoordToCell(i, j, center, *pos, layer);
				return;
			}
		}
	}
}

/**
 * Returns coordinates of goal.
 *
 */
Bool Pathfinder::goalPosition(Object *obj, Coord3D *pos)
{
	if (!m_isMapReady || !obj || !pos) return false;
	Int iRadius;
	Bool center;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return false; // only consider ai objects.
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell = *ai->getPathfindGoalCell();
	pos->zero();
	if (cell.x<0 || cell.y<0) return false;
	adjustCoordToCell(cell.x, cell.y,  center, *pos, LAYER_GROUND);
	return true;
}


Bool Pathfinder::checkForAdjust(Object *obj, const LocomotorSet& locomotorSet, Bool isHuman,
																Int cellX, Int cellY, PathfindLayerEnum layer,
																Int iRadius, Bool center, Coord3D *dest, const Coord3D *groupDest)
{
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	if (cellP && cellP->getType() == PathfindCell::CELL_CLIFF) {
		return false;  // no final destinations on cliffs.
	}
	if (isHuman) {
		// check if new cell is in logical map.	(computer can move off logical map)
		if (cellX < m_logicalExtent.lo.x ||
				cellY < m_logicalExtent.lo.y ||
				cellX > m_logicalExtent.hi.x ||
				cellY > m_logicalExtent.hi.y) return false;
	}
	if (!obj->isKindOf(KINDOF_AIRCRAFT)) {
		// The anchor alone may be clear while the unit overlaps a cliff or
		// water. Match the terrain footprint required by ground routing.
		if (cellP->getPinched()) return false;
		const Int above = std::max(1, iRadius + (center ? 1 : 0));
		const auto* ai = obj->getAIUpdateInterface();
		const ObjectID ignored = ai ? ai->getIgnoredObstacleID() : INVALID_ID;
		for (Int y = cellY-iRadius; y < cellY+above; ++y)
			for (Int x = cellX-iRadius; x < cellX+above; ++x) {
				auto* footprintCell = getCell(layer,x,y);
				if (footprintCell && footprintCell->isObstaclePresent(ignored)) continue;
				if (!validMovementPosition(obj->getCrusherLevel()>0,
					locomotorSet.getValidSurfaces(), footprintCell)) return false;
			}
	}
	if (checkDestination(obj, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		Bool adjustedPathExists;
		if (obj->isKindOf(KINDOF_AIRCRAFT)) {
			adjustedPathExists = true;
		}	else {
			Bool pathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getPosition(), dest);
			if (!pathExists && clientSafeQuickDoesPathExist( locomotorSet, dest, &adjustDest)) {
				adjustedPathExists = true;
			} else {
				adjustedPathExists = clientSafeQuickDoesPathExist( locomotorSet, obj->getPosition(), &adjustDest);
			}
		}
		if ( adjustedPathExists	) {
			if (groupDest) {
				tightenPath(obj, locomotorSet, &adjustDest, groupDest);
				// Check to see if it is a long way to get to the adjusted destination.
				Int cost = checkPathCost(obj, locomotorSet, groupDest, &adjustDest);
				Int dx = IABS(groupDest->x-adjustDest.x);
				Int dy = IABS(groupDest->y-adjustDest.y);
				if (1.4f*(dx+dy)<cost) {
					return false;
				}
			}
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

bool Pathfinder::checkCellOutsideExtents(ICoord2D& cell) {
	return cell.x < m_logicalExtent.lo.x || cell.x > m_logicalExtent.hi.x ||
			cell.y < m_logicalExtent.lo.y || cell.y > m_logicalExtent.hi.y;
}

Bool Pathfinder::checkForLanding(Int cellX, Int cellY, PathfindLayerEnum layer,
																																								 Int iRadius, Bool center, Coord3D *dest)
{
	if (!m_isMapReady || !dest) return false;
	Coord3D adjustDest;
	PathfindCell *cellP = getCell(layer, cellX, cellY);
	if (cellP==nullptr) return false;
	switch (cellP->getType())
	{
		case PathfindCell::CELL_CLIFF:
		case PathfindCell::CELL_WATER:
		case PathfindCell::CELL_IMPASSABLE:
			return false;  // no final destinations on cliffs, water, etc.
	}
	if (checkDestination(nullptr, cellX, cellY, layer, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, cellP->getLayer());
		*dest = adjustDest;
		return true;
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to land at.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToLandingDestination(Object *obj, Coord3D *dest)
{
	if (!m_isMapReady || !obj || !dest || !TheTerrainLogic) return false;
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;

	Region3D extent;
	TheTerrainLogic->getMaximumPathfindExtent(&extent);
	// If the object is off the map & the goal is off the map, it is a scripted setup, so just
	// go to the dest.
	if (!extent.isInRegionNoZ(*dest)) {
		if (!extent.isInRegionNoZ(*obj->getPosition())) {
			return true;
		}
	}

	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &adjustDest, &cell );

	Int i = cell.x;
	Int j = cell.y;
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (checkForLanding(i,j, layer, iRadius, center, dest)) {
		return true;
	}
	Coord3D projected=*dest;
	const auto selected=navigation::projectGroundSpiral({i,j},MAX_ADJUSTMENT_CELL_COUNT,
		[&](int x,int y) {
			Coord3D candidate{};
			if (!checkForLanding(x,y,layer,iRadius,center,&candidate)) return false;
			projected=candidate;
			return true;
		});
	if (!selected) return false;
	*dest=projected;
	return true;
}


/**
 * Find an unoccupied spot for a unit to move to.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::projectFormationDestination(Object* obj,const LocomotorSet& locomotors,Coord3D* dest)
{
    if (!obj || !dest || !m_isMapReady) return false;
    Int radius; Bool center;
    getRadiusAndCenter(obj,radius,center);
    Coord3D requested=*dest;
    if (!center) { requested.x+=PATHFIND_CELL_SIZE_F/2; requested.y+=PATHFIND_CELL_SIZE_F/2; }
    ICoord2D goal;
    worldToCell(&requested,&goal);
    const Bool human=!obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
    const auto layer=TheTerrainLogic->getLayerForDestination(dest);
    const auto& bounds=human?m_logicalExtent:m_extent;
    const auto selected=navigation::projectDestinationInGrid(
        {goal.x-bounds.lo.x,goal.y-bounds.lo.y},bounds.hi.x-bounds.lo.x+1,bounds.hi.y-bounds.lo.y+1,
        [&](int x,int y) {
            Coord3D candidate=*dest;
            return checkForAdjust(obj,locomotors,human,x+bounds.lo.x,y+bounds.lo.y,
                layer,radius,center,&candidate,nullptr);
        });
    if (!selected) return false;
    adjustCoordToCell(selected->x+bounds.lo.x,selected->y+bounds.lo.y,center,*dest,layer);
    return true;
}

Bool Pathfinder::adjustDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest, const Coord3D *groupDest)
{
	if (!m_isMapReady || !obj || !dest || !TheTerrainLogic) return false;
	if( obj->isKindOf(KINDOF_PROJECTILE) )
	{
		return true; // missiles can go wherever they want to. jba.
	}

	Bool isHuman = true;
	if (obj && obj->getControllingPlayer() && (obj->getControllingPlayer()->getPlayerType()==PLAYER_COMPUTER)) {
		isHuman = false; // computer gets to cheat.
	}
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D cellDest = *dest;
	if (!center) {
		cellDest.x += PATHFIND_CELL_SIZE_F/2;
		cellDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	worldToCell( &cellDest, &cell );
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(dest);
	if (groupDest) {
		layer = TheTerrainLogic->getLayerForDestination(groupDest);
	}

	Int i = cell.x;
	Int j = cell.y;
	// Check the center cell
	Coord3D adjustDest = *dest;
	if (checkForAdjust(obj, locomotorSet, isHuman, i, j, layer, iRadius, center, &adjustDest, groupDest)) {
		// TheSuperHackers @bugfix stephanmeesters 15/06/2026 Destination adjustment always snaps to the nearest grid cell
		// even when no adjustment is necessary because there are no obstructions. For single units this adjustment
		// can be skipped in order to provide more accurate movement, which is especially noticeable for chinooks.
		const Bool singleUnit = obj && obj->getGroup() && obj->getGroup()->getCount() == 1;
		const Bool useExactDestination = isHuman && singleUnit;
		if (!useExactDestination) {
			*dest = adjustDest;
		}
		return true;
	}

	// TheSuperHackers @info Expanding counter-clockwise spiral search around center cell C. Each full lap walks right->up->left->down.
	// After every pair of directions (right+up, then left+down) length of the segment grows by 1.
	//
	//    <------  12
	//    4  3  2  11
	//    5  C  1  10
	//    6  7  8  9
	//
	Coord3D projected=*dest;
	const auto selected=navigation::projectGroundSpiral({i,j},MAX_ADJUSTMENT_CELL_COUNT,
		[&](int x,int y) {
			Coord3D candidate=*dest;
			if (!checkForAdjust(obj,locomotorSet,isHuman,x,y,layer,iRadius,center,
				&candidate,groupDest)) return false;
			projected=candidate;
			return true;
		});
	if (selected) {
		*dest=projected;
		return true;
	}

	if (groupDest) {
		// Didn't work, so just do simple adjust.
		return(adjustDestination(obj, locomotorSet, dest, nullptr));
	}
	return false;
}

Bool Pathfinder::checkForTarget(const Object *obj, 	Int cellX, Int cellY, const Weapon *weapon,
																const Object *victim, const Coord3D *victimPos,
																Int iRadius, Bool center,Coord3D *dest)
{
	Coord3D adjustDest;
	if (checkDestination(obj, cellX, cellY, LAYER_GROUND, iRadius, center)) {
		adjustCoordToCell(cellX, cellY,  center, adjustDest, LAYER_GROUND);
		if (weapon->isGoalPosWithinAttackRange( obj, &adjustDest, victim, victimPos ))	{
			*dest = adjustDest;
			return true;
		}
	}
	return false;
}

/**
 * Find an unoccupied spot for a unit to move to that can fire at victim.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustTargetDestination(const Object *obj, const Object *target, const Coord3D *targetPos,
																				 const Weapon *weapon, Coord3D *dest)
{
	Int iRadius;
	Bool center;
	getRadiusAndCenter(obj, iRadius, center);
	ICoord2D cell;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &cell )) {
		return false; // outside of bounds.
	}

	Int i = cell.x;
	Int j = cell.y;
	if (checkForTarget(obj, i,j, weapon, target, targetPos, iRadius, center, dest)) {
		return true;
	}
	Coord3D projected=*dest;
	const auto selected=navigation::projectGroundSpiral({i,j},MAX_ADJUSTMENT_CELL_COUNT,
		[&](int x,int y) {
			Coord3D candidate{};
			if (!checkForTarget(obj,x,y,weapon,target,targetPos,iRadius,center,&candidate)) return false;
			projected=candidate;
			return true;
		});
	if (!selected) return false;
	*dest=projected;
	return true;
}

Bool Pathfinder::checkForPossible(Bool isCrusher, const Coord3D* from, Bool center, const LocomotorSet& locomotorSet,
																					Int cellX, Int cellY, PathfindLayerEnum layer, Coord3D *dest, Bool startingInObstacle)
{
	PathfindCell *goalCell = getCell(layer, cellX, cellY);
	if (!goalCell) return false;
	if (IS_IMPASSABLE(goalCell->getType())) return false;
	adjustCoordToCell(cellX, cellY, center, *dest, layer);
	if (layer == LAYER_GROUND && from && !startingInObstacle) {
		GroundRouteQuery query;
		query.acceptableSurfaces = locomotorSet.getValidSurfaces();
		query.crusher = isCrusher;
		query.centerInCell = center;
		query.isHuman = true;
		if (m_groundPlanner->definitelyDisconnected(query, from, dest)) return false;
	}
	return true;
}

/**
 * Find a pathable spot near the destination.
 * Returns false if there are no spots available within a reasonable radius.
 */
Bool Pathfinder::adjustToPossibleDestination(Object *obj, const LocomotorSet& locomotorSet,
																						 Coord3D *dest)
{
	Int radius;
	Bool center;
	getRadiusAndCenter(obj, radius, center);
	ICoord2D goalCellNdx;
	Coord3D adjustDest = *dest;
	if (!center) {
		adjustDest.x += PATHFIND_CELL_SIZE_F/2;
		adjustDest.y += PATHFIND_CELL_SIZE_F/2;
	}
	if (worldToCell( &adjustDest, &goalCellNdx )) {
		return false; // outside of bounds.
	}

	// determine goal cell
	PathfindCell *goalCell;
	PathfindLayerEnum destinationLayer = TheTerrainLogic->getLayerForDestination(dest);

	goalCell = getCell(destinationLayer, goalCellNdx.x, goalCellNdx.y);
	if (!goalCell) return false;


	Coord3D from = *obj->getPosition();

	// determine start cell
	ICoord2D startCellNdx;
	worldToCell(&from, &startCellNdx);
	PathfindLayerEnum layer = LAYER_GROUND;
	if (obj) {
		layer = obj->getLayer();
	}
	PathfindCell *parentCell = getClippedCell( layer, &from );
	if (parentCell == nullptr) {
		return false;
	}

	Bool isCrusher = obj ? obj->getCrusherLevel() > 0 : false;
	Bool isObstacle = false;
	if (parentCell->getType() == PathfindCell::CELL_OBSTACLE)	{
		isObstacle = true;
	}
	if (checkDestination(obj, goalCellNdx.x, goalCellNdx.y, destinationLayer, radius, center) &&
		checkForPossible(isCrusher, &from, center, locomotorSet, goalCellNdx.x, goalCellNdx.y,
			destinationLayer, &adjustDest, isObstacle)) {
		*dest = adjustDest;
		return true;
	}

	Int i, j;
	i = goalCellNdx.x;
	j = goalCellNdx.y;
	Coord3D projected=*dest;
	const auto selected=navigation::projectGroundSpiral({i,j},MAX_ADJUSTMENT_CELL_COUNT,
		[&](int x,int y) {
			if (x==goalCellNdx.x && y==goalCellNdx.y) return false;
			Coord3D candidate=*dest;
			if (!checkForPossible(isCrusher,&from,center,locomotorSet,x,y,
				destinationLayer,&candidate,isObstacle) ||
				!checkDestination(obj,x,y,destinationLayer,radius,center)) return false;
			projected=candidate;
			return true;
		});
	if (!selected) return false;
	*dest=projected;
	return true;
}


/**
 * Queues an object to do a pathfind.
 * It will call the object's ai update->doPathfind() during processPathfindQueue().
 */
Bool Pathfinder::queueForPath(ObjectID id)
{
#ifdef DEBUG_LOGGING
	{
		Object *tmpObj = TheGameLogic->findObjectByID(id);
		if (tmpObj) {
			AIUpdateInterface *tmpAI = tmpObj->getAIUpdateInterface();
			if (tmpAI) {
				const Coord3D* pos = tmpAI->friend_getRequestedDestination();
				DEBUG_ASSERTLOG(pos->x != 0.0 && pos->y != 0.0, ("Queueing pathfind to (0, 0), usually a bug. (Unit Name: '%s', Type: '%s')", tmpObj->getName().str(), tmpObj->getTemplate()->getName().str()));
			}
		}
	}
#endif

	m_pathRequests->push(static_cast<UnsignedInt>(id));
	return true;
}

#if defined(RTS_DEBUG)
void Pathfinder::doDebugIcons() {
	const Int FRAMES_TO_SHOW_OBSTACLES = 100;
	extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
	// render AI debug information
	if (TheGlobalData->m_debugAI!=AI_DEBUG_CELLS && TheGlobalData->m_debugAI!=AI_DEBUG_TERRAIN) {
		return;
	}

		RGBColor color;
		color.red = color.green = color.blue = 0;
		addIcon(nullptr, 0, 0, color);	 // clear.
		Coord3D topLeftCorner;
		Bool showCells = TheGlobalData->m_debugAI==AI_DEBUG_CELLS;
		Int i;
		for (i=0; i<=LAYER_LAST; i++) {
			m_layers[i].doDebugIcons();
		}
		if (!showCells)	{
			frameToShowObstacles = TheGameLogic->getFrame()+FRAMES_TO_SHOW_OBSTACLES;
			//return;
		}
		// show the pathfind grid
		for( int j=0; j<getExtent()->y; j++ )
		{
			topLeftCorner.y = (Real)j * PATHFIND_CELL_SIZE_F;

			for( int i=0; i<getExtent()->x; i++ )
			{
				topLeftCorner.x = (Real)i * PATHFIND_CELL_SIZE_F;

				color.red = color.green = color.blue = 0;
				Bool empty = true;

				const PathfindCell *cell = TheAI->pathfinder()->getCell( LAYER_GROUND, i, j );
				if (cell)
				{
					switch (cell->getType())
					{
						case PathfindCell::CELL_CLIFF:
							color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_BRIDGE_IMPASSABLE:
							color.blue = color.red = 1;
							empty = false;
							break;
						case PathfindCell::CELL_IMPASSABLE:
							color.green = 1;
							empty = false;
							break;

						case PathfindCell::CELL_WATER:
							color.blue = 1;
							empty = false;
							break;

						case PathfindCell::CELL_RUBBLE:
							color.red = 1;
							color.green = 0.5;
							empty = false;
							break;

						case PathfindCell::CELL_OBSTACLE:
							color.red = color.green = 1;
							empty = false;
							break;
						default:
							if (cell->getPinched()) {
								color.blue = color.green = 0.7f;
								empty = false;
							}
							break;
					}
				}
				if (showCells) {
					empty = true;
					color.red = color.green = color.blue = 0;
					if (empty && cell) {
						if (cell->getFlags()!=PathfindCell::NO_UNITS) {
							empty = false;
							if (cell->getFlags() == PathfindCell::UNIT_GOAL) {
								color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_FIXED) {
								color.green = color.blue = color.red = 1;
							}	else if (cell->getFlags() == PathfindCell::UNIT_PRESENT_MOVING) {
								color.green = 1;
							}	else {
								color.green = color.red = 1;
							}
						}
						if (cell->isAircraftGoal()) {
							empty = false;
							color.red = 0;
							color.green = color.blue = 1;
						}
					}
				}
				if (!empty) {
					Coord3D loc;
					loc.x = topLeftCorner.x + PATHFIND_CELL_SIZE_F/2.0f;
					loc.y = topLeftCorner.y + PATHFIND_CELL_SIZE_F/2.0f;
					loc.z = TheTerrainLogic->getGroundHeight(loc.x , loc.y);
					addIcon(&loc, PATHFIND_CELL_SIZE_F*0.8f, FRAMES_TO_SHOW_OBSTACLES-1, color);
				}
			}

	}
}
#endif


//-------------------------------------------------------------------------------------------------
/**
 * Create an aircraft path.  Just jogs around tall buildings marked with KINDOF_AIRCRAFT_PATH_AROUND.
 */
Path *Pathfinder::getAircraftPath( const Object *obj, const Coord3D *to )
{
	if (!m_isMapReady || !obj || !to) return nullptr;
	// for now, quick path objects don't pathfind, generally airborne units
	// build a trivial one-node path containing destination, then avoid buildings.
	Path *thePath = newInstance(Path);
	const AIUpdateInterface *ai = obj->getAI();
	ObjectID avoidObject = INVALID_ID;
	if (ai) {
		avoidObject = ai->getBuildingToNotPathAround();
	}

	// If it is an aircraft that circles (like raptors & migs) we need to adjust the destination
	// to one that doesn't clip buildings.
	Bool checkClips = false;
	if (ai && ai->getCurLocomotor()) {
		if (ai->getCurLocomotor()->getAppearance() == LOCO_WINGS) {
			checkClips = true;
		}
	}

	Real radius = 100;
	Coord3D adjDest = *to;
	if (checkClips) {
		circleClipsTallBuilding(obj->getPosition(), to, radius, avoidObject, &adjDest);
	}
	thePath->prependNode(&adjDest, LAYER_GROUND);
	Coord3D pos = *obj->getPosition();
	pos.z = to->z;
	thePath->prependNode( &pos, LAYER_GROUND );
	Int limit = 20;
	PathNode *curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		Coord3D newPos1, newPos2, newPos3;
		if (segmentIntersectsTallBuilding(curNode, curNode->getNext(), avoidObject, &newPos1, &newPos2, &newPos3)) {
			PathNode *newNode3 = newInstance(PathNode);
			newNode3->setPosition( &newPos3 );
			newNode3->setLayer(LAYER_GROUND);
			curNode->append(newNode3);
			PathNode *newNode2 = newInstance(PathNode);
			newNode2->setPosition( &newPos2 );
			newNode2->setLayer(LAYER_GROUND);
			curNode->append(newNode2);
			PathNode *newNode1 = newInstance(PathNode);
			newNode1->setPosition( &newPos1 );
			newNode1->setLayer(LAYER_GROUND);
			curNode->append(newNode1);
			curNode = newNode2;
		}
		curNode = curNode->getNext();
		limit--;
		if (limit<0) break;
	}

	curNode = thePath->getFirstNode();
	while (curNode && curNode->getNext()) {
		curNode->setNextOptimized(curNode->getNext());
		curNode = curNode->getNext();
	}
	thePath->markOptimized();
	if (TheGlobalData->m_debugAI==AI_DEBUG_PATHS) {
		TheAI->pathfinder()->setDebugPath(thePath);
	}

	return thePath;
}


/**
 * Process some path requests in the pathfind queue.
 */
//DECLARE_PERF_TIMER(processPathfindQueue)
Bool Pathfinder::isGroundPathPending(ObjectID id) const { return m_groundPlanner->hasPending(id); }

void Pathfinder::processPathfindQueue()
{
	const auto queueStart=std::chrono::steady_clock::now();
	auto recordQueueTime=[&] {
		m_lastQueueNanoseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now()-queueStart).count());
		m_maximumQueueNanoseconds=(std::max)(m_maximumQueueNanoseconds,m_lastQueueNanoseconds);
	};
	m_groundPlanner->discardStalePending();
	m_groundPlanner->discardUnqueuedRequests();
	//USE_PERF_TIMER(processPathfindQueue)
	if (!m_isMapReady) {
		recordQueueTime();
		return;
	}
	// Get the current logical extent.
	Region3D terrainExtent;
	TheTerrainLogic->getExtent( &terrainExtent );
	IRegion2D bounds;
	bounds.lo.x = REAL_TO_INT_FLOOR(terrainExtent.lo.x / PATHFIND_CELL_SIZE_F);
	bounds.hi.x = REAL_TO_INT_FLOOR(terrainExtent.hi.x / PATHFIND_CELL_SIZE_F);
	bounds.lo.y = REAL_TO_INT_FLOOR(terrainExtent.lo.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.y = REAL_TO_INT_FLOOR(terrainExtent.hi.y / PATHFIND_CELL_SIZE_F);
	bounds.hi.x--;
	bounds.hi.y--;
	m_logicalExtent = bounds;

    m_cumulativeCellsAllocated=0;
    m_lastRequestNanoseconds=m_lastDispatchNanoseconds=0;
    unsigned callbacks=0;
    const auto poll=[&](ObjectID id,bool playerCommand) {
        auto* object=TheGameLogic->findObjectByID(id);
        auto* ai=object?object->getAIUpdateInterface():nullptr;
        const auto before=m_cumulativeCellsAllocated;
        const auto began=std::chrono::steady_clock::now();
        if (ai) {
            struct Restore {
                Bool& deferred; Bool& priority;
                Bool oldDeferred,oldPriority;
                ~Restore() { deferred=oldDeferred;priority=oldPriority; }
            } restore{m_deferGroundQueries,m_priorityGroundQuery,m_deferGroundQueries,m_priorityGroundQuery};
            m_deferGroundQueries=TRUE;
            m_priorityGroundQuery=playerCommand;
            auto timing=navigation::diagnostics::frameCapture().measure("Navigation.Queue.Request",
                TheGameLogic->getFrame(),unsigned(id));
            ai->doPathfind(this);
            ai->acknowledgePlayerPathCommand();
        }
        if (!ai || !ai->isWaitingForPath()) m_groundPlanner->cancelRequest(id);
        if (!ai || !ai->isWaitingForPath() || !isGroundPathPending(id))
            m_pathRequests->erase(unsigned(id));
        m_lastRequestNanoseconds+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-began).count());
        if (m_cumulativeCellsAllocated<=before) m_cumulativeCellsAllocated=before+1;
        ++callbacks;
    };
    // New commands overtake automatic work and earlier commands, including
    // commands received during the same tick. The ordinary FIFO independently
    // services older orders, so repeated input cannot starve them.
    ObjectID playerCommand=INVALID_ID;
    unsigned newestFrame=0;
    std::uint64_t newestSequence=0;
    for (std::size_t i=0;i<m_pathRequests->size();++i) {
        const auto id=static_cast<ObjectID>(m_pathRequests->at(i));
        const auto* object=TheGameLogic->findObjectByID(id);
        const auto* ai=object?object->getAIUpdateInterface():nullptr;
        if (ai && ai->hasFreshPlayerPathCommand() && !isGroundPathPending(id) &&
            (playerCommand==INVALID_ID || ai->getPlayerPathCommandFrame()>newestFrame ||
                (ai->getPlayerPathCommandFrame()==newestFrame && ai->getPlayerPathCommandSequence()>newestSequence))) {
            playerCommand=id;newestFrame=ai->getPlayerPathCommandFrame();newestSequence=ai->getPlayerPathCommandSequence();
        }
    }
    const auto budgetAvailable=[&] {
        return callbacks<unsigned(navigation::MaximumOwnerPollsPerFrame) &&
            m_cumulativeCellsAllocated<int(navigation::CellsPerFrame);
    };
    if (playerCommand!=INVALID_ID) m_groundPlanner->preemptOlderPlayerRequest(newestFrame,newestSequence);
    while (budgetAvailable()) {
        bool progressed=false;
        if (playerCommand!=INVALID_ID && m_groundPlanner->canAdmitRequest(true)) {
            poll(playerCommand,true);playerCommand=INVALID_ID;progressed=true;
        }
        for (const auto id:m_groundPlanner->pollableRequests()) {
            if (!budgetAvailable()) break;
            if (!m_pathRequests->contains(unsigned(id))) continue;
            poll(id,m_groundPlanner->isPlayerCommand(id));progressed=true;
        }
        if (budgetAvailable()) {
            const auto began=std::chrono::steady_clock::now();
            progressed=m_groundPlanner->advanceCapturedSlice() || progressed;
            m_groundPlanner->commitCapturedSlice();
            m_lastDispatchNanoseconds+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-began).count());
        }
        // Publish a completed foreground slice before preparing more ordinary
        // requests; their admission cost must not delay a ready player route.
        for (const auto id:m_groundPlanner->pollableRequests()) {
            if (!budgetAvailable()) break;
            if (m_groundPlanner->isPlayerCommand(id) && m_pathRequests->contains(unsigned(id))) {
                poll(id,true);progressed=true;
            }
        }
        while (budgetAvailable() && m_groundPlanner->canAdmitRequest(false)) {
            std::optional<ObjectID> next;
            for (std::size_t i=0;i<m_pathRequests->size();++i) {
                const auto id=static_cast<ObjectID>(m_pathRequests->at(i));
                if (!isGroundPathPending(id)) { next=id;break; }
            }
            if (!next) break;
            poll(*next,false);progressed=true;
        }
        if (!progressed) break;
    }
	recordQueueTime();

}


Path *Pathfinder::findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
															 const Coord3D *rawTo)
{
    if (!m_isMapReady || !from || !rawTo || (rawTo->x == 0.0f && rawTo->y == 0.0f))
        return nullptr;
    const Bool isHuman=!obj || !obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
	// The captured weighted planner is the sole ground route implementation.
	// No second route implementation is consulted before or after this query.
	{
		GroundRouteQuery query;
		query.object = obj;
		query.acceptableSurfaces = locomotorSet.getValidSurfaces();
		query.allowReservedGoal = true;
		query.isHuman = isHuman;
		if (obj) {
			getRadiusAndCenter(obj, query.radius, query.centerInCell);
			query.crusher = obj->getCrusherLevel() > 0;
		}
		// The modern planner selects live synchronous execution or a captured
		// continuation from the explicit deferral setting. The gameplay request
		// queue enables deferral while polling each AI owner.
		if (obj) {
			for (;;) {
				if (auto* path=m_groundPlanner->findWeighted(query,from,rawTo,
					locomotorSet.isDownhillOnly(),{},{},{},nullptr,true)) return path;
				if (m_deferGroundQueries) return nullptr;
				if (!m_groundPlanner->hasPending(obj->getID())) return nullptr;
				m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
			}
		}
        for (;;) {
            if (auto* path=m_groundPlanner->findWeighted(query,from,rawTo,
                    locomotorSet.isDownhillOnly(),{},{},{},nullptr,true)) return path;
            if (m_deferGroundQueries || !m_groundPlanner->hasPending(INVALID_ID)) return nullptr;
            m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
        }
    }
}
Path* PathfindServicesInterface::findPathOrClosest(Object* obj, const LocomotorSet& locomotors,
    const Coord3D* from, Coord3D* to, Bool blocked, Bool& usedFallback)
{
    usedFallback=FALSE;
    if (auto* path=findPath(obj,locomotors,from,to)) return path;
    if (obj && isGroundPathPending(obj->getID())) return nullptr;
    usedFallback=TRUE;
    return findClosestPath(obj,locomotors,from,to,blocked,0,FALSE);
}

Path* Pathfinder::findPathOrClosest(Object* obj, const LocomotorSet& locomotors,
    const Coord3D* from, Coord3D* to, Bool blocked, Bool& usedFallback)
{
    usedFallback=FALSE;
    if (!obj || !from || !to || !m_isMapReady) return nullptr;
    GroundRouteQuery query;
    query.object=obj;
    query.acceptableSurfaces=locomotors.getValidSurfaces();
    query.allowReservedGoal=true;
    query.crusher=obj->getCrusherLevel()>0;
    query.isHuman=!obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
    getRadiusAndCenter(obj,query.radius,query.centerInCell);
    const bool recovering=obj->getAIUpdateInterface() &&
        obj->getAIUpdateInterface()->getNumFramesBlocked()>LOGICFRAMES_PER_SECOND/4;
    // A suspended closest search owns the second stage of this request.
    // Polling it as an exact search loses its endpoint/failure semantics.
    if (!recovering && !m_groundPlanner->pendingUsesFallback(obj->getID())) {
        if (auto* path=m_groundPlanner->findWeighted(query,from,to,
                locomotors.isDownhillOnly(),{},{},{},nullptr,true)) return path;
        if (m_groundPlanner->hasPending(obj->getID())) {
			if (m_deferGroundQueries) return nullptr;
			for (;;) {
				m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
				if (auto* path=m_groundPlanner->findWeighted(query,from,to,
					locomotors.isDownhillOnly(),{},{},{},nullptr,true)) return path;
				if (!m_groundPlanner->hasPending(obj->getID())) break;
			}
		}
	}
    return findClosestPathInternal(obj,locomotors,from,to,blocked,0,&usedFallback,TRUE);
}
/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
/**
 * Checks to see if there is enough path width at this cell for ground
 * movement.  Returns the width available.
 */
Int Pathfinder::clearCellForDiameter(Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter)
{
    return m_groundClearanceMemo->evaluate(cellX, cellY, layer, pathDiameter, crusher,
        [&] { return computeGroundClearance(crusher, cellX, cellY, layer, pathDiameter); });
}

Int Pathfinder::computeGroundClearance(Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter)
{
    return navigation::corridorClearance(cellX,cellY,pathDiameter,crusher!=0,[&](int x,int y) {
        return navigation::NativeCorridorCell{getCell(layer,x,y)};
    },navigation::NativeCorridorUnits{});
}

/**
 * Work backwards from goal cell to construct final path.
 */


namespace {

// Ally yielding is movement policy, not a mutation of the path grid. The old
// implementation walked native cells and changed object state from a cell
// callback, making the result depend on callback order. This adapter consumes
// route geometry and publishes recovery commands in ObjectID order.
double pointSegmentDistanceSquared(const Coord3D& point, const Coord3D& start,
	const Coord3D& end)
{
	const double dx=double(end.x)-start.x, dy=double(end.y)-start.y;
	const double lengthSquared=dx*dx+dy*dy;
	if (lengthSquared<=0.0) {
		const double px=double(point.x)-start.x, py=double(point.y)-start.y;
		return px*px+py*py;
	}
	const double projection=std::clamp(((double(point.x)-start.x)*dx+
		(double(point.y)-start.y)*dy)/lengthSquared,0.0,1.0);
	const double closestX=double(start.x)+projection*dx;
	const double closestY=double(start.y)+projection*dy;
	const double px=double(point.x)-closestX, py=double(point.y)-closestY;
	return px*px+py*py;
}

bool pointNearPath(Path* path, const Object* object, PathfindLayerEnum layer,
	double corridorRadius)
{
	if (!path || !object || corridorRadius<0.0) return false;
	const auto& point=*object->getPosition();
	const double radius=corridorRadius+object->getGeometryInfo().getBoundingCircleRadius();
	const double limit=radius*radius;
	for (const PathNode* node=path->getFirstNode(); node; node=node->getNext()) {
		const auto* next=node->getNext();
		if (!next || node->getLayer()!=layer || next->getLayer()!=layer) continue;
		if (pointSegmentDistanceSquared(point,*node->getPosition(),*next->getPosition())<=limit)
			return true;
	}
	return false;
}

bool pointNearSegment(const Object* object, const Coord3D& start,
	const Coord3D& end, double corridorRadius)
{
	if (!object || corridorRadius<0.0) return false;
	const double radius=corridorRadius+object->getGeometryInfo().getBoundingCircleRadius();
	return pointSegmentDistanceSquared(*object->getPosition(),start,end)<=radius*radius;
}

template<class NearRoute>
void requestAlliedYields(Object* source, ObjectID ignored, NearRoute nearRoute,
	bool sourceInfantry, bool blockedByAlly)
{
	if (!source || !TheGameLogic) return;
	std::vector<Object*> candidates;
	for (Object* candidate=TheGameLogic->getFirstObject(); candidate;
		candidate=candidate->getNextObject()) {
		if (candidate==source || candidate->getID()==ignored || candidate->isDestroyed() ||
			source->getRelationship(candidate)!=ALLIES || !candidate->getAI() ||
			candidate->getAI()->isMoving() || !nearRoute(candidate)) continue;
		if (sourceInfantry && candidate->isKindOf(KINDOF_INFANTRY)) continue;
		if (sourceInfantry && !candidate->isKindOf(KINDOF_INFANTRY) && !blockedByAlly) continue;
		if (candidate->testStatus(OBJECT_STATUS_IS_USING_ABILITY) || candidate->getAI()->isBusy() ||
			candidate->getAI()->isAttacking()) continue;
		candidates.push_back(candidate);
	}
	std::sort(candidates.begin(),candidates.end(),[](const Object* left,const Object* right) {
		return left->getID()<right->getID();
	});
	for (Object* candidate:candidates)
		candidate->getAI()->aiMoveAwayFromUnit(source,CMD_FROM_AI);
}

}

void Pathfinder::moveAlliesAwayFromDestination(Object* obj,const Coord3D& destination)
{
	if (!obj || !obj->getAI() || !TheTerrainLogic) return;
	const auto layer=obj->getLayer()==LAYER_GROUND
		? TheTerrainLogic->getLayerForDestination(&destination) : obj->getLayer();
	const auto position=*obj->getPosition();
	Int radius=0;
	Bool centered=false;
	getRadiusAndCenter(obj,radius,centered);
	const double corridor=(double(radius)+(centered?1.0:0.0))*PATHFIND_CELL_SIZE_F;
	requestAlliedYields(obj,obj->getAI()->getIgnoredObstacleID(),
		[&](const Object* candidate) {
			return candidate->getLayer()==layer &&
				pointNearSegment(candidate,position,destination,corridor);
		},obj->isKindOf(KINDOF_INFANTRY),true);
}


Path *Pathfinder::findGroundPath( const Coord3D *from,
															 const Coord3D *rawTo, Int pathDiameter, Bool crusher)
{
    if (!m_isMapReady || !from || !rawTo || pathDiameter<1 ||
        (rawTo->x == 0.0f && rawTo->y == 0.0f)) return nullptr;
    GroundRouteQuery query;
    query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
    query.radius=pathDiameter/2;
    query.centerInCell=false;
    query.crusher=crusher;
    query.pathDiameter=pathDiameter;
    query.usePathDiameter=true;
    for (;;) {
        if (auto* path=m_groundPlanner->findWeighted(query,from,rawTo,false,{}, {}, {},nullptr,true))
            return path;
        if (m_deferGroundQueries || !m_groundPlanner->hasPending(INVALID_ID)) return nullptr;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
}
/**
 * Find a short, valid path between given locations.
 * Uses A* algorithm.
 */
Bool Pathfinder::findBrokenBridge(const LocomotorSet& locoSet,
																												 const Coord3D *from, const Coord3D *to, ObjectID *bridgeID)
{
	if (bridgeID) *bridgeID = INVALID_ID;
	if (!m_isMapReady || !from || !to || !bridgeID || !locoSet.getValidSurfaces()) return false;
	if (clientSafeQuickDoesPathExist(locoSet, from, to)) return false;
	const auto bridgeEndPoints = [&](PathfindLayerEnum layer) {
		std::array<Coord3D,2> endpoints{};
		ICoord2D start{}, end{};
		m_layers[layer].getStartCellIndex(&start);
		m_layers[layer].getEndCellIndex(&end);
		adjustCoordToCell(start.x,start.y,true,endpoints[0],LAYER_GROUND);
		adjustCoordToCell(end.x,end.y,true,endpoints[1],LAYER_GROUND);
		return endpoints;
	};
	for (Int i=0; i<=LAYER_LAST; ++i) {
		if (!m_layers[i].isDestroyed() || m_layers[i].isUnused()) continue;
		ICoord2D start{}, end{};
		m_layers[i].getStartCellIndex(&start);
		m_layers[i].getEndCellIndex(&end);
		if (start.x<0 || start.y<0 || end.x<0 || end.y<0) continue;
		const auto endpoints=bridgeEndPoints(static_cast<PathfindLayerEnum>(i));
		const bool firstSide = clientSafeQuickDoesPathExist(locoSet, from, &endpoints[0]);
		const bool secondSide = clientSafeQuickDoesPathExist(locoSet, &endpoints[1], to);
		const bool reversedFirst = clientSafeQuickDoesPathExist(locoSet, from, &endpoints[1]);
		const bool reversedSecond = clientSafeQuickDoesPathExist(locoSet, &endpoints[0], to);
		if ((firstSide && secondSide) || (reversedFirst && reversedSecond)) {
			*bridgeID=m_layers[i].getBridgeID();
			return true;
		}
	}
	return false;
}

/**
 * Does any path exist from 'from' to 'to' given the locomotor set
 * This is the quick check, only looks at whether the terrain is possible or
 * impossible to path over.  Doesn't take other units into account.
 * False means it is impossible to path.
 * True means it is possible given the terrain, but there may be units in the way.
 */
Bool Pathfinder::clientSafeQuickDoesPathExist(const LocomotorSet& locomotorSet,
    const Coord3D* from, const Coord3D* to)
{
    if (!m_isMapReady || !from || !to || !TheTerrainLogic ||
        !locomotorSet.getValidSurfaces()) return false;
    const auto destinationLayer = TheTerrainLogic->getLayerForDestination(to);
    if (!validMovementPosition(false, destinationLayer, locomotorSet, to))
        return false;
    GroundRouteQuery query;
    query.acceptableSurfaces = locomotorSet.getValidSurfaces();
    query.centerInCell = true;
    query.isHuman = true;
    return !m_groundPlanner->definitelyDisconnected(query, from, to);
}


Bool Pathfinder::clientSafeQuickDoesPathExistForUI(const LocomotorSet& locomotorSet,
    const Coord3D* from, const Coord3D* to)
{
    if (!m_isMapReady || !from || !to || !TheTerrainLogic ||
        !locomotorSet.getValidSurfaces()) return false;
    GroundRouteQuery query;
    query.acceptableSurfaces = locomotorSet.getValidSurfaces();
    query.centerInCell = true;
    query.isHuman = true;
    return !m_groundPlanner->definitelyDisconnected(query, from, to);
}


Bool Pathfinder::slowDoesPathExist( Object *obj,
																const Coord3D *from,
																const Coord3D *to,
																ObjectID ignoreObject)
{
	AIUpdateInterface *ai = obj->getAI();
	if (ai==nullptr) {
		return false;
	}
	const LocomotorSet &locoSet = ai->getLocomotorSet();
	m_ignoreObstacleID = ignoreObject;
	Path *path = findPath(obj, locoSet, from, to);
	m_ignoreObstacleID = INVALID_ID;
	Bool found = (path!=nullptr);

	deleteInstance(path);
	path = nullptr;

	return found;
}

void Pathfinder::clip( Coord3D *from, Coord3D *to )
{
	ICoord2D fromCell, toCell;
	ICoord2D clipFromCell, clipToCell;
	fromCell.x = REAL_TO_INT_FLOOR(from->x/PATHFIND_CELL_SIZE);
	fromCell.y = REAL_TO_INT_FLOOR(from->y/PATHFIND_CELL_SIZE);
	toCell.x = REAL_TO_INT_FLOOR(to->x/PATHFIND_CELL_SIZE);
	toCell.y = REAL_TO_INT_FLOOR(to->y/PATHFIND_CELL_SIZE);
	if (ClipLine2D(&fromCell, &toCell, &clipFromCell, &clipToCell,&m_extent)) {
		if (fromCell.x!=clipFromCell.x || fromCell.y != clipFromCell.y) {
			from->x = clipFromCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			from->y = clipFromCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
		if (toCell.x!=clipToCell.x || toCell.y != clipToCell.y) {
			to->x = clipToCell.x*PATHFIND_CELL_SIZE_F + 0.05f;
			to->y = clipToCell.y*PATHFIND_CELL_SIZE_F + 0.05f;
		}
	}

}

struct TightenPathStruct
{
	Object *obj;
	const LocomotorSet *locomotorSet;
	PathfindLayerEnum layer;
	Int		radius;
	Bool	center;
	Bool	foundNewDest;
	Coord3D orgDestPos;
	Coord3D newDestPos;
};


/*static*/ Int Pathfinder::tightenPathCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	TightenPathStruct* d = (TightenPathStruct*)userData;
	if (from == nullptr || to==nullptr) return 0; // failure
	if (d->layer != to->getLayer()) {
		return 0; // failure
	}

	Coord3D pos = d->orgDestPos;

	if (!TheAI->pathfinder()->checkForAdjust(d->obj, *d->locomotorSet, true, to_x, to_y, to->getLayer(), d->radius, d->center, &pos, nullptr))
	{
		return 0; // failure
	}
	d->foundNewDest = true;
	d->newDestPos = pos;

	return 0; // success but continue
}

/* Returns the cost, which is in the same units as coord3d distance. */
void Pathfinder::tightenPath(Object *obj, const LocomotorSet& locomotorSet, Coord3D *from,
		const Coord3D *to)
{
	TightenPathStruct info;

	getRadiusAndCenter(obj, info.radius, info.center);
	info.layer = TheTerrainLogic->getLayerForDestination(from);
	info.obj = obj;
	info.locomotorSet = &locomotorSet;
	info.foundNewDest = false;
	info.orgDestPos = *to;
	iterateCellsAlongLine(*from, *to, info.layer, tightenPathCallback, &info);
	if (info.foundNewDest) {
		*from = info.newDestPos;
	}
}


/* Returns the cost, which is in the same units as coord3d distance. */
Int Pathfinder::checkPathCost(Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
        const Coord3D *rawTo)
{
    constexpr Int maxCost = 0x7fff0000;
    if (!m_isMapReady || !obj || !from || !rawTo) return maxCost;

    GroundRouteQuery query;
    query.object = obj;
    query.acceptableSurfaces = locomotorSet.getValidSurfaces();
    query.crusher = obj->getCrusherLevel() > 0;
    query.isHuman = !obj->getControllingPlayer() ||
        obj->getControllingPlayer()->getPlayerType() != PLAYER_COMPUTER;
    getRadiusAndCenter(obj, query.radius, query.centerInCell);

    Path* route = nullptr;
    for (;;) {
        route = m_groundPlanner->findWeighted(query, from, rawTo,
            locomotorSet.isDownhillOnly(), {}, {}, {}, nullptr, true);
        if (route || m_deferGroundQueries ||
            !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    if (!route) return maxCost;

    double distance = 0.0;
    for (auto* node = route->getFirstNode(); node && node->getNext();
         node = node->getNext()) {
        const auto& a = *node->getPosition();
        const auto& b = *node->getNext()->getPosition();
        const double dx = double(b.x) - double(a.x);
        const double dy = double(b.y) - double(a.y);
        distance += std::sqrt(dx * dx + dy * dy);
    }
    deleteInstance(route);
    return distance >= double(maxCost) ? maxCost : static_cast<Int>(distance);
}



Path *Pathfinder::findClosestPath(Object* obj, const LocomotorSet& locomotorSet,
    const Coord3D* from, Coord3D* rawTo, Bool blocked, Real pathCostMultiplier, Bool)
{
    return findClosestPathInternal(obj,locomotorSet,from,rawTo,blocked,pathCostMultiplier,nullptr);
}

Path* Pathfinder::findClosestPathInternal(Object* obj, const LocomotorSet& locomotorSet,
    const Coord3D* from, Coord3D* rawTo, Bool blocked, Real pathCostMultiplier, Bool* usedFallback, Bool exactFirst)
{
    if (usedFallback) *usedFallback=TRUE;
    if (!m_isMapReady || !obj || !from || !rawTo || !locomotorSet.getValidSurfaces() ||
        !std::isfinite(pathCostMultiplier) || pathCostMultiplier<0) return nullptr;
    GroundRouteQuery query;
    query.object=obj;
    query.acceptableSurfaces=locomotorSet.getValidSurfaces();
    query.crusher=obj->getCrusherLevel()>0;
    query.considerTransient=blocked;
    query.isHuman=!obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
    getRadiusAndCenter(obj,query.radius,query.centerInCell);
    Coord3D clippedFrom=*from,adjustTo=*rawTo;
    if (!query.centerInCell) {
        adjustTo.x+=PATHFIND_CELL_SIZE_F/2;
        adjustTo.y+=PATHFIND_CELL_SIZE_F/2;
    }
    clip(&clippedFrom,&adjustTo);
    const auto destinationLayer=TheTerrainLogic->getLayerForDestination(&adjustTo);
    ICoord2D goalCoordinates;
    worldToCell(&adjustTo,&goalCoordinates);
    auto* goalCell=getCell(destinationLayer,goalCoordinates.x,goalCoordinates.y);
	if (!goalCell) return nullptr;
	Bool goalOnObstacle = false;
	if (m_ignoreObstacleID != INVALID_ID) {
		// Check for object on structure.
		// srj sez: check for obstacle on AIRFIELD... only want to do this for things
		// that are "parked" on the airfield, but not for things hovering over an obstacle
		// (eg, a chinook over a supply dock).
		Object *goalObj = TheGameLogic->findObjectByID(m_ignoreObstacleID);
		if (goalObj) {
			PathfindCell *ignoreCell = getClippedCell(goalObj->getLayer(), goalObj->getPosition());
			if (ignoreCell && (goalCell->getObstacleID()==ignoreCell->getObstacleID()) && (goalCell->getObstacleID() != INVALID_ID)) {
				Object* newObstacle = TheGameLogic->findObjectByID(goalCell->getObstacleID());
				if (newObstacle != nullptr && newObstacle->isKindOf(KINDOF_FS_AIRFIELD))
				{
					m_ignoreObstacleID = goalCell->getObstacleID();
					goalOnObstacle = true;
				}
				else
				{
					if (m_ignoreObstacleID == goalCell->getObstacleID()) {
						goalOnObstacle = true;
					}
				}
			}
		}
	}


    const auto* ai=obj->getAIUpdateInterface();
    const bool throughUnits=ai && ai->canPathThroughUnits();
    const navigation::DestinationRankQuery rankQuery{goalCoordinates.x,goalCoordinates.y,
        unsigned(destinationLayer),bool(exactFirst || goalOnObstacle || throughUnits),
        COST_TO_DISTANCE_FACTOR_SQR,pathCostMultiplier};
    const auto destinationQuery=navigation::makeDestinationQuery(obj,query.radius,query.centerInCell);
    const auto rank=[this,obj,rankQuery,destinationQuery](Int x,Int y,PathfindLayerEnum layer,unsigned cost)->std::optional<double> {
        return navigation::rankDestination(rankQuery,x,y,unsigned(layer),cost,
            [&](int xx,int yy,unsigned candidateLayer) {
                return navigation::permitsDestination(destinationQuery,xx,yy,[&](int cx,int cy) {
                    return navigation::NativeDestinationCell{getCell(PathfindLayerEnum(candidateLayer),cx,cy)};
                },navigation::NativeDestinationUnits{obj});
            });
    };
    const std::optional<ICoord2D> distanceOrigin=pathCostMultiplier==0 ?
        std::optional<ICoord2D>{goalCoordinates} : std::nullopt;
    bool fallback=false;
    Path* path=nullptr;
    for (;;) {
        path=m_groundPlanner->findWeighted(query,from,rawTo,locomotorSet.isDownhillOnly(),{},rank,
            distanceOrigin,&fallback,true,&rankQuery,&destinationQuery);
        if (path || m_deferGroundQueries || !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    if (usedFallback) *usedFallback=path?fallback:!isGroundPathPending(obj->getID());
    if (path && path->getLastNode() && (!exactFirst || fallback)) *rawTo=*path->getLastNode()->getPosition();
    return path;
}


void Pathfinder::adjustCoordToCell(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer)
{
    navigation::GroundPathBuilder(*this).coordinate(cellX, cellY, centerInCell, pos, layer);
}


/**
 * Work backwards from goal cell to construct final path.
 */

void Pathfinder::setDebugPath(Path *newDebugpath)
{
	if (TheGlobalData->m_debugAI)
	{
		// copy the path for debugging
		deleteInstance(debugPath);
		debugPath = newInstance(Path);

		for( PathNode *copyNode = newDebugpath->getFirstNode(); copyNode; copyNode = copyNode->getNextOptimized() )
			debugPath->appendNode( copyNode->getPosition(), copyNode->getLayer() );
	}

}

/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Int Pathfinder::iterateCellsAlongLine( const Coord3D& startWorld, const Coord3D& endWorld,
																			PathfindLayerEnum layer, CellAlongLineProc proc, void* userData )
{
	ICoord2D start, end;
	worldToCell( &startWorld, &start );
	worldToCell( &endWorld, &end );
	return iterateCellsAlongLine(start, end, layer, proc, userData);
}
/**
 * Given two world-space points, call callback for each cell.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
namespace {
template<bool SkipParent=false,class Visit>
Int visitNativeLine(Pathfinder& world,const ICoord2D& start,const ICoord2D& end,
    PathfindLayerEnum layer,Visit visit,PathfindCell* parent=nullptr) {
    PathfindCell* from=parent;
    Int result=0;
    bool first=true;
    navigation::visitPhaseLine({start.x,start.y},{end.x,end.y},[&](std::int64_t x,std::int64_t y) {
        if constexpr (SkipParent) {
            if (first) { first=false;return true; }
        }
        if (x<std::numeric_limits<Int>::min() || x>std::numeric_limits<Int>::max() ||
            y<std::numeric_limits<Int>::min() || y>std::numeric_limits<Int>::max()) return false;
        auto* to=world.getCell(layer,static_cast<Int>(x),static_cast<Int>(y));
        if (!to) return false;
        result=visit(from,to,static_cast<Int>(x),static_cast<Int>(y));
        from=to;
        return result==0;
    });
    return result;
}
}
Int Pathfinder::iterateCellsAlongLine(const ICoord2D& start,const ICoord2D& end,
    PathfindLayerEnum layer,CellAlongLineProc proc,void* userData) {
    return visitNativeLine(*this,start,end,layer,[&](auto* from,auto* to,Int x,Int y) {
        return proc(this,from,to,x,y,userData);
    });
}
//-----------------------------------------------------------------------------

static ObjectID getSlaverID(const Object* o)
{
	for (BehaviorModule** update = o->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != nullptr)
		{
			return sdu->getSlaverID();
		}
	}

	return INVALID_ID;
}

static ObjectID getContainerID(const Object* o)
{
	const Object* container = o ? o->getContainedBy() : nullptr;
	return container ? container->getID() : INVALID_ID;
}

struct segmentIntersectsStruct
{
	Object *theTallBuilding;
	ObjectID ignoreBuilding;
};

/*static*/ Int Pathfinder::segmentIntersectsBuildingCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	segmentIntersectsStruct* d = (segmentIntersectsStruct*)userData;

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		Object *obj = TheGameLogic->findObjectByID(to->getObstacleID());
		if (obj && obj->isKindOf(KINDOF_AIRCRAFT_PATH_AROUND)) {
			if (obj->getID() == d->ignoreBuilding) {
				return 0;
			}
			d->theTallBuilding = obj;
			return 1;
		}
	}

	return 0;	// keep going
}



struct ViewBlockedStruct
{
	const Object *obj;
	const Object *objOther;
};


/*static*/ Int Pathfinder::lineBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const ViewBlockedStruct* d = (const ViewBlockedStruct*)userData;

	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{

		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		// nor does the object we're trying to see!
		if (to->isObstaclePresent(d->objOther->getID()))
			return 0;

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->objOther)))
			return 0;

		// if the obstacle is transparent, ignore it, since this callback is only used for line-of-sight. (srj)
		if (to->isObstacleTransparent())
			return 0;

		return 1;	// bail early
	}

	return 0;	// keep going
}

struct ViewAttackBlockedStruct
{
	const Object *obj;
	const Object *victim;
	const PathfindCell *victimCell;
	Int		skipCount;
};

/*static*/ Int Pathfinder::attackBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	ViewAttackBlockedStruct* d = (ViewAttackBlockedStruct*)userData;

	if (d->skipCount>0) {
		d->skipCount--;
		return 0;
	}
	if (to != nullptr && (to->getType() == PathfindCell::CELL_OBSTACLE))
	{
		// we never block our own view!
		if (to->isObstaclePresent(d->obj->getID()))
			return 0;

		if (d->victim) {
			// nor does the object we're trying to attack!
			if (to->isObstaclePresent(d->victim->getID()))
				return 0;
			// if the obstacle is the item to which objOther is slaved, ignore it as an obstacle.
			if (to->isObstaclePresent(getSlaverID(d->victim)))
				return 0;
		}

		// if the obstacle is our container, ignore it as an obstacle.
		if (to->isObstaclePresent(getContainerID(d->obj)))
			return 0;

		// @todo: if the obstacle is objOther's container, AND it's a "visible" container, ignore it.

		// if the obstacle is the item to which we are slaved, ignore it as an obstacle.
		if (to->isObstaclePresent(getSlaverID(d->obj)))
			return 0;

		if (to->isObstacleTransparent())
			return 0;
		//Kris: Added the check for victimCell because in China01 -- after the intro, NW of your
		//base is a cream colored building that lies in a negative coord. When you order units to
		//force attack it, it crashes.
		if( d->victimCell && to->isObstaclePresent( d->victimCell->getObstacleID() ) )
		{
			// Victim is inside the bounds of another object.  We don't let this block us,
			// as usually it is on the edge and it looks like we should be able to shoot it. jba.
			return 0;
		}
		return 1;	// bail early
	}

	return 0;	// keep going
}

//-----------------------------------------------------------------------------
Bool Pathfinder::isViewBlockedByObstacle(const Object* obj, const Object* objOther)
{
	ViewBlockedStruct info;
	info.obj = obj;
	info.objOther = objOther;
	if (objOther && objOther->isSignificantlyAboveTerrain()) {
		return false; // We don't check los to flying objects.  jba.
	}
#if 1
	return isAttackViewBlockedByObstacle(obj, *obj->getPosition(), objOther, *objOther->getPosition());
#else
	PathfindLayerEnum layer = objOther->getLayer();
	if (layer==LAYER_GROUND) {
		layer = obj->getLayer();
	}
	Int ret = iterateCellsAlongLine(*obj->getPosition(), *objOther->getPosition(),
		layer, lineBlockedByObstacleCallback, &info);
	return ret != 0;
#endif
}


//-----------------------------------------------------------------------------
Bool Pathfinder::isAttackViewBlockedByObstacle(const Object* attacker, const Coord3D& attackerPos, const Object* victim, const Coord3D& victimPos)
{
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - attackerPos is (%g,%g,%g) (%X,%X,%X)",
	//	attackerPos.x, attackerPos.y, attackerPos.z,
	//	AS_INT(attackerPos.x),AS_INT(attackerPos.y),AS_INT(attackerPos.z)));
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() - victimPos is (%g,%g,%g) (%X,%X,%X)",
	//	victimPos.x, victimPos.y, victimPos.z,
	//	AS_INT(victimPos.x),AS_INT(victimPos.y),AS_INT(victimPos.z)));
	// Global switch to turn this off in case it doesn't work.
	if (!TheAI->getAiData()->m_attackUsesLineOfSight)
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 1"));
		return false;
	}

	// If the attacker doesn't need line of sight, isn't blocked.
	if (!attacker->isKindOf(KINDOF_ATTACK_NEEDS_LINE_OF_SIGHT))
	{
		//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 2"));
		return false;
	}

// srj sez: this is a good start at taking terrain into account for attacks, but findAttackPath needs to be smartened also
#define LOS_TERRAIN
#ifdef LOS_TERRAIN
	const Weapon* w = attacker->getCurrentWeapon();
	if (attacker->isKindOf(KINDOF_IMMOBILE)) {
		// Don't take terrain blockage into account, since we can't move around it. jba.
		w = nullptr;
	}
	if (w)
	{
		Bool viewBlocked;
		if (victim)
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victim);
		else
			viewBlocked = !w->isClearGoalFiringLineOfSightTerrain(attacker, attackerPos, victimPos);

		if (viewBlocked)
		{
			//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 3"));
			return true;
		}
	}
#endif

	ViewAttackBlockedStruct info;
	info.obj = attacker;
	info.victim = victim;
	PathfindLayerEnum layer = LAYER_GROUND;
	if (victim) {
		layer = victim->getLayer();
	}
	info.victimCell = getCell(layer, &victimPos);

	info.skipCount = 0;
	if (attacker->getLayer() != LAYER_GROUND)
	{
		info.skipCount = 3;	/// srj -- someone wanna tell me what this magic number means?
												/// jba - Yes, it means that if someone is on a bridge, or rooftop, they can see
												///      3 pathfind cells out of whatever they are standing on.
												/// srj -- awesome! thank you very much :-)
		if (layer==LAYER_GROUND) {
			layer = attacker->getLayer();
		}
	}

	Int ret = iterateCellsAlongLine(attackerPos, victimPos, layer, attackBlockedByObstacleCallback, &info);
	//CRCDEBUG_LOG(("Pathfinder::isAttackViewBlockedByObstacle() 4"));
	return ret != 0;
}

static void computeNormalRadialOffset(const Coord3D& from,	Coord3D& insert, const Coord3D& to,
																			Object *obj, Real radius)
{
	Real crossProduct;
	Real dx = to.x - from.x;
	Real dy = to.y -from.y;
	Coord3D objPos = *obj->getPosition();


	Real objDx = objPos.x - from.x;
	Real objDy = objPos.y - from.y;

	crossProduct = dx*objDy - dy*objDx;

	Coord3D fromToNormal;
	fromToNormal.z = 0;
	if (crossProduct>0) {
		fromToNormal.x = dy;
		fromToNormal.y = -dx;
	}	else {
		fromToNormal.x = -dy;
		fromToNormal.y = dx;
	}
	fromToNormal.normalize();
	Real length = radius;
	insert = *obj->getPosition();
	insert.x += fromToNormal.x*length;
	insert.y += fromToNormal.y*length;

}

//-----------------------------------------------------------------------------
Bool Pathfinder::segmentIntersectsTallBuilding(const PathNode *curNode,
										PathNode *nextNode,  ObjectID ignoreBuilding, Coord3D *insertPos1,  Coord3D *insertPos2,  Coord3D *insertPos3 )
{
	segmentIntersectsStruct info;
	info.theTallBuilding = nullptr;
	info.ignoreBuilding = ignoreBuilding;

	Coord3D fromPos = *curNode->getPosition();
	Coord3D toPos = *nextNode->getPosition();

	Int i;
	for (i=0; i<2; i++) {
		Int ret = iterateCellsAlongLine(fromPos, toPos, LAYER_GROUND, segmentIntersectsBuildingCallback, &info);
		if (ret!=0 && info.theTallBuilding) {
			// see if toPos is inside the radius of the tall building.
			Coord3D bldgPos = *info.theTallBuilding->getPosition();
			Coord2D delta;
			Real radius = info.theTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
			delta.x = toPos.x - bldgPos.x;
			delta.y = toPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				toPos.x = bldgPos.x+delta.x;
				toPos.y = bldgPos.y+delta.y;
				nextNode->setPosition(&toPos);
				continue;
			}
			delta.x = fromPos.x - bldgPos.x;
			delta.y = fromPos.y - bldgPos.y;
			if (delta.length() <= radius*0.98) {
				if (delta.length() < 0.1) {
					delta.x = 1;
				}
				delta.normalize();
				delta.x *= radius;
				delta.y *= radius;
				fromPos.x = bldgPos.x+delta.x;
				fromPos.y = bldgPos.y+delta.y;
			}


			computeNormalRadialOffset(fromPos, *insertPos2, toPos, info.theTallBuilding, radius);
			computeNormalRadialOffset(fromPos, *insertPos1, *insertPos2, info.theTallBuilding, radius);
			computeNormalRadialOffset(*insertPos2, *insertPos3, toPos, info.theTallBuilding, radius);

			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
Bool Pathfinder::circleClipsTallBuilding(	const Coord3D *from, const Coord3D *to, Real circleRadius, ObjectID ignoreBuilding, Coord3D *adjustTo)
{
	PartitionFilterAcceptByKindOf filterKindof(MAKE_KINDOF_MASK(KINDOF_AIRCRAFT_PATH_AROUND), KINDOFMASK_NONE);
	PartitionFilter *filters[] = { &filterKindof, nullptr };
	Object* tallBuilding = ThePartitionManager->getClosestObject(to, circleRadius, FROM_BOUNDINGSPHERE_2D, filters);
	if (tallBuilding) {
		Real radius = tallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
		computeNormalRadialOffset(*from, *adjustTo, *to, tallBuilding, circleRadius+radius);
		Object* otherTallBuilding = ThePartitionManager->getClosestObject(adjustTo, circleRadius, FROM_BOUNDINGSPHERE_2D, filters);
		if (otherTallBuilding && otherTallBuilding!=tallBuilding) {
			radius = otherTallBuilding->getGeometryInfo().getBoundingCircleRadius() + 2*PATHFIND_CELL_SIZE_F;
			Coord3D tmpTo = *adjustTo;
			computeNormalRadialOffset(*from, *adjustTo, tmpTo, otherTallBuilding, circleRadius+radius);
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------

struct LinePassableStruct
{
    Bool crusher;
    navigation::MovementContext movement;
	LocomotorSurfaceTypeMask acceptableSurfaces;
	Int radius;
	Bool centerInCell;
	Bool blocked;
	Bool allowPinched;
};

/*static*/ Int Pathfinder::linePassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const LinePassableStruct* d = (const LinePassableStruct*)userData;

	TCheckMovementInfo info;
	info.cell.x = to_x;
	info.cell.y = to_y;
	info.layer = to->getLayer();
	info.centerInCell = d->centerInCell;
	info.radius = d->radius;
	info.considerTransient = d->blocked;
	info.acceptableSurfaces = d->acceptableSurfaces;
	if (!navigation::MovementValidator(*pathfinder).check(d->movement, info, false))
	{
		return 1;	// bail out
	}

	if (info.allyFixedCount || info.enemyFixed)
	{
		return 1;	// bail out
	}

	if (!d->allowPinched && to->getPinched()) {
		return 1; // bail out.
	}

	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		if (to->getType() == PathfindCell::CELL_CLEAR) {
			return 0;
		}
	}

	if (pathfinder->validMovementPosition( d->crusher, d->acceptableSurfaces, to, from ) == false)
	{
		return 1;	// bail out
	}

	return 0;	// keep going
}

//-----------------------------------------------------------------------------

struct GroundPathPassableStruct
{
	Int		diameter;
	Bool	crusher;
};

/*static*/ Int Pathfinder::groundPathPassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData)
{
	const GroundPathPassableStruct* d = (const GroundPathPassableStruct*)userData;

	Int curDiameter = pathfinder->clearCellForDiameter(d->crusher, to_x, to_y, to->getLayer(), d->diameter);
	if (curDiameter==d->diameter) return 0;	//  good to go.
	if (from && to->getLayer() != LAYER_GROUND && from->getLayer() == to->getLayer()) {
		return 0;
	}

	return 1;	// failed.
}

//-----------------------------------------------------------------------------

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isLinePassable( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces,
																PathfindLayerEnum layer, const Coord3D& startWorld,
																const Coord3D& endWorld, Bool blocked,
																Bool allowPinched)
{
	LinePassableStruct info;
	//CRCDEBUG_LOG(("Pathfinder::isLinePassable(): %d %d %d ", m_ignoreObstacleID, m_isMapReady, m_isTunneling));

	info.crusher = obj && obj->getCrusherLevel() > 0;
	info.acceptableSurfaces = acceptableSurfaces;
	getRadiusAndCenter(obj, info.radius, info.centerInCell);
    info.movement = navigation::MovementValidator::prepare(obj, info.radius, info.centerInCell);
	info.blocked = blocked;
	info.allowPinched = allowPinched;

	ICoord2D start,end;
	worldToCell(&startWorld,&start);
	worldToCell(&endWorld,&end);
	const Int ret=visitNativeLine(*this,start,end,layer,[&](auto* from,auto* to,Int x,Int y) {
		return linePassableCallback(this,from,to,x,y,&info);
	});
	return ret == 0;
}

//-----------------------------------------------------------------------------

/**
 * Given two world-space points, check the line of sight between them for any impassible cells.
 * Uses Bresenham line algorithm from www.gamedev.net.
 */
Bool Pathfinder::isGroundPathPassable( Bool isCrusher, const Coord3D& startWorld, PathfindLayerEnum startLayer,
		const Coord3D& endWorld, Int pathDiameter)
{
	GroundPathPassableStruct info;

	info.diameter = pathDiameter;
	info.crusher = isCrusher;

	ICoord2D start,end;
	worldToCell(&startWorld,&start);
	worldToCell(&endWorld,&end);
	const Int ret=visitNativeLine(*this,start,end,startLayer,[&](auto* from,auto* to,Int x,Int y) {
		return groundPathPassableCallback(this,from,to,x,y,&info);
	});
	return ret == 0;
}

/**
 * Classify the cells under the bridge
 * If 'repaired' is true, bridge is repaired
 * If 'repaired' is false, bridge has been damaged to be impassable
 */
void Pathfinder::changeBridgeState( PathfindLayerEnum layer, Bool repaired)
{
	invalidateNavigationTopology();
	if (m_layers[layer].isUnused()) return;
	if (m_layers[layer].setDestroyed(!repaired)) {
	}
}

void Pathfinder::getRadiusAndCenter(const Object *obj, Int &iRadius, Bool &center)
{
	enum {MAX_RADIUS = 2};
	if (!obj)
	{
		center = true;
		iRadius = 0;
		return;
	}
	Real diameter = 2*obj->getGeometryInfo().getBoundingCircleRadius();
	if (diameter>PATHFIND_CELL_SIZE_F && diameter<2.0f*PATHFIND_CELL_SIZE_F) {
		diameter = 2.0f*PATHFIND_CELL_SIZE_F;
	}
	iRadius = REAL_TO_INT_FLOOR(diameter/PATHFIND_CELL_SIZE_F+0.3f);
	center = false;
	if (iRadius==0) iRadius++;
	if (iRadius&1)
	{
		center = true;
	}
	iRadius /= 2;
	if (iRadius > MAX_RADIUS)
	{
		iRadius = MAX_RADIUS;
		center = true;
	}
}

/**
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateGoal( Object *obj, const Coord3D *newGoalPos, PathfindLayerEnum layer)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	if (!ai->isDoingGroundMovement()) {
		// exception:sniped choppers are on ground
		Bool isUnmannedHelicopter = ( obj->isKindOf( KINDOF_PRODUCED_AT_HELIPAD ) && obj->isDisabledByType( DISABLED_UNMANNED  ) ) ;
		if (!isUnmannedHelicopter) {
			updateAircraftGoal(obj, newGoalPos);
			return;
		}
	}

	PathfindLayerEnum originalLayer = obj->getDestinationLayer();

	Bool layerChanged = originalLayer != layer;

	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;
	} else {
		doLayer = true;
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true;
		}
	}

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (!layerChanged && newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	removeGoal(obj);

	obj->setDestinationLayer(layer);
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	Bool warn = true;
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						//Units got stuck close to each other.  jba
					}
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (warn && cell->getGoalUnit()!=INVALID_ID && cell->getGoalUnit() != obj->getID()) {
						warn = false;
						//Units got stuck close to each other.  jba
					}
					cellNdx.x = i;
					cellNdx.y = j;
					cell->setGoalUnit(obj->getID(), cellNdx);
				}
			}
		}
	}

}

/**
 * Updates the goal cell for an ai unit.
 */
void Pathfinder::updateAircraftGoal( Object *obj, const Coord3D *newGoalPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	removeGoal(obj);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	if (ai->isDoingGroundMovement()) {
		return;  // shouldn't really happen, but just in case.
	}

	// For now, we are only doing HOVER, and WINGS.
	if (!ai->isAircraftThatAdjustsDestination()) return;

	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	if (centerInCell) {
		newCell.x = REAL_TO_INT_FLOOR(newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newGoalPos->y/PATHFIND_CELL_SIZE_F);
	} else {
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newGoalPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newGoalPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}

	ai->setPathfindGoalCell(newCell);
	Int i,j;
	ICoord2D cellNdx;

	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cell = getCell(LAYER_GROUND, i, j);
			if (cell) {
				cellNdx.x = i;
				cellNdx.y = j;
				cell->setGoalAircraft(obj->getID(), cellNdx);
			}
		}
	}

}

/**
 * Removes the goal cell for an ai unit.
 * Used for a unit that is going to be moving several times, like following a waypoint path,
 * or intentionally collides with other units (like a car bomb). jba
 */
void Pathfinder::removeGoal( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D goalCell = *ai->getPathfindGoalCell();

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	if (radius==0) {
		radius++;
	}
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	newCell.x = newCell.y = -1;
	if (newCell.x==goalCell.x && newCell.y == goalCell.y) {
		return;
	}
	ICoord2D cellNdx;
	ai->setPathfindGoalCell(newCell);
	Int i,j;
	if (goalCell.x>=0 && goalCell.y>=0) {
		for (i=goalCell.x-radius; i<goalCell.x+numCellsAbove; i++) {
			for (j=goalCell.y-radius; j<goalCell.y+numCellsAbove; j++) {
				PathfindCell	*cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					if (cell->getGoalUnit()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalUnit(INVALID_ID, cellNdx);
					}
					if (cell->getGoalAircraft()==obj->getID()) {
						cellNdx.x = i;
						cellNdx.y = j;
						cell->setGoalAircraft(INVALID_ID, cellNdx);
					}
				}
				if (obj->getDestinationLayer()!=LAYER_GROUND) {
					cell = getCell( obj->getDestinationLayer(), i, j);
					if (cell) {
						if (cell->getGoalUnit()==obj->getID()) {
							cellNdx.x = i;
							cellNdx.y = j;
							cell->setGoalUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/**
 * Updates the position cell for an ai unit.
 */
void Pathfinder::updatePos( Object *obj, const Coord3D *newPos)
{
	if (obj->isKindOf(KINDOF_IMMOBILE))
	{
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady)
		return;

	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
		return; // only consider ai objects.

	ICoord2D curCell = *ai->getCurPathfindCell();
	if (!ai->isDoingGroundMovement())
	{
		if (curCell.x>=0 && curCell.y>=0)
		{
			removePos(obj);
		}
		return;
	}

	Bool centerInCell;
	Int radius;
	ICoord2D newCell;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell)
		numCellsAbove++;
	if (centerInCell)
	{
		newCell.x = REAL_TO_INT_FLOOR(newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(newPos->y/PATHFIND_CELL_SIZE_F);
	}
	else
	{
		newCell.x = REAL_TO_INT_FLOOR(0.5f+newPos->x/PATHFIND_CELL_SIZE_F);
		newCell.y = REAL_TO_INT_FLOOR(0.5f+newPos->y/PATHFIND_CELL_SIZE_F);
	}
	if (newCell.x==curCell.x && newCell.y == curCell.y)
	{
		return;
	}

	PathfindLayerEnum layer = obj->getLayer();
	Bool doGround=false;
	Bool doLayer=false;
	if (layer==LAYER_GROUND) {
		doGround = true;	// just have to do ground
	} else {
		doLayer = true; // have to do the layer
		if (TheTerrainLogic->objectInteractsWithBridgeEnd(obj, layer)) {
			doGround = true; // In this case, have to both layer & ground, as they overlap here.
		}
	}

	ai->setCurPathfindCell(newCell);
	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
	for (i=newCell.x-radius; i<newCell.x+numCellsAbove; i++) {
		for (j=newCell.y-radius; j<newCell.y+numCellsAbove; j++) {
			PathfindCell	*cell;
			cellNdx.x = i;
			cellNdx.y = j;
			if (doLayer) {
				cell = getCell(layer, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
			if (doGround) {
				cell = getCell(LAYER_GROUND, i, j);
				if (cell) {
					cell->setPosUnit(obj->getID(), cellNdx);
				}
			}
		}
	}
}

/**
 * Removes the position cell flags for an ai unit.
 */
void Pathfinder::removePos( Object *obj)
{
	if (obj->isKindOf(KINDOF_IMMOBILE)) {
		// Only consider mobile.
		return;
	}
	if (!m_isMapReady) return;
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai) return; // only consider ai objects.
	ICoord2D curCell = *ai->getCurPathfindCell();
	Bool centerInCell;
	Int radius;
	getRadiusAndCenter(obj, radius, centerInCell);
	Int numCellsAbove = radius;
	if (centerInCell) numCellsAbove++;
	PathfindLayerEnum layer = obj->getLayer();

	ICoord2D newCell;
	newCell.x = newCell.y = -1;
	ai->setCurPathfindCell(newCell);

	Int i,j;
	ICoord2D cellNdx;
	//DEBUG_LOG(("Updating unit pos at cell %d, %d", newCell.x, newCell.y));
	if (curCell.x>=0 && curCell.y>=0) {
		for (i=curCell.x-radius; i<curCell.x+numCellsAbove; i++) {
			for (j=curCell.y-radius; j<curCell.y+numCellsAbove; j++) {
				cellNdx.x = i;
				cellNdx.y = j;
				PathfindCell	*cell = getCell(layer, i, j);
				if (cell) {
					if (cell->getPosUnit()==obj->getID()) {
						cell->setPosUnit(INVALID_ID, cellNdx);
					}
				}
				if (layer!=LAYER_GROUND) {
					// Remove from the ground, if present.
					cell = getCell(LAYER_GROUND, i, j);
					if (cell) {
						if (cell->getPosUnit()==obj->getID()) {
							cell->setPosUnit(INVALID_ID, cellNdx);
						}
					}
				}
			}
		}
	}
}

/**
 * Removes a mobile unit from the pathfind grid.
 */
void Pathfinder::removeUnitFromPathfindMap(  Object *obj )
{
	removePos(obj);
	removeGoal(obj);
}

Bool Pathfinder::moveAllies(Object *obj, Path *path)
{
#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
extern Bool g_UT_startTiming;
if (g_UT_startTiming) return false;
#endif
	if (!obj || !path || !TheGameLogic) return false;
	if (!obj->isKindOf(KINDOF_DOZER) && !obj->isKindOf(KINDOF_HARVESTER)) {
		// Harvesters & dozers want a clear path.
		if (!path->getBlockedByAlly()) {
			return FALSE; // Only move units if it is required.
		}
	}
	LatchRestore<Int> recursiveDepth(m_moveAlliesDepth, m_moveAlliesDepth+1);
	if (m_moveAlliesDepth > 2) {
		return false;
	}
	Bool centered=false;
	Int radius=0;
	getRadiusAndCenter(obj,radius,centered);
	const double corridor=(double(radius)+(centered?1.0:0.0))*PATHFIND_CELL_SIZE_F;
	const ObjectID ignored=obj->getAIUpdateInterface()
		? obj->getAIUpdateInterface()->getIgnoredObstacleID() : INVALID_ID;
	requestAlliedYields(obj,ignored,[&](const Object* candidate) {
		return pointNearPath(path,candidate,candidate->getLayer(),corridor);
	},obj->isKindOf(KINDOF_INFANTRY),path->getBlockedByAlly()!=0);
	return true;
}


/**
 * Moves an allied unit out of the path of another unit.
 * Uses A* algorithm.
 */
Path *Pathfinder::getMoveAwayFromPath(Object* obj, Object* otherObj,
    Path* pathToAvoid, Object* otherObj2, Path* pathToAvoid2)
{
    if (!m_isMapReady || !obj || !otherObj || !pathToAvoid || !obj->getAIUpdateInterface())
        return nullptr;
    const auto& locomotors=obj->getAIUpdateInterface()->getLocomotorSet();
    GroundRouteQuery query;
    query.object=obj;
    query.acceptableSurfaces=locomotors.getValidSurfaces();
    query.crusher=obj->getCrusherLevel()>0;
    query.isHuman=!obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
    getRadiusAndCenter(obj,query.radius,query.centerInCell);
    ICoord2D start;
    worldToCell(obj->getPosition(),&start);
    const auto corridorWidth=[&](const Object* other) {
        Int radius;
        Bool centered;
        getRadiusAndCenter(other,radius,centered);
        return query.radius*PATHFIND_CELL_SIZE_F-PATHFIND_CELL_SIZE_F/4+
            (query.centerInCell?PATHFIND_CELL_SIZE_F/2:0)+radius*PATHFIND_CELL_SIZE_F+
            (centered?PATHFIND_CELL_SIZE_F/2:0);
    };
    const Real firstWidth=corridorWidth(otherObj);
    const Real secondWidth=otherObj2?corridorWidth(otherObj2):firstWidth;
    std::vector<navigation::YieldSegment> segments;
    const auto capture=[&](Path* path,Real width) {
        if (!path) return;
        for (auto* node=path->getFirstNode();node && node->getNextOptimized();node=node->getNextOptimized()) {
            const auto* next=node->getNextOptimized();
            segments.push_back({node->getPosition()->x,node->getPosition()->y,
                next->getPosition()->x,next->getPosition()->y,width});
        }
    };
    capture(pathToAvoid,firstWidth);
    capture(pathToAvoid2,secondWidth);
    const navigation::YieldDestinationQuery yieldQuery{start.x,start.y,unsigned(obj->getLayer()),
        query.centerInCell!=0,PATHFIND_CELL_SIZE_F};
    // A deferred weighted query may outlive this stack frame.  Own the
    // captured corridors instead of leaving the destination callback with a
    // reference to a local YieldPaths object.
    const auto corridors=std::make_shared<const navigation::YieldPaths>(segments);
    const auto clearDestination=[this,obj,query,yieldQuery,corridors](Int x,Int y,PathfindLayerEnum layer) {
        return corridors->destination(yieldQuery,x,y,unsigned(layer),[this,obj,query](int xx,int yy,unsigned candidateLayer) {
            return checkDestination(obj,xx,yy,PathfindLayerEnum(candidateLayer),query.radius,query.centerInCell)!=0;
        },[](const navigation::YieldSegment& segment,
            float left,float top,float right,float bottom) {
            const Coord2D from{segment.x1,segment.y1},to{segment.x2,segment.y2};
            const Region2D bounds{{left,top},{right,bottom}};
            return LineInRegion(&from,&to,&bounds)!=0;
        });
    };
    const auto destinationQuery=navigation::makeDestinationQuery(obj,query.radius,query.centerInCell);
    const std::function<bool(Int,Int,PathfindLayerEnum)> capturedDestination=[corridors,yieldQuery](Int x,Int y,PathfindLayerEnum layer) {
        return corridors->destination(yieldQuery,x,y,unsigned(layer),
            [](int,int,unsigned) { return true; },
            [](const navigation::YieldSegment& segment,float left,float top,float right,float bottom) {
                const Coord2D from{segment.x1,segment.y1},to{segment.x2,segment.y2};
                const Region2D bounds{{left,top},{right,bottom}};
                return LineInRegion(&from,&to,&bounds)!=0;
            });
    };
    Path* path=nullptr;
    for (;;) {
        path=m_groundPlanner->findWeighted(query,obj->getPosition(),obj->getPosition(),
            locomotors.isDownhillOnly(),clearDestination,{}, {},nullptr,true,nullptr,
            &destinationQuery,nullptr,&capturedDestination);
        if (path || m_deferGroundQueries || !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    return path;
}


/** Patch to the exiting path from the current position, either because we became blocked,
  or because we had to move off the path to avoid other units. */
Path *Pathfinder::patchPath( const Object *obj, const LocomotorSet& locomotorSet,
        Path *originalPath, Bool blocked )
{
    if (!obj || !obj->getAIUpdateInterface() || !originalPath || !originalPath->getLastNode())
        return nullptr;
    GroundRouteQuery query;
    query.object = obj;
    query.acceptableSurfaces = locomotorSet.getValidSurfaces();
    getRadiusAndCenter(obj, query.radius, query.centerInCell);
    query.crusher = obj->getCrusherLevel() > 0;
    query.isHuman = !obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType() != PLAYER_COMPUTER;
    // The route repair search walks backwards over the current route and stops at
    // the first unusable node.  A repaired route may join any later usable
    // node, but must retain the original tail after that join.  Keep the
    // candidates in owned storage: a deferred continuation can outlive this
    // stack frame.
    struct Candidate { Int x, y; PathfindLayerEnum layer; };
    auto candidates = std::make_shared<std::vector<Candidate>>();
    ICoord2D startCell{};
    worldToCell(obj->getPosition(), &startCell);
    for (auto* node = originalPath->getLastNode();
         node && node != originalPath->getFirstNode(); node = node->getPrevious()) {
        ICoord2D cell{};
        worldToCell(node->getPosition(), &cell);
        TCheckMovementInfo movement{};
        movement.cell = cell;
        movement.layer = node->getLayer();
        movement.radius = query.radius;
        movement.centerInCell = query.centerInCell;
        movement.considerTransient = blocked;
        movement.acceptableSurfaces = query.acceptableSurfaces;
        if (std::abs(cell.x - startCell.x) > 2 || std::abs(cell.y - startCell.y) > 2)
            movement.considerTransient = false;
        if (!getCell(node->getLayer(), cell.x, cell.y) || !checkForMovement(obj, movement) ||
            movement.allyFixedCount || movement.enemyFixed)
            break;
        // DX9 accepts every usable tail node. The nearest one is only a
        // possible rejoin; enclosing it must not discard a farther entrance.
        candidates->push_back({cell.x, cell.y, node->getLayer()});
    }
    if (candidates->empty()) return nullptr;

    const auto destination = [this, obj, query, candidates](Int x, Int y, PathfindLayerEnum layer) {
        const auto found = std::find_if(candidates->begin(), candidates->end(),
            [=](const Candidate& candidate) {
                return candidate.x == x && candidate.y == y && candidate.layer == layer;
            });
        return found != candidates->end() &&
            checkDestination(obj, x, y, layer, query.radius, query.centerInCell);
    };
    const auto destinationQuery=navigation::makeDestinationQuery(obj,query.radius,query.centerInCell);
    const std::function<bool(Int,Int,PathfindLayerEnum)> capturedDestination=[candidates](Int x,Int y,PathfindLayerEnum layer) {
        return std::find_if(candidates->begin(),candidates->end(),[=](const Candidate& candidate) {
            return candidate.x==x && candidate.y==y && candidate.layer==layer;
        })!=candidates->end();
    };
    // A fast route cannot express a set of possible rejoin points.  The
    // weighted search also retains the captured occupancy behavior needed when
    // repairing a route, so use it for both synchronous and deferred calls.
    Path* repaired=nullptr;
    for (;;) {
        repaired=m_groundPlanner->findWeighted(query,obj->getPosition(),obj->getPosition(),
            locomotorSet.isDownhillOnly(),destination,{}, {},nullptr,true,nullptr,
            &destinationQuery,nullptr,&capturedDestination,2000);
        if (repaired || m_deferGroundQueries || !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    if (!repaired || !repaired->getLastNode()) return repaired;

    ICoord2D joinedCell{};
    worldToCell(repaired->getLastNode()->getPosition(), &joinedCell);
    PathNode* joined = nullptr;
    for (auto* node = originalPath->getLastNode();
         node && node != originalPath->getFirstNode(); node = node->getPrevious()) {
        ICoord2D cell{};
        worldToCell(node->getPosition(), &cell);
        if (cell.x == joinedCell.x && cell.y == joinedCell.y &&
            node->getLayer() == repaired->getLastNode()->getLayer()) {
            joined = node;
            break;
        }
    }
    if (!joined) {
        deleteInstance(repaired);
        return nullptr;
    }
    for (auto* node = joined->getNext(); node; node = node->getNext())
        repaired->appendNode(node->getPosition(), node->getLayer());
    // The repaired prefix was produced by the captured weighted graph and its
    // optimized links are already authoritative. Append the preserved tail as
    // a deterministic route-link chain instead of re-entering the retired
    // native line-of-sight optimizer.
    PathNode* link = repaired->getFirstNode();
    while (link && link->getNext()) {
        link->setNextOptimized(link->getNext());
        link = link->getNext();
    }
    repaired->markOptimized();
    return repaired;
}

/** Find a short, valid path to a location that obj can attack victim from.  */
Path *Pathfinder::findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon )
{
	// Weapon and visibility checks run on the simulation thread. Ordinary
	// synchronous queries evaluate only cells actually reached by the search.
	if (!m_isMapReady || !obj || !from || !weapon || (!victim && !victimPos))
		return nullptr;
	if (!victimPos) victimPos=victim->getPosition();
	Int radius=0; Bool centerInCell=true;
	getRadiusAndCenter(obj,radius,centerInCell);
	GroundRouteQuery query;
	query.object=obj; query.acceptableSurfaces=locomotorSet.getValidSurfaces();
	query.allowReservedGoal=true; query.radius=radius; query.centerInCell=centerInCell;
	query.crusher=obj->getCrusherLevel()>0;
	query.isHuman=!obj->getControllingPlayer() ||
		obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
	const auto source=*obj->getPosition();
	const auto targetPosition=*victimPos;
	const auto targetId=victim?victim->getID():INVALID_ID;
	const auto nativeGoal=[this,obj,targetId,targetPosition,weapon,query,source,radius,centerInCell]
		(int x,int y,PathfindLayerEnum layer) {
		// Slices run on the simulation thread. Own the position and resolve
		// object lifetime at each deterministic expansion boundary; never
		// retain the caller's stack coordinate or dereference a retired weapon.
		const auto* target=targetId!=INVALID_ID?TheGameLogic->findObjectByID(targetId):nullptr;
		if ((targetId!=INVALID_ID && (!target || target->isDestroyed())) ||
			obj->getCurrentWeapon()!=weapon) return false;
		auto* cell=getCell(layer,x,y);
		if (!cell) return false;
		Coord3D point;
		adjustCoordToCell(x,y,centerInCell,point,layer);
		const auto dx=point.x-source.x,dy=point.y-source.y;
		return dx*dx+dy*dy>=sqr(PATHFIND_CELL_SIZE_F*0.5f) &&
			validMovementPosition(query.crusher,query.acceptableSurfaces,cell) &&
			checkDestination(obj,x,y,layer,radius,centerInCell) &&
			weapon->isGoalPosWithinAttackRange(obj,&point,target,&targetPosition) &&
			!isAttackViewBlockedByObstacle(obj,point,target,targetPosition);
	};
	// DX9 limits newly admitted neighbours to 2,500 for attack approaches.
	// Do not first enumerate every firing position on every map layer: that
	// preparation exceeded the cost of the local search by orders of magnitude.
	const std::function<bool(int,int,PathfindLayerEnum)> goalPredicate=nativeGoal;
	const auto destinationQuery=navigation::makeDestinationQuery(obj,radius,centerInCell);
    Path* path=nullptr;
    for (;;) {
        path=m_groundPlanner->findWeighted(query,from,victimPos,locomotorSet.isDownhillOnly(),
            goalPredicate,{}, {},nullptr,true,nullptr,&destinationQuery,nullptr,&goalPredicate,2500);
        if (path || m_deferGroundQueries || !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    return path;

}

/** Find a short, valid path to a location that is safe from the repulsors.  */
Path *Pathfinder::findSafePath(const Object* obj,const LocomotorSet& locomotors,
    const Coord3D* from,const Coord3D* firstThreat,const Coord3D* secondThreat,Real radius)
{
    if (!m_isMapReady || !obj || !obj->getAIUpdateInterface() || !from || !firstThreat || !secondThreat ||
        !locomotors.getValidSurfaces()) return nullptr;
    GroundRouteQuery query;
    query.object=obj;query.acceptableSurfaces=locomotors.getValidSurfaces();
    query.crusher=obj->getCrusherLevel()>0;
    query.isHuman=!obj->getControllingPlayer() || obj->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
    getRadiusAndCenter(obj,query.radius,query.centerInCell);
    const navigation::FleeQuery threat{firstThreat->x,firstThreat->y,secondThreat->x,secondThreat->y,
        radius,MAX_SAFE_PATH_CELL_COUNT};
    ICoord2D start;
    worldToCell(obj->getPosition(),&start);
    const auto destination=[this,obj,query,goal=navigation::FleeGoal(threat)](int x,int y,PathfindLayerEnum layer) mutable {
        Coord3D point;
        adjustCoordToCell(x,y,query.centerInCell,point,layer);
        return goal.consider(point.x,point.y) && checkDestination(obj,x,y,layer,query.radius,query.centerInCell);
    };
    const auto nearestSafe=[this,obj,query,threat,start](int x,int y,PathfindLayerEnum layer,unsigned)->std::optional<double> {
        if (x==start.x && y==start.y && layer==obj->getLayer()) return {};
        if (!checkDestination(obj,x,y,layer,query.radius,query.centerInCell)) return {};
        Coord3D point;
        adjustCoordToCell(x,y,query.centerInCell,point,layer);
        return -double(navigation::FleeGoal::distanceSquared(threat,point.x,point.y));
    };
    const auto destinationQuery=navigation::makeDestinationQuery(obj,query.radius,query.centerInCell);
    Path* path=nullptr;
    for (;;) {
        path=m_groundPlanner->findWeighted(query,from,from,locomotors.isDownhillOnly(),destination,nearestSafe,
            {},nullptr,true,nullptr,&destinationQuery,&threat);
        if (path || m_deferGroundQueries || !m_groundPlanner->hasPending(obj->getID())) break;
        m_groundPlanner->commitCapturedSlice();
        m_groundPlanner->advanceCapturedSlice();
    }
    return path;
}


void Pathfinder::crc( Xfer *xfer )
{
	CRCDEBUG_LOG(("Pathfinder::crc() on frame %d", TheGameLogic->getFrame()));
	CRCDEBUG_LOG(("beginning CRC: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser( &m_extent, sizeof(IRegion2D) );
	CRCDEBUG_LOG(("m_extent: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferBool( &m_isMapReady );
	CRCDEBUG_LOG(("m_isMapReady: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferBool( &m_isTunneling );
	CRCDEBUG_LOG(("m_isTunneling: %8.8X", ((XferCRC *)xfer)->getCRC()));

	Int obsolete1 = 0;
	xfer->xferInt( &obsolete1 );

	xfer->xferUser(&m_ignoreObstacleID, sizeof(ObjectID));
	CRCDEBUG_LOG(("m_ignoreObstacleID: %8.8X", ((XferCRC *)xfer)->getCRC()));

    // Hash logical FIFO contents, independent of capacity, wrap position or hash buckets.
    UnsignedInt pending = static_cast<UnsignedInt>(m_pathRequests->size());
    xfer->xferUnsignedInt(&pending);
    for (UnsignedInt i = 0; i < pending; ++i) {
        UnsignedInt id = m_pathRequests->at(i);
        xfer->xferUnsignedInt(&id);
    }
    xfer->xferUser(&m_playerCommandSequence,sizeof(m_playerCommandSequence));

	xfer->xferInt(&m_numWallPieces);
	CRCDEBUG_LOG(("m_numWallPieces: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferUser(m_wallPieces, sizeof(m_wallPieces));

	CRCDEBUG_LOG(("m_wallPieces: %8.8X", ((XferCRC *)xfer)->getCRC()));

	xfer->xferReal(&m_wallHeight);
	CRCDEBUG_LOG(("m_wallHeight: %8.8X", ((XferCRC *)xfer)->getCRC()));
	xfer->xferInt(&m_cumulativeCellsAllocated);
	CRCDEBUG_LOG(("m_cumulativeCellsAllocated: %8.8X", ((XferCRC *)xfer)->getCRC()));

}

//-----------------------------------------------------------------------------
void Pathfinder::xfer( Xfer *xfer )
{
	// Version 1 had no payload. Version 2 retains pending orders in command
	// arrival order; sorting by object ID changes destination reservations.
	// Version 3 preserves the ordering of player commands within one tick.
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	if (version < 2) return;

	UnsignedInt pending = static_cast<UnsignedInt>(m_pathRequests->size());
	xfer->xferUnsignedInt(&pending);
	const Bool loading = xfer->getXferMode() == XFER_LOAD;
	if (loading) m_pathRequests->clear();
	for (UnsignedInt i = 0; i < pending; ++i) {
		UnsignedInt id = loading ? 0 : m_pathRequests->at(i);
		xfer->xferUnsignedInt(&id);
		if (loading) m_pathRequests->push(id);
	}
	if (version>=3) xfer->xferUser(&m_playerCommandSequence,sizeof(m_playerCommandSequence));
	else if (loading) m_playerCommandSequence=0;
	xfer->xferInt(&m_cumulativeCellsAllocated);
	xfer->xferBool(&m_isTunneling);
	xfer->xferUser(&m_ignoreObstacleID, sizeof(ObjectID));
	xfer->xferInt(&m_numWallPieces);
	xfer->xferUser(m_wallPieces, sizeof(m_wallPieces));
	xfer->xferReal(&m_wallHeight);
	if (loading) {
		m_loadedPathQueue = TRUE;
		invalidateNavigationSnapshots();
	}
}

//-----------------------------------------------------------------------------
void Pathfinder::loadPostProcess()
{

}

} // extern "C++"
