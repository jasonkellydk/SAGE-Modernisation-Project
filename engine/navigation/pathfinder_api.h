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

// AIPathfind.h
// AI pathfinding system
// Author: Michael S. Booth, October 2001

#pragma once

#include <cstdint>
#include <memory>

namespace navigation {
template<class Owner> class SearchWorkspace;
class GroundClearanceQueryMemo;
class PathRequestQueue;
class GroundRoutePlanner;
class MovementValidator;
class GroundPathBuilder;
}
namespace navigation::testing { class World; class Simulation; }

#include "Common/GameType.h"
#include "Common/GameMemory.h"
#include "Common/Snapshot.h"
//#include "GameLogic/Locomotor.h"	// no, do not include this, unless you like long recompiles
#include "GameLogic/LocomotorSet.h"
#include "GameLogic/GameLogic.h"

class Bridge;
class Object;
class Weapon;
class PathfindCell;
class Path;

// How close is close enough when moving.

#define PATHFIND_CLOSE_ENOUGH 1.0f
#define PATH_MAX_PRIORITY 0x7FFFFFFF

#define INFANTRY_MOVES_THROUGH_INFANTRY


//----------------------------------------------------------------------------------------------------------

/**
 * PathNodes are used to create a final Path to return from the
 * pathfinder.  Note that these are not used during the A* search.
 */
class PathNode : public MemoryPoolObject
{
	friend class Path;
public:
	explicit PathNode(Path* owner = nullptr);

	const Coord3D *getPosition() const { return &m_pos; }			///< return position of this node
	void setPosition(const Coord3D *pos);

	PathNode *getNext() { return m_next; }				///< return next node in the path
	PathNode *getPrevious() { return m_prev; }		///< return previous node in the path
	const PathNode *getNext() const { return m_next; }				///< return next node in the path
	const PathNode *getPrevious() const { return m_prev; }		///< return previous node in the path

	PathfindLayerEnum getLayer() const { return m_layer; }				///< return layer of this node.
	void setLayer(PathfindLayerEnum layer);

	void setNextOptimized( PathNode *node );

	PathNode *getNextOptimized(Coord2D* dir = nullptr, Real* dist = nullptr)  	///< return next node in optimized path
	{
		if (dir)
			*dir = m_nextOptiDirNorm2D;
		if (dist)
			*dist = m_nextOptiDist2D;
		return m_nextOpti;
	}

	const PathNode *getNextOptimized(Coord2D* dir = nullptr, Real* dist = nullptr) const  	///< return next node in optimized path
	{
		if (dir)
			*dir = m_nextOptiDirNorm2D;
		if (dist)
			*dist = m_nextOptiDist2D;
		return m_nextOpti;
	}

	void setCanOptimize(Bool canOpt);
	Bool getCanOptimize() const { return m_canOptimize;}

	/// given a list, prepend this node, return new list
	PathNode *prependToList( PathNode *list );

	/// given a node, append to this node
	void append( PathNode *list );

public:
	mutable Int					m_id; // Used in Path::xfer() to save & recreate the path list.

private:
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( PathNode, "PathNodePool"  );		///< @todo Set real numbers for mem alloc
	Path* m_owner;

	PathNode*						m_nextOpti;													///< next node in the optimized path
	PathNode*						m_next;															///< next node in the path
	PathNode*						m_prev;															///< previous node in the path
	Coord3D							m_pos;															///< position of node in space
	PathfindLayerEnum		m_layer;														///< Layer for this section.
	Bool								m_canOptimize;											///< True if this cell can be optimized out.

	Real								m_nextOptiDist2D;										///< if nextOpti is nonnull, the dist to it.
	Coord2D							m_nextOptiDirNorm2D;								///< if nextOpti is nonnull, normalized dir vec towards it.

};

// this doesn't actually seem to be a particularly useful win,
// performance-wise, so I didn't enable it. (srj)
#define NO_CPOP_STARTS_FROM_PREV_SEG

struct ClosestPointOnPathInfo
{
	Real								distAlongPath;
	Coord3D							posOnPath;
	PathfindLayerEnum		layer;
};

/**
 * This class encapsulates a "path" returned by the Pathfinder.
 */
class Path : public MemoryPoolObject, public Snapshot
{
	friend class PathNode;
	struct FollowingGeometry;
	std::unique_ptr<FollowingGeometry> m_followingGeometry;
	bool m_followingGeometryValid = false;
    void invalidateFollowing() { m_cpopValid = false; m_cpopRecentStart = nullptr; m_followingGeometryValid = false; }
public:
	Path();

	PathNode *getFirstNode() { return m_path; }
	PathNode *getLastNode() { return m_pathTail; }

	void updateLastNode( const Coord3D *pos );

