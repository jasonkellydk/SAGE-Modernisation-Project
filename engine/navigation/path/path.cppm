module;
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "PreRTS.h"
#include "engine/navigation/pathfinder_api.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "Common/CRCDebug.h"

static inline Int IABS(Int value) { return value < 0 ? -value : value; }

export module engine.navigation.path;
import engine.debug;
import engine.navigation.movement.following.route_target;
import engine.navigation.path.smoothing;

extern "C++" {
// Modern game-facing route representation and movement geometry.
struct Path::FollowingGeometry {
	navigation::following::RouteGeometry<unsigned> route;
	std::vector<navigation::following::TargetEdge> edges;
	Coord3D lastPoint{};
	PathfindLayerEnum firstLayer=LAYER_GROUND;
};

PathNode::PathNode(Path* owner) :
	m_owner(owner),
	m_nextOpti(nullptr),
	m_next(nullptr),
	m_prev(nullptr),
	m_nextOptiDist2D(0),
	m_canOptimize(false),
	m_id(-1)
{
	m_nextOptiDirNorm2D.x = 0;
	m_nextOptiDirNorm2D.y = 0;
	m_pos.zero();
	m_layer = LAYER_INVALID;
}

//-----------------------------------------------------------------------------------
PathNode::~PathNode()
{
}

void PathNode::setPosition(const Coord3D* pos)
{
	m_pos = *pos;
	// Optimized edges can skip raw nodes. Refresh every incoming edge as well
	// as this node's outgoing edge when an already-owned waypoint moves.
	if (m_owner) {
		m_owner->invalidateFollowing();
		for (auto* node = m_owner->getFirstNode(); node; node = node->getNext()) {
			if (node->m_nextOpti == this) node->setNextOptimized(this);
		}
	}
	if (m_nextOpti) setNextOptimized(m_nextOpti);
}

void PathNode::setLayer(PathfindLayerEnum layer)
{
	if (m_layer == layer) return;
	m_layer = layer;
	if (m_owner) m_owner->invalidateFollowing();
}

void PathNode::setCanOptimize(Bool canOpt)
{
	if (m_canOptimize == canOpt) return;
	m_canOptimize = canOpt;
	if (m_owner) m_owner->invalidateFollowing();
}

//-----------------------------------------------------------------------------------
void PathNode::setNextOptimized(PathNode *node)
{
	if (m_owner) m_owner->invalidateFollowing();
	m_nextOpti = node;
	if (node)
	{
		m_nextOptiDirNorm2D.x = node->getPosition()->x - getPosition()->x;
		m_nextOptiDirNorm2D.y = node->getPosition()->y - getPosition()->y;
		m_nextOptiDist2D = m_nextOptiDirNorm2D.length();
		if (m_nextOptiDist2D == 0.0f)
		{
			//engine::debug::log_info("Warning - Path Seg length == 0, adjusting. john a.");
			m_nextOptiDist2D = 0.01f;
		}
		m_nextOptiDirNorm2D.x /= m_nextOptiDist2D;
		m_nextOptiDirNorm2D.y /= m_nextOptiDist2D;
	}
	else
	{
		m_nextOptiDist2D = 0;
	}
}

//-----------------------------------------------------------------------------------
/// given a list, prepend this node, return new list
PathNode *PathNode::prependToList( PathNode *list )
{
	if (!m_owner && list) m_owner = list->m_owner;
	if (m_owner) m_owner->invalidateFollowing();
	m_next = list;
	if (list)
		list->m_prev = this;
	m_prev = nullptr;
	return this;
}

//-----------------------------------------------------------------------------------
/// given a node, append new node to this.
void PathNode::append( PathNode *newNode )
{
	newNode->m_owner = m_owner;
	if (m_owner) m_owner->invalidateFollowing();
	newNode->m_next = this->m_next;
	newNode->m_prev = this;
	if (newNode->m_next) {
		newNode->m_next->m_prev = newNode;
	}
	this->m_next = newNode;

}

//-----------------------------------------------------------------------------------
Path::Path():
m_path(nullptr),
m_pathTail(nullptr),
m_isOptimized(FALSE),
m_blockedByAlly(FALSE),
m_cpopRecentStart(nullptr),
m_cpopCountdown(MAX_CPOP),
m_cpopValid(FALSE)
{
	m_cpopIn.zero();
	m_cpopOut.distAlongPath=0;
	m_cpopOut.layer = LAYER_GROUND;
	m_cpopOut.posOnPath.zero();
}

Path::~Path()
{
	PathNode *node, *nextNode;

	// delete all of the path nodes
	for( node = m_path; node; node = nextNode )
	{
		nextNode = node->getNext();
		deleteInstance(node);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void Path::crc( Xfer *xfer )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void Path::xfer( Xfer *xfer )
{
  // version
  XferVersion currentVersion = 1;
  XferVersion version = currentVersion;
  xfer->xferVersion( &version, currentVersion );

	PathNode *node = m_path;
	Int count = 0;
	while (node) {
		count++;
		node = node->getNext();
	}
	xfer->xferInt(&count);

	if (xfer->getXferMode() == XFER_SAVE)	{
		node = m_pathTail;  // Write them out backwards.
		while (node) {
			node->m_id = count;
			xfer->xferInt(&count);
			Coord3D pos = *node->getPosition();
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer = node->getLayer();
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt = node->getCanOptimize();
			xfer->xferBool(&canOpt);
			Int id = -1;
			if (node->getNextOptimized()) {
				id = node->getNextOptimized()->m_id;
			}
			xfer->xferInt(&id);
			count--;
			node = node->getPrevious();
		}
		engine::debug::invariant((count==0), "count==0", __FILE__, __LINE__, "Wrong data count");
	} else {
		invalidateFollowing();
		while (count) {
			Int nodeId;
			xfer->xferInt(&nodeId);
			engine::debug::invariant((nodeId==count), "nodeId==count", __FILE__, __LINE__, "Bad data");
			Coord3D pos;
			xfer->xferCoord3D(&pos);
			PathfindLayerEnum layer;
			xfer->xferUser(&layer, sizeof(layer));
			Bool canOpt;
			xfer->xferBool(&canOpt);
			Int optID = -1;
			xfer->xferInt(&optID);
			PathNode *node = newInstance(PathNode);
			node->m_id = nodeId;
			node->setPosition(&pos);
			node->setLayer(layer);
			node->setCanOptimize(canOpt);
			PathNode *optNode = nullptr;
			if (optID > 0) {
				optNode = m_path;
				while (optNode && optNode->m_id != optID) {
					optNode = optNode->getNext();
				}
				engine::debug::invariant((optNode && optNode->m_id == optID), "optNode && optNode->m_id == optID", __FILE__, __LINE__, "Could not find optimized link.");
			}
			node->m_owner = this;
			m_path = node->prependToList(m_path);
			if (m_pathTail == nullptr)
				m_pathTail = node;
			if (optNode) {
				node->setNextOptimized(optNode);
			}
			count--;
		}
	}

	xfer->xferBool(&m_isOptimized);
	Int obsolete1 = 0;
	xfer->xferInt(&obsolete1);
	UnsignedInt obsolete2;
	xfer->xferUnsignedInt(&obsolete2);
	xfer->xferBool(&m_blockedByAlly);


#if defined(RTS_DEBUG)
	if (TheGlobalData->m_debugAI == AI_DEBUG_PATHS)
	{
		extern void addIcon(const Coord3D *pos, Real width, Int numFramesDuration, RGBColor color);
 		RGBColor color;
		color.blue = 0;
		color.red = color.green = 1;
		Coord3D pos;
		addIcon(nullptr, 0, 0, color); // erase feedback.
		for( PathNode *node = getFirstNode(); node; node = node->getNext() )
		{

			// create objects to show path - they decay

			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.25f, 200, color);
		}

		// show optimized path
		for( node = getFirstNode(); node; node = node->getNextOptimized() )
		{
			pos = *node->getPosition();
			addIcon(&pos, PATHFIND_CELL_SIZE_F*.8f, 200, color);
		}
		TheAI->pathfinder()->setDebugPath(this);
	}
#endif
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void Path::loadPostProcess()
{
}

/**
 * Create a new node at the head of the path
 */
void Path::prependNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	invalidateFollowing();
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	node->m_owner = this;
	m_path = node->prependToList( m_path );

	if (m_pathTail == nullptr)
		m_pathTail = node;

	m_isOptimized = false;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}

/**
 * Create a new node at the tail of the path
 */
void Path::appendNode( const Coord3D *pos, PathfindLayerEnum layer )
{
	if (m_isOptimized && m_pathTail)
	{
		/* Check for duplicates. */
		if (pos->x == m_pathTail->getPosition()->x && pos->y == m_pathTail->getPosition()->y) {
			engine::debug::log_info("Warning - Path Seg length == 0, ignoring. john a.");
			return;
		}
	}
	invalidateFollowing();
	PathNode *node = newInstance(PathNode);

	node->setPosition( pos );
	node->setLayer(layer);

	node->m_owner = this;
	if (m_pathTail) m_pathTail->append(node);
	else m_path = node;

	if (m_isOptimized && m_pathTail)
	{
		m_pathTail->setNextOptimized(node);
	}

	m_pathTail = node;

#ifdef CPOP_STARTS_FROM_PREV_SEG
	m_cpopRecentStart = nullptr;
#endif
}
/**
 * Create a new node at the tail of the path
 */
void Path::updateLastNode( const Coord3D *pos )
{
	invalidateFollowing();
	PathfindLayerEnum layer = TheTerrainLogic->getLayerForDestination(pos);
	if (m_pathTail) {
		m_pathTail->setPosition(pos);
		m_pathTail->setLayer(layer);
	}
	if (m_isOptimized && m_pathTail)
	{
		PathNode *node = m_path;
		while(node && node->getNextOptimized() != m_pathTail) {
			node = node->getNextOptimized();
		}
		if (node && node->getNextOptimized() == m_pathTail) {
			node->setNextOptimized(m_pathTail);
		}
	}
}

/**
 * Optimize the path by checking line of sight
 */
namespace {
template<class Visible>
void smoothNativeRoute(Path& path,bool group,Visible visible) {
    std::vector<PathNode*> nodes;
    std::vector<float> x,y;
    std::vector<unsigned> layers;
    std::vector<std::uint8_t> optimize;
    for (auto* node=path.getFirstNode();node;node=node->getNext()) {
        nodes.push_back(node);
        x.push_back(node->getPosition()->x);y.push_back(node->getPosition()->y);
        layers.push_back(unsigned(node->getLayer()));
        optimize.push_back(node->getCanOptimize()!=false);
    }
    const auto selected=navigation::smoothRoute({x,y,layers,optimize},group,
        [&](unsigned first,unsigned last,unsigned layer) {
            return visible(*nodes[first]->getPosition(),*nodes[last]->getPosition(),PathfindLayerEnum(layer));
        });
    for (std::size_t i=1;i<selected.size();++i)
        nodes[selected[i-1]]->setNextOptimized(nodes[selected[i]]);
    if (!selected.empty()) nodes[selected.back()]->setNextOptimized(nullptr);
    path.markOptimized();
}
}
void Path::optimize( const Object* object, LocomotorSurfaceTypeMask surfaces, Bool blocked )
{
    auto* pathfinder=TheAI->pathfinder();
    smoothNativeRoute(*this,false,[&](const Coord3D& from,const Coord3D& to,PathfindLayerEnum layer) {
        if (pathfinder->isLinePassable(object,surfaces,layer,from,to,blocked,false)) return true;
        const auto* cell=pathfinder->getCell(layer,&to);
        return cell && cell->getType()==PathfindCell::CELL_CLIFF && !cell->getPinched();
    });
}

/**
 * Optimize the path by checking line of sight
 */
void Path::optimizeGroundPath( Bool crusher, Int diameter )
{
    smoothNativeRoute(*this,true,[&](const Coord3D& from,const Coord3D& to,PathfindLayerEnum layer) {
        return TheAI->pathfinder()->isGroundPathPassable(crusher,from,layer,to,diameter)!=false;
    });
}

inline Bool isReallyClose(const Coord3D& a, const Coord3D& b)
{
	const Real CLOSE_ENOUGH = 0.1f;
	return
		fabs(a.x-b.x) <= CLOSE_ENOUGH &&
		fabs(a.y-b.y) <= CLOSE_ENOUGH &&
		fabs(a.z-b.z) <= CLOSE_ENOUGH;
}

/**
 * Given a location, return the closest position on the path.
 * If 'allowBacktrack' is true, the entire path is considered.
 * If it is false, the point computed cannot be prior to previously returned non-backtracking points on this path.
 * Because the path "knows" the direction of travel, it will "lead" the given position a bit
 * to ensure the path is followed in the intended direction.
 *
 * Note: The path cleanup does not take into account rolling terrain, so we can end up with
 * these situations:
 *
 *           B
 *        ######
 *      ##########
 *  A-##----------##---C
 * #######################
 *
 *
 * When an agent gets to B, he seems far off of the path, but it really not.
 * There are similar problems with valleys.
 *
 * Since agents track the closest path, if a high hill gets close to the underside of
 * a bridge, an agent may 'jump' to the higher path.  This must be avoided in maps.
 *
 * return along-path distance to the end will be returned as function result
 */
void Path::computePointOnPath(
	const Object* obj,
	const LocomotorSet& locomotorSet,
	const Coord3D& pos,
	ClosestPointOnPathInfo& out
)
{
	out.layer = LAYER_GROUND;
	out.posOnPath.zero();
	out.distAlongPath = 0;

	if (m_path == nullptr)
	{
		invalidateFollowing();
		return;
	}
	if (m_cpopValid && m_cpopCountdown>0 && isReallyClose(pos, m_cpopIn))
	{
		out = m_cpopOut;
		m_cpopCountdown--;
		return;
	}
	m_cpopCountdown = MAX_CPOP;

	if (!m_followingGeometryValid) {
		if (!m_followingGeometry) m_followingGeometry = std::make_unique<FollowingGeometry>();
		auto& route = m_followingGeometry->route;
		route.clear();
		auto& edges=m_followingGeometry->edges;
		edges.clear();
		m_followingGeometry->firstLayer=m_path->getLayer();
		m_followingGeometry->lastPoint=*getLastNode()->getPosition();
		for (const PathNode* node = m_path; node;) {
			Coord2D direction;
			Real length;
			const auto* next = node->getNextOptimized(&direction, &length);
			if (!next) break;
			const auto& a = *node->getPosition();
			const auto& b = *next->getPosition();
			const auto* after=next->getNextOptimized();
			const auto c=after?*after->getPosition():Coord3D{};
			const auto* previous=node->getPrevious();
			edges.push_back({{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},
				direction.x,direction.y,length,int(node->getLayer()),int(next->getLayer()),
				after?int(after->getLayer()):0,previous && previous->getLayer()>LAYER_GROUND,
				next->getCanOptimize()!=false,after!=nullptr});
			route.append({a.x,a.y,a.z}, {b.x,b.y,b.z}, direction.x,direction.y,length,unsigned(edges.size()));
			node = next;
		}
		m_followingGeometryValid = true;
	}
	out.layer=m_followingGeometry->firstLayer;
	out.posOnPath=m_followingGeometry->lastPoint;
	const auto& route = m_followingGeometry->route;
	const Real totalPathLength=route.length();
	Real lengthAlongPathToPos=0;
	const auto closest=route.closest({pos.x,pos.y,pos.z});
	lengthAlongPathToPos=closest.along;
	if (closest.handle) {
		const auto target=navigation::following::selectRouteTarget(
			m_followingGeometry->edges[closest.handle-1],{pos.x,pos.y,pos.z},
			int(obj->getLayer()),int(out.layer),PATHFIND_CELL_SIZE_F,[&](auto point,int layer) {
				const Coord3D candidate{point.x,point.y,point.z};
				return TheAI->pathfinder()->isLinePassable(obj,locomotorSet.getValidSurfaces(),
					static_cast<PathfindLayerEnum>(layer),pos,candidate,false,true);
			},int(LAYER_GROUND));
		out.posOnPath={target.point.x,target.point.y,target.point.z};
		out.layer=static_cast<PathfindLayerEnum>(target.layer);
	}

	TheAI->pathfinder()->setDebugPathPosition( &out.posOnPath );

	out.distAlongPath = totalPathLength - lengthAlongPathToPos;

	Coord3D delta;
	delta.x = out.posOnPath.x - pos.x;
	delta.y = out.posOnPath.y - pos.y;
	delta.z = 0;
	Real lenDelta = delta.length();
	if (lenDelta > out.distAlongPath && out.distAlongPath > PATHFIND_CLOSE_ENOUGH)
	{
		out.distAlongPath = lenDelta;
	}

	m_cpopIn = pos;
	m_cpopOut = out;
	m_cpopValid = true;
}


/**
	Given a position, computes the distance to the goal.  Returns 0 if we are past the goal.
	Returns the goal position in goalPos.  This is intended for use with flying paths, that go
	directly to the goal and don't consider obstacles.  jba.
 */
Real Path::computeFlightDistToGoal( const Coord3D *pos, Coord3D& goalPos )
{
	if (m_path == nullptr)
	{
		goalPos.x = 0.0f;
		goalPos.y = 0.0f;
		goalPos.z = 0.0f;
		return 0.0f;
	}
	const PathNode *curNode = getFirstNode();
	if (m_cpopRecentStart) {
		curNode = m_cpopRecentStart;
	} else {
		m_cpopRecentStart = curNode;
	}
	const PathNode *nextNode = curNode->getNextOptimized();
	goalPos = *curNode->getPosition();
	Real distance = 0;
	Bool useNext = true;
	while (nextNode) {

		if (useNext) {
			goalPos = *nextNode->getPosition();
		}

		Coord3D startPos = *curNode->getPosition();
		Coord3D endPos = *nextNode->getPosition();

		Coord2D posToGoalVector;
		// posToGoalVector is pos to goalPos vector.
		posToGoalVector.x = endPos.x - pos->x;
		posToGoalVector.y = endPos.y - pos->y;

		// pathVector is the startPos to goal pos vector.
		Coord2D pathVector;
		pathVector.x = endPos.x - startPos.x;
		pathVector.y = endPos.y - startPos.y;

		// Normalize pathVector
		pathVector.normalize();

		// Dot product is the posToGoal vector projected onto the path vector.
		Real dotProduct = posToGoalVector.x*pathVector.x	+ posToGoalVector.y*pathVector.y;
		if (dotProduct>=0) {
			distance += dotProduct;
			useNext = false;
		}	else if (useNext) {
			m_cpopRecentStart = nextNode;
		}
		curNode = nextNode;
		nextNode = curNode->getNextOptimized();
	}
	return distance;

}
//-----------------------------------------------------------------------------------



}