	void prependNode( const Coord3D *pos, PathfindLayerEnum layer );				///< Create a new node at the head of the path
	void appendNode( const Coord3D *pos, PathfindLayerEnum layer );				///< Create a new node at the end of the path
	void setBlockedByAlly(Bool blocked) {m_blockedByAlly = blocked;}
	Bool getBlockedByAlly() {return m_blockedByAlly;}
	void optimize( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces, Bool blocked );			///< Optimize the path to discard redundant nodes

	void optimizeGroundPath( Bool crusher, Int diameter );			///< Optimize the ground path to discard redundant nodes

	/// Given a location, return nearest location on path, and along-path dist to end as function result
	void computePointOnPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D& pos, ClosestPointOnPathInfo& out);

	/// Given a location, return nearest location on path, and along-path dist to end as function result
	void peekCachedPointOnPath( Coord3D& pos ) const {pos = m_cpopOut.posOnPath;}

	/// Given a flight path, compute the distance to goal (0 if we are past it) & return the goal pos.
	Real computeFlightDistToGoal( const Coord3D *pos, Coord3D& goalPos );

	/// Given a location, return closest location on path, and along-path dist to end as function result
	void markOptimized() {m_isOptimized = true; invalidateFollowing();}
	// The modern planner may certify a two-node route against the immutable
	// terrain snapshot. The follower may use that certificate while the unit
	// is not blocked; blocked recovery re-enters the authoritative query.

protected:
	// snapshot interface
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

protected:
	enum {MAX_CPOP=2};			///< Reuse a nearby target for a short run; blocked units always recompute.
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( Path, "PathPool" );							///< @todo Set real numbers for mem alloc

	PathNode*		m_path;															///< The list of PathNode objects that define the path
	PathNode*		m_pathTail;
	Bool				m_isOptimized;											///< True if the path has been optimized
	Bool				m_blockedByAlly;										///< An ally needs to move off of this path.
	// caching info for computePointOnPath.
	Bool										m_cpopValid;
	Int											m_cpopCountdown;				///< We only return the same cpop MAX_CPOP times.  It is occasionally possible to get stuck.
	Coord3D									m_cpopIn;
	ClosestPointOnPathInfo	m_cpopOut;
	const PathNode*					m_cpopRecentStart;
};

//----------------------------------------------------------------------------------------------------------

// See GameType.h for
// enum {LAYER_INVALID = 0, LAYER_GROUND = 1, LAYER_TOP=2 };

// Fits in 4 bits for now
enum {MAX_WALL_PIECES = 128};

// Game-facing adapter over the indexed frontier and dense closed-state storage.
/**
 * This represents one cell in the pathfinding grid.
 * These cells categorize the world into idealized cellular states,
 * and are also used for efficient A* pathfinding.
 * @todo Optimize memory usage of pathfind grid.
 */
class PathfindCell
{
	friend class Pathfinder;
    friend class navigation::testing::World;
public:

	enum CellType
	{
		CELL_CLEAR		= 0x00,									///< clear, unobstructed ground
		CELL_WATER		= 0x01,									///< water area
		CELL_CLIFF		= 0x02,									///< steep altitude change
		CELL_RUBBLE		= 0x03,									///< Cell is occupied by rubble.
		CELL_OBSTACLE	= 0x04,									///< Occupied by a structure
		CELL_BRIDGE_IMPASSABLE = 0x05,				///< Piece of a bridge that is impassable.
		CELL_IMPASSABLE = 0x06								///< Just plain impassable except for aircraft.
	};

	enum CellFlags
	{
		NO_UNITS		= 0x00,						///< No units in this cell.
		UNIT_GOAL		= 0x01,						///< A unit is heading to this cell.
		UNIT_PRESENT_MOVING	= 0x02,		///< A unit is moving through this cell.
		UNIT_PRESENT_FIXED	= 0x03,		///< A unit is stationary in this cell.
		UNIT_GOAL_OTHER_MOVING	= 0x05		///< A unit is moving through this cell, and another unit has this as it's goal.
	};

	/// reset the cell
	void reset();

	PathfindCell();
	~PathfindCell();

	Bool setTypeAsObstacle( Object *obstacle, Bool isFence, const ICoord2D &pos );				///< flag this cell as an obstacle, from the given one
	Bool removeObstacle( Object *obstacle );				///< unflag this cell as an obstacle, from the given one
	void setType( CellType type );	///< set the cell type
	CellType getType() const { return (CellType)m_type; }				///< get the cell type
	CellFlags getFlags() const { return (CellFlags)m_flags; }				///< get the cell type
	Bool isAircraftGoal() const {return m_aircraftGoal != 0;}

	Bool isObstaclePresent( ObjectID objID ) const;					///< return true if the given object ID is registered as an obstacle in this cell

	Bool isObstacleTransparent() const;
	Bool isObstacleFence() const;

	/// Return estimated cost from given cell to reach goal cell
    UnsignedShort getXIndex() const;
    UnsignedShort getYIndex() const;
	Bool getPinched() const {return m_pinched;}
	void setPinched(Bool pinch) {m_pinched = pinch;	}

	Bool allocateInfo(const ICoord2D &pos);
	void releaseInfo();
	Bool hasInfo() const {return m_searchIndex != NO_SEARCH_INDEX;}
	void setGoalUnit(ObjectID unit, const ICoord2D &pos );
	void setGoalAircraft(ObjectID unit, const ICoord2D &pos );
	void setPosUnit(ObjectID unit, const ICoord2D &pos );
	ObjectID getGoalUnit() const;
	ObjectID getGoalAircraft() const;
	ObjectID getPosUnit() const;

	ObjectID getObstacleID() const;

	void setLayer( PathfindLayerEnum layer ) { m_layer = layer; }	///< set the cell layer
	PathfindLayerEnum getLayer() const { return (PathfindLayerEnum)m_layer; }				///< get the cell layer

	void setConnectLayer( PathfindLayerEnum layer ) { m_connectsToLayer = layer; }	///< set the cell layer	connect id
	PathfindLayerEnum getConnectLayer() const { return (PathfindLayerEnum)m_connectsToLayer; }				///< get the cell layer connect id

private:
    static constexpr UnsignedInt NO_SEARCH_INDEX = ~UnsignedInt(0);
    static navigation::SearchWorkspace<PathfindCell> s_search;
    static UnsignedInt searchEpoch();
    UnsignedInt m_searchIndex;
	ObjectID m_obstacleID;	                  ///< the object ID who overlaps this cell
	UnsignedInt m_obstacleIsFence : 1;        ///< True if occupied by a fence.
	UnsignedInt m_obstacleIsTransparent : 1;  ///< True if obstacle is transparent (undefined if obstacleid is invalid)

	UnsignedShort m_aircraftGoal : 1;         ///< This is an aircraft goal cell.
	UnsignedShort m_pinched : 1;              ///< This cell is surrounded by obstacle cells.
	UnsignedByte m_type : 4;                  ///< what type of cell terrain this is.
	UnsignedByte m_flags : 4;                 ///< what type of units are in or moving through this cell.
	UnsignedByte m_connectsToLayer : 4;       ///< This cell can pathfind onto this layer, if > LAYER_TOP.
	UnsignedByte m_layer : 4;                 ///< Layer of this cell.
};

typedef PathfindCell *PathfindCellP;


// how close a unit has to be in z to interact with the layer.
#define LAYER_Z_CLOSE_ENOUGH_F 10.0f
/**
 * This class represents a bridge in the map. This is effectively
 * a sub-rectangle of the big pathfind map.
 */
class PathfindLayer
{
public:
	PathfindLayer();
	~PathfindLayer();
public:
	void reset();
	Bool init(Bridge *theBridge, PathfindLayerEnum layer);
	void allocateCells(const IRegion2D *extent);
	void allocateCellsForWallLayer(const IRegion2D *extent, ObjectID *wallPieces, Int numPieces);
	void classifyCells();
	void classifyWallCells(ObjectID *wallPieces, Int numPieces);
	Bool setDestroyed(Bool destroyed);
	Bool isUnused(); // True if it doesn't contain a bridge.
	Bool isDestroyed() {return m_destroyed;} // True if it has been destroyed.
	PathfindCell *getCell(Int x, Int y);
	void getStartCellIndex(ICoord2D *start) {*start = m_startCell;}
	void getEndCellIndex(ICoord2D *end) {*end = m_endCell;}

	ObjectID getBridgeID();
	Bool isPointOnWall(ObjectID *wallPieces, Int numPieces, const Coord3D *pt);

#if defined(RTS_DEBUG)
	void doDebugIcons() ;
#endif
protected:
	void classifyLayerMapCell( Int i, Int j , PathfindCell *cell, Bridge *theBridge);
	void classifyWallMapCell( Int i, Int j, PathfindCell *cell , ObjectID *wallPieces, Int numPieces);

private:
	PathfindCell *m_blockOfMapCells;		///< Pathfinding map - contains iconic representation of the map
	PathfindCell **m_layerCells;		///< Pathfinding map indexes - contains matrix indexing into the map.
	Int m_width;		// Number of cells in x
	Int m_height;		// Number of cells in y
	Int m_xOrigin;	// Index of first cell in x
	Int m_yOrigin;	// Index of first cell in y
	ICoord2D m_startCell; // pathfind cell indexes for center cell on the from side.
	ICoord2D m_endCell; // pathfind cell indexes for center cell on the to side.

	PathfindLayerEnum m_layer;
	Bridge *m_bridge; // Corresponding bridge in TerrainLogic.
	Bool m_destroyed;


};


#define PATHFIND_CELL_SIZE		10
#define PATHFIND_CELL_SIZE_F	10.0f


struct TCheckMovementInfo;


/**
 * The pathfinding services interface provides access to the 3 expensive path find calls:
 * findPath, findClosestPath, and findAttackPath.
 * It is only available to units when their ai interface doPathfind method is called.
 * This allows the pathfinder to spread out the pathfinding over a number of frames
 * when a lot of units are trying to pathfind all at the same time.
 */
class PathfindServicesInterface {
public:
	// A deferred ground find/patch is neither a completed route nor a failure.
	virtual Bool isGroundPathPending(ObjectID) const { return false; }
	virtual Path* findPathOrClosest(Object*, const LocomotorSet&, const Coord3D*, Coord3D*, Bool blocked, Bool& usedFallback);
	virtual Path *findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Coord3D *to )=0;	///< Find a short, valid path between given locations
	/** Find a short, valid path to a location NEAR the to location.
		This succeeds when the destination is unreachable (like inside a building).
		If the destination is unreachable, it will adjust the to point.  */
	virtual Path *findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		Coord3D *to, Bool blocked, Real pathCostMultiplier, Bool moveAllies )=0;

	/** Find a short, valid path to a location that obj can attack victim from.  */
	virtual Path *findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon )=0;

	/** Patch to the exiting path from the current position, either because we became blocked,
  or because we had to move off the path to avoid other units. */
	virtual Path *patchPath( const Object *obj, const LocomotorSet& locomotorSet,
		Path *originalPath, Bool blocked ) = 0;

	/** Find a short, valid path to a location that is away from the repulsors.  */
	virtual Path *findSafePath( const Object *obj, const LocomotorSet& locomotorSet,
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius ) = 0;

};

/**
 * The Pathfinding engine itself.
 */
class Pathfinder : PathfindServicesInterface, public Snapshot
{
	friend class PathfindCell;
	friend class navigation::testing::World;
	friend class navigation::testing::Simulation;
// The following routines are private, but available through the doPathfind callback to aiInterface. jba.
private:
    Bool isGroundPathPending(ObjectID) const override;
	virtual Path *findPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to) override;	///< Find a short, valid path between given locations
	Path* findPathOrClosest(Object*, const LocomotorSet&, const Coord3D*, Coord3D*, Bool, Bool&) override;
	Path* findClosestPathInternal(Object*, const LocomotorSet&, const Coord3D*, Coord3D*, Bool, Real, Bool*, Bool exactFirst=false);
	/** Find a short, valid path to a location NEAR the to location.
		This succeeds when the destination is unreachable (like inside a building).
		If the destination is unreachable, it will adjust the to point.  */
	virtual Path *findClosestPath( Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		Coord3D *to, Bool blocked, Real pathCostMultiplier, Bool moveAllies ) override;

	/** Find a short, valid path to a location that obj can attack victim from.  */
	virtual Path *findAttackPath( const Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Object *victim, const Coord3D* victimPos, const Weapon *weapon ) override;

	/** Find a short, valid path to a location that is away from the repulsors.  */
	virtual Path *findSafePath( const Object *obj, const LocomotorSet& locomotorSet,
		const Coord3D *from, const Coord3D* repulsorPos1, const Coord3D* repulsorPos2, Real repulsorRadius ) override;

	/** Patch to the exiting path from the current position, either because we became blocked,
  or because we had to move off the path to avoid other units. */
	virtual Path *patchPath( const Object *obj, const LocomotorSet& locomotorSet,
		Path *originalPath, Bool blocked ) override;

public:
	Pathfinder();
	~Pathfinder() ;
	void invalidateNavigationSnapshots(); ///< Discard modern immutable snapshots after save restoration
	Bool wasPathQueueLoaded() const; ///< Old saves have no navigation request block
	void setGroundQueriesDeferred(Bool deferred);
	Bool groundQueriesDeferred() const;

	void reset();														///< Reset system in preparation for new map

	// --------------- inherited from Snapshot interface --------------
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

	Bool clientSafeQuickDoesPathExist( const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to );  ///< Can we build any path at all between the locations	(terrain & buildings check - fast)
	Bool clientSafeQuickDoesPathExistForUI( const LocomotorSet& locomotorSet, const Coord3D *from, const Coord3D *to );  ///< Can we build any path at all between the locations	(terrain only - fast)
	Bool slowDoesPathExist( Object *obj, const Coord3D *from,
		const Coord3D *to, ObjectID ignoreObject=INVALID_ID );  ///< Can we build any path at all between the locations	(terrain, buildings & units check - slower)

	Bool queueForPath(ObjectID id);	 ///< The object wants to request a pathfind, so put it on the list to process.
    std::uint64_t nextPlayerCommandSequence() { return ++m_playerCommandSequence; }
	void processPathfindQueue(); ///< Process some or all of the queued pathfinds.
	void forceMapRecalculation();	///< Force pathfind map recomputation. If region is given, only that area is recomputed

	/** Returns an aircraft path to the goal.  */
	Path *getAircraftPath( const Object *obj, const Coord3D *to);
	Path *findGroundPath( const Coord3D *from, const Coord3D *to, Int pathRadius,
		Bool crusher);	///< Find a short, valid path of the desired width on the ground.

	void addObjectToPathfindMap( class Object *obj );				///< Classify the given object's cells in the map
	void removeObjectFromPathfindMap( class Object *obj );	///< De-classify the given object's cells in the map

	void removeUnitFromPathfindMap( Object *obj );	///< De-classify the given mobile unit's cells in the map
	void updateGoal( Object *obj, const Coord3D *newGoalPos, PathfindLayerEnum layer);		///< Update the given mobile unit's cells in the map
	void updateAircraftGoal( Object *obj, const Coord3D *newGoalPos);		///< Update the given aircraft unit's cells in the map
	void removeGoal( Object *obj);		///< Removes the given mobile unit's goal cells in the map
	void updatePos( Object *obj, const Coord3D *newPos);		///< Update the given mobile unit's cells in the map
	void removePos( Object *obj);		///< Removes the unit's position cells from the map

	Bool moveAllies(Object *obj, Path *path);

	// NOTE - The object MUST NOT MOVE between the call to createAWall... and removeWall...
	// or BAD THINGS will happen.  jba.
	void createAWallFromMyFootprint( Object *obj ) {internal_classifyObjectFootprint(obj, true);}  // Temporarily treat this object as an obstacle.
	void removeWallFromMyFootprint( Object *obj ){internal_classifyObjectFootprint(obj, false);}   // Undo createAWallFromMyFootprint.

	Path *getMoveAwayFromPath(Object *obj, Object *otherObj, Path *pathToAvoid, Object *otherObj2, Path *pathToAvoid2);

	void changeBridgeState( PathfindLayerEnum layer, Bool repaired );

	Bool findBrokenBridge(const LocomotorSet &locomotorSet, const Coord3D *from, const Coord3D *to, ObjectID *bridgeID);

	void newMap();

	PathfindCell *getCell( PathfindLayerEnum layer, Int x, Int y );							///< Return the cell at grid coords (x,y)
	PathfindCell *getCell( PathfindLayerEnum layer, const Coord3D *pos );				///< Given a position, return associated grid cell
	PathfindCell *getClippedCell( PathfindLayerEnum layer, const Coord3D *pos );				///< Given a position, return associated grid cell
	void clip(Coord3D *from, Coord3D *to);
	Bool worldToCell( const Coord3D *pos, ICoord2D *cell );	///< Given a world position, return grid cell coordinate

	const ICoord2D *getExtent() const {return &m_extent.hi;}

	void setIgnoreObstacleID( ObjectID objID );					///< if non-zero, the pathfinder will ignore the given obstacle

	Bool validMovementPosition( Bool isCrusher, LocomotorSurfaceTypeMask acceptableSurfaces, PathfindCell *toCell, PathfindCell *fromCell = nullptr );		///< Return true if given position is a valid movement location
	Bool validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, Int x, Int y );					///< Return true if given position is a valid movement location
	Bool validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, const Coord3D *pos );		///< Return true if given position is a valid movement location
	Bool validMovementTerrain( PathfindLayerEnum layer, const Locomotor* locomotor, const Coord3D *pos );		///< Return true if given position is a valid movement location

	Locomotor* chooseBestLocomotorForPosition(PathfindLayerEnum layer,  LocomotorSet* locomotorSet, const Coord3D* pos );

	Bool isViewBlockedByObstacle(const Object* obj, const Object* objOther);	///< Return true if the straight line between the given points contains any obstacle, and thus blocks vision

	Bool isAttackViewBlockedByObstacle(const Object* obj, const Coord3D& attackerPos,  const Object* victim, const Coord3D& victimPos);	///< Return true if the straight line between the given points contains any obstacle, and thus blocks vision

	Bool isLinePassable( const Object *obj, LocomotorSurfaceTypeMask acceptableSurfaces,
		PathfindLayerEnum layer, const Coord3D& startWorld, const Coord3D& endWorld,
		Bool blocked, Bool allowPinched );	///< Return true if the straight line between the given points is passable

	void moveAlliesAwayFromDestination( Object *obj,const Coord3D& destination);

	Bool isGroundPathPassable( Bool isCrusher, const Coord3D& startWorld, PathfindLayerEnum startLayer,
		const Coord3D& endWorld, Int pathDiameter);	///< Return true if the straight line between the given points is passable

	// for debugging
	const Coord3D *getDebugPathPosition();
	void setDebugPathPosition( const Coord3D *pos );
	Path *getDebugPath();
	void setDebugPath( Path *debugpath );

	void recordNavigationWork(Int amount);

	// Adjusts the destination to a spot near dest that is not occupied by other units.
	Bool adjustDestination(Object *obj, const LocomotorSet& locomotorSet,
		Coord3D *dest, const Coord3D *groupDest=nullptr);
    Bool projectFormationDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest);

	// Adjusts the destination to a spot near dest for landing that is not occupied by other units.
	Bool adjustToLandingDestination(Object *obj, Coord3D *dest);

	// Adjusts the destination to a spot that can attack target that is not occupied by other units.
	Bool adjustTargetDestination(const Object *obj, const Object *target, const Coord3D *targetPos,
		const Weapon *weapon, Coord3D *dest);

	// Adjusts destination to a spot near dest that is possible to path to.
	Bool adjustToPossibleDestination(Object *obj, const LocomotorSet& locomotorSet, Coord3D *dest);

	void snapPosition(Object *obj, Coord3D *pos); // Snaps the current position to it's grid location.
	void snapClosestGoalPosition(Object *obj, Coord3D *pos); // Snaps the current position to a good goal position.
	Bool goalPosition(Object *obj, Coord3D *pos); // Returns the goal position on the grid.

	PathfindLayerEnum addBridge(Bridge *theBridge); // Adds a bridge layer, and returns the layer id.

	void addWallPiece(Object *wallPiece); // Adds a wall piece.
	void removeWallPiece(Object *wallPiece);  // Removes a wall piece.
	Real getWallHeight() {return m_wallHeight;}
	Bool isPointOnWall(const Coord3D *pos);

	void updateLayer(Object *obj, PathfindLayerEnum layer); ///< Updates object's layer.

	static void classifyMapCell( Int x, Int y, PathfindCell *cell);					///< Classify the given map cell
	Int computeGroundClearance(Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter);
	Int clearCellForDiameter( Bool crusher, Int cellX, Int cellY, PathfindLayerEnum layer, Int pathDiameter );		///< Return true if given position is a valid movement location

	// Navigation work and timing diagnostics.
	struct NavigationStats
	{
		// Test/profiling counters for the modern queued boundary. They measure
		// only navigation dispatch, not the rest of the game tick.
		std::uint64_t lastQueueNanoseconds = 0;
		std::uint64_t maximumQueueNanoseconds = 0;
		std::uint64_t lastSliceNanoseconds = 0;
		std::uint64_t maximumSliceNanoseconds = 0;
		std::uint32_t maximumSliceObject = 0;
		std::uint64_t maximumSliceWork = 0;
		std::int32_t maximumSliceStartX = 0;
		std::int32_t maximumSliceStartY = 0;
		std::int32_t maximumSliceGoalX = 0;
		std::int32_t maximumSliceGoalY = 0;
		std::uint64_t lastCommitNanoseconds = 0;
		std::uint64_t lastRequestNanoseconds = 0;
		std::uint64_t lastDispatchNanoseconds = 0;
	};
	NavigationStats getNavigationStats() const;

	struct GroundRouteQuery
	{
		const Object *object = nullptr;
		LocomotorSurfaceTypeMask acceptableSurfaces = LOCOMOTORSURFACE_GROUND;
		Int radius = 0;
		Bool centerInCell = true;
		Bool crusher = false;
		Int pathDiameter = 0;
		Bool usePathDiameter = false;
		Bool isHuman = true;
        Bool considerTransient = false;
        Bool allowReservedGoal = false;
	};

protected:
	Bool checkForAdjust(Object *, const LocomotorSet& locomotorSet, Bool isHuman, Int cellX, Int cellY,
		PathfindLayerEnum layer, Int iRadius, Bool center,Coord3D *dest, const Coord3D *groupDest) ;
	Bool checkForLanding(Int cellX, Int cellY,
		PathfindLayerEnum layer, Int iRadius, Bool center,Coord3D *dest) ;
	Bool checkForTarget(const Object *obj, 	Int cellX, Int cellY, const Weapon *weapon,
																const Object *victim, const Coord3D *victimPos,
																Int iRadius, Bool center,Coord3D *dest) ;
	Bool checkForPossible(Bool isCrusher, const Coord3D* from, Bool center, const LocomotorSet& locomotorSet,
		Int cellX, Int cellY, PathfindLayerEnum layer, Coord3D *dest, Bool startingInObstacle) ;
	void getRadiusAndCenter(const Object *obj, Int &iRadius, Bool &center);
	void adjustCoordToCell(Int cellX, Int cellY, Bool centerInCell, Coord3D &pos, PathfindLayerEnum layer);
	Bool checkDestination(const Object *obj, Int cellX, Int cellY, PathfindLayerEnum layer, Int iRadius, Bool centerInCell);
	Bool checkForMovement(const Object *obj, TCheckMovementInfo &info);
	Bool segmentIntersectsTallBuilding(const PathNode *curNode, PathNode *nextNode,
		ObjectID ignoreBuilding, Coord3D *insertPos1, Coord3D *insertPos2, Coord3D *insertPos3);	///< Return true if the straight line between the given points intersects a tall building.
	Bool circleClipsTallBuilding(const Coord3D *from, const Coord3D *to, Real radius, ObjectID ignoreBuilding, Coord3D *adjustTo);	///< Return true if the circle at the end of the line between the given points intersects a tall building.

	Int checkPathCost(Object *obj, const LocomotorSet& locomotorSet, const Coord3D *from,
		const Coord3D *to);

	void tightenPath(Object *obj, const LocomotorSet& locomotorSet, Coord3D *from,
		const Coord3D *to);

	/**
		return 0 to continue iterating the line, nonzero to terminate the iteration.
		the nonzero result will be returned as the result of iterateCellsAlongLine().
		iterateCellsAlongLine will return zero if it completes.
	*/
	typedef Int (*CellAlongLineProc)(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	Int iterateCellsAlongLine(const Coord3D& startWorld, const Coord3D& endWorld,
		PathfindLayerEnum layer, CellAlongLineProc proc, void* userData);

	Int iterateCellsAlongLine(const ICoord2D &start, const ICoord2D &end,
		PathfindLayerEnum layer, CellAlongLineProc proc, void* userData);

	static Int linePassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int groundPathPassableCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int lineBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int tightenPathCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
	static Int attackBlockedByObstacleCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);
 	static Int moveAlliesDestinationCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);

	static Int segmentIntersectsBuildingCallback(Pathfinder* pathfinder, PathfindCell* from, PathfindCell* to, Int to_x, Int to_y, void* userData);

	void classifyMap();					///< Classify all cells in grid as obstacles, etc
	void invalidateNavigationTopology();
	void invalidateNavigationTopology(const IRegion2D& region);
	void classifyObjectFootprint( Object *obj, Bool insert );	/** Classify the cells under the given object
																																If 'insert' is true, object is being added
																																If 'insert' is false, object is being removed */
	void internal_classifyObjectFootprint( Object *obj, Bool insert );	/** Classify the cells under the given object
																																If 'insert' is true, object is being added
																																If 'insert' is false, object is being removed */
	void classifyFence( Object *obj, Bool insert );	/** Classify the cells under the given fence object. */
	void classifyUnitFootprint( Object *obj, Bool insert, Bool remove, Bool update );	/** Classify the cells under the given object If 'insert' is true, object is being added */
	/// Convert world coordinate to array index
	void worldToGrid( const Coord3D *pos, ICoord2D *cellIndex );

	void materializeGroundPath(Path *path, Bool center);

	static LocomotorSurfaceTypeMask validLocomotorSurfacesForCellType(PathfindCell::CellType t);

	bool checkCellOutsideExtents(ICoord2D& cell);

#if defined(RTS_DEBUG)
	void doDebugIcons() ;
#endif

private:
	/// This uses WAY too much memory.  Should at least be array of pointers to cells w/ many fewer cells
	PathfindCell *m_blockOfMapCells;		///< Pathfinding map - contains iconic representation of the map
	PathfindCell **m_map;		///< Pathfinding map indexes - contains matrix indexing into the map.
	IRegion2D m_extent;														///< Grid extent limits
	IRegion2D m_logicalExtent;										///< Logical grid extent limits


  Bool m_isMapReady;														///< True if all cells of map have been classified
	Bool m_isTunneling;														///< True if path started in an obstacle

	Int m_frameToShowObstacles;										///< Time to redraw obstacles.  For debug output.

	Coord3D debugPathPos;													///< Used for visual debugging
	Path *debugPath;															///< Used for visual debugging

	ObjectID m_ignoreObstacleID;									///< Ignore the given obstacle

	PathfindLayer m_layers[LAYER_LAST+1];

	ObjectID			m_wallPieces[MAX_WALL_PIECES];
	Int						m_numWallPieces;
	Real					m_wallHeight;

	Int						m_moveAlliesDepth;


	// Pathfind queue
	navigation::PathRequestQueue* m_pathRequests;
    std::uint64_t m_playerCommandSequence=0;
	Bool m_loadedPathQueue = FALSE; // Load-format metadata, not simulation state
	Int						m_cumulativeCellsAllocated;
	std::uint64_t				m_lastQueueNanoseconds = 0;
	std::uint64_t				m_maximumQueueNanoseconds = 0;
	std::uint64_t				m_lastRequestNanoseconds = 0;
	std::uint64_t				m_lastDispatchNanoseconds = 0;
	navigation::GroundClearanceQueryMemo* m_groundClearanceMemo;

    friend class navigation::GroundRoutePlanner;
    friend class navigation::MovementValidator;
    friend class navigation::GroundPathBuilder;
    std::unique_ptr<navigation::GroundRoutePlanner> m_groundPlanner;
    Bool m_deferGroundQueries=FALSE;
    Bool m_priorityGroundQuery=FALSE;

};


inline void Pathfinder::setIgnoreObstacleID( ObjectID objID )
{
	m_ignoreObstacleID = objID;
}

inline void Pathfinder::worldToGrid( const Coord3D *pos, ICoord2D *cellIndex )
{
	cellIndex->x = REAL_TO_INT(pos->x/PATHFIND_CELL_SIZE);
	cellIndex->y = REAL_TO_INT(pos->y/PATHFIND_CELL_SIZE);
}

inline Bool Pathfinder::validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, Int x, Int y )
{
	return validMovementPosition( isCrusher, locomotorSet.getValidSurfaces(), getCell( layer, x, y ) );
}

inline Bool Pathfinder::validMovementPosition( Bool isCrusher, PathfindLayerEnum layer, const LocomotorSet& locomotorSet, const Coord3D *pos )
{

	Int x = REAL_TO_INT(pos->x/PATHFIND_CELL_SIZE);
	Int y = REAL_TO_INT(pos->y/PATHFIND_CELL_SIZE);

	return validMovementPosition( isCrusher, layer, locomotorSet, x, y );
}

inline const Coord3D *Pathfinder::getDebugPathPosition()
{
	return &debugPathPos;
}

inline void Pathfinder::setDebugPathPosition( const Coord3D *pos )
{
	debugPathPos = *pos;
}

inline Path *Pathfinder::getDebugPath()
{
	return debugPath;
}

inline void Pathfinder::addObjectToPathfindMap( class Object *obj )
{
	classifyObjectFootprint( obj, true );
}

inline void Pathfinder::removeObjectFromPathfindMap( class Object *obj )
{
	classifyObjectFootprint( obj, false );
}

inline PathfindCell *Pathfinder::getCell( PathfindLayerEnum layer, Int x, Int y )
{
	if (x >= m_extent.lo.x && x <= m_extent.hi.x &&
		y >= m_extent.lo.y && y <= m_extent.hi.y)
	{
		PathfindCell *cell = nullptr;
		if (layer > LAYER_GROUND && layer <= LAYER_LAST)
		{
			cell = m_layers[layer].getCell(x, y);
			if (cell)
				return cell;
		}
		return &m_map[x][y];
	}
	else
	{
		return nullptr;
	}
}

inline PathfindCell *Pathfinder::getCell( PathfindLayerEnum layer, const Coord3D *pos )
{
	ICoord2D cell;
	Bool overflow = worldToCell( pos, &cell );
	if (overflow) return nullptr;
	return getCell( layer, cell.x, cell.y );
}

inline PathfindCell *Pathfinder::getClippedCell( PathfindLayerEnum layer, const Coord3D *pos)
{
	ICoord2D cell;
	worldToCell( pos, &cell );
	return getCell( layer, cell.x, cell.y );
}

inline Bool Pathfinder::worldToCell( const Coord3D *pos, ICoord2D *cell )
{
	cell->x = REAL_TO_INT_FLOOR(pos->x/PATHFIND_CELL_SIZE);
	cell->y = REAL_TO_INT_FLOOR(pos->y/PATHFIND_CELL_SIZE);
	Bool overflow = false;
	if (cell->x < m_extent.lo.x) {overflow = true; cell->x = m_extent.lo.x;}
	if (cell->y < m_extent.lo.y) {overflow = true; cell->y = m_extent.lo.y;}
	if (cell->x > m_extent.hi.x) {overflow = true; cell->x = m_extent.hi.x;}
	if (cell->y > m_extent.hi.y) {overflow = true; cell->y = m_extent.hi.y;}
	return overflow;
}
