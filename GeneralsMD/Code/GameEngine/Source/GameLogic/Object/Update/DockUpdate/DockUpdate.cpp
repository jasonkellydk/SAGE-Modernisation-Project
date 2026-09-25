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

// FILE: DockUpdate.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   Behavior common to all DockUpdates is here.  Everything but action()
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
import engine.debug;	// This must go first in EVERY cpp file in the GameEngine
import Engine.Core.Math.Vector3;
#include <algorithm>

#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/DockUpdate.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
DockUpdateModuleData::DockUpdateModuleData()
{
	m_numberApproachPositionsData = 0;
	m_isAllowPassthrough = TRUE;
}

/*static*/ void DockUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	UpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "NumberApproachPositions"	,INI::parseInt,		nullptr, offsetof( DockUpdateModuleData, m_numberApproachPositionsData ) },
		{ "AllowsPassthrough"				,INI::parseBool,	nullptr, offsetof( DockUpdateModuleData, m_isAllowPassthrough ) },
		{ nullptr, nullptr, nullptr, 0 }

	};

  p.add(dataFieldParse);

}

DockUpdate::DockUpdate( Thing *thing, const ModuleData* moduleData ) : UpdateModule( thing, moduleData )
{

	m_dockOpen = TRUE;

	m_positionsLoaded = FALSE;
	m_numberApproachPositionBones = -1;
	m_activeDocker = INVALID_ID;
	m_dockerInside = FALSE;
	m_dockCrippled = FALSE;

	const DockUpdateModuleData *md = (const DockUpdateModuleData *)moduleData;

	m_exitPosition.zero();
	m_dockPosition.zero();
	m_enterPosition.zero();

	m_numberApproachPositions = md->m_numberApproachPositionsData;
	if( m_numberApproachPositions != DYNAMIC_APPROACH_VECTOR_FLAG )
	{
		// Not dynamic, so make this the size
		m_approachPositions.resize(m_numberApproachPositions);
		m_approachPositionOwners.resize(m_numberApproachPositions);
		m_approachPositionReached.resize(m_numberApproachPositions);
	}
	else
	{
		//Otherwise, make a default size, and plan on growing it later
		m_approachPositions.resize(DEFAULT_APPROACH_VECTOR_SIZE);
		m_approachPositionOwners.resize(DEFAULT_APPROACH_VECTOR_SIZE);
		m_approachPositionReached.resize(DEFAULT_APPROACH_VECTOR_SIZE);
	}

	for( size_t vectorIndex = 0; vectorIndex < m_approachPositions.size(); ++vectorIndex )
	{
		// Whatever size we are, init everything.
		m_approachPositions[vectorIndex].zero();
		m_approachPositionOwners[vectorIndex] = INVALID_ID;
		m_approachPositionReached[vectorIndex] = FALSE;
	}
}

DockUpdate::~DockUpdate()
{
}

Bool DockUpdate::isClearToApproach( Object const* docker ) const
{
	// If we allow infinite approaches, we don't even need to look up.  Just say yes.
	// The reserve code will handle appending a free spot to the end.
	if( m_numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG )
		return TRUE;

	ObjectID dockerID = docker->getID();

	for( size_t positionIndex = 0; positionIndex < m_approachPositionOwners.size(); ++positionIndex )
	{
		if( m_approachPositionOwners[positionIndex] == INVALID_ID )
		{
			return TRUE;
		}
		if( m_approachPositionOwners[positionIndex] == dockerID )
		{
			return TRUE;
		}
	}

	return FALSE;
}

Bool DockUpdate::reserveApproachPosition( Object* docker, Coord3D *position, Int *index )
{

	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return FALSE;

	ObjectID dockerID = docker->getID();

	Int positionIndex = 0;
	for( ; positionIndex < m_approachPositionOwners.size(); ++positionIndex )
	{
		if( m_approachPositionOwners[positionIndex] == dockerID )
		{
			*position = computeApproachPosition( positionIndex, docker );
			*index = positionIndex;
			return TRUE;
		}
		if( m_approachPositionOwners[positionIndex] == INVALID_ID )
		{
			m_approachPositionOwners[positionIndex] = dockerID;
			*position = computeApproachPosition( positionIndex, docker );
			*index = positionIndex;
			return TRUE;
		}
	}

	// If I make it out of the loop, I am full, so dynamic approach buildings should make a new entry instead of saying no
	if( m_numberApproachPositions == DYNAMIC_APPROACH_VECTOR_FLAG )
	{
		Coord3D zero;
		zero.zero();
		m_approachPositions.push_back( zero );
		m_approachPositionOwners.push_back( INVALID_ID );
		m_approachPositionReached.push_back( FALSE );

		loadDockPositions();// refresh this new one

		positionIndex = m_approachPositionOwners.size() - 1;// The new last spot
		m_approachPositionOwners[positionIndex] = dockerID;
		*position = computeApproachPosition( positionIndex, docker );
		*index = positionIndex;
		return TRUE;
	}

	return FALSE;
}

Bool DockUpdate::advanceApproachPosition( Object* docker, Coord3D *position, Int *index )
{
	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return FALSE;
	if( *index <= 0 )
		return FALSE;
	if( m_approachPositionOwners[(*index) - 1] != INVALID_ID )
		return FALSE;

	Int hisIndex = *index;
	Int previousIndex = hisIndex - 1;

	m_approachPositionOwners[previousIndex] = docker->getID();
	m_approachPositionReached[previousIndex] = FALSE;

	m_approachPositionOwners[hisIndex] = INVALID_ID;
	m_approachPositionReached[hisIndex] = FALSE;

	*position = computeApproachPosition( previousIndex, docker );
	*index = previousIndex;
	return TRUE;
}

Bool DockUpdate::isClearToEnter( Object const* docker ) const
{
	ObjectID dockerID = docker->getID();
	return dockerID == m_activeDocker;
}

Bool DockUpdate::isClearToAdvance( Object const* docker, Int dockerIndex ) const
{
	if( dockerIndex < 0 )
		return FALSE;

	ObjectID dockerID = docker->getID();
	Bool correctRequest = dockerID == m_approachPositionOwners[dockerIndex];
	Bool approachReached = m_approachPositionReached[dockerIndex];
	Bool nextSpotFree = (dockerIndex > 0)  &&  (m_approachPositionOwners[dockerIndex - 1] == INVALID_ID);

	return correctRequest && approachReached && nextSpotFree;
}

void DockUpdate::getEnterPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are, unless you fly, in which case I should recenter you
	Coord3D zero;
	zero.zero();
	if( m_enterPosition == zero )
	{
		if( docker->isUsingAirborneLocomotor() )
		{
			*position = *getObject()->getPosition();
			return;
		}
		*position = *docker->getPosition();
		// A boneless supply dock has no entry waypoint to bring a waiting
		// truck back after another vehicle has displaced it. Recover before
		// processing supplies; action() must retain its normal distance check.
		if (getObject()->isKindOf(KINDOF_SUPPLY_SOURCE)) {
			const Real radius = docker->getGeometryInfo().getBoundingCircleRadius();
			if (ThePartitionManager->getDistanceSquared(docker, getObject(), FROM_BOUNDINGSPHERE_2D) > sqr(2 * radius)) {
				const Coord3D& center = *getObject()->getPosition();
				const Real dx = position->x - center.x, dy = position->y - center.y;
				const Real distance = sqrtf(dx * dx + dy * dy);
				const Real targetDistance = getObject()->getGeometryInfo().getBoundingCircleRadius() + radius;
				if (distance > targetDistance && distance > 0) {
					position->x = center.x + dx * (targetDistance / distance);
					position->y = center.y + dy * (targetDistance / distance);
					position->z = TheTerrainLogic->getLayerHeight(position->x, position->y, getObject()->getLayer());
				}
			}
		}
		return;
	}

	// take local space position and convert to world space
	getObject()->transformBoneToWorld( &m_enterPosition, nullptr, position, nullptr );

}

void DockUpdate::getDockPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are.
	Coord3D zero;
	zero.zero();
	if( m_enterPosition == zero )
	{
		*position = *docker->getPosition();
		return;
	}

	// take local space position and convert to world space
	getObject()->transformBoneToWorld( &m_dockPosition, nullptr, position, nullptr );

}

void DockUpdate::getExitPosition( Object* docker, Coord3D *position )
{

	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	// sanity
	if( position == nullptr )
		return;

	// If I don't have a bone, you are fine where you are.
	Coord3D zero;
	zero.zero();
	if( m_enterPosition == zero )
	{
		*position = *docker->getPosition();
		return;
	}

	// take local space position and convert to world space
	getObject()->transformBoneToWorld( &m_exitPosition, nullptr, position, nullptr );

}

void DockUpdate::onApproachReached( Object* docker )
{
	ObjectID dockerID = docker->getID();
	for( size_t positionIndex = 0; positionIndex < m_approachPositionOwners.size(); ++positionIndex )
	{
		if( m_approachPositionOwners[positionIndex] == dockerID )
		{
			m_approachPositionReached[positionIndex] = TRUE;
			if (dockerID != m_activeDocker &&
				std::find(m_readyDockers.begin(), m_readyDockers.end(), dockerID) == m_readyDockers.end())
				m_readyDockers.push_back(dockerID);
			return;
		}
	}
}

void DockUpdate::onEnterReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING),
																			MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING) );
	docker->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING),
																					MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING) );
	m_dockerInside = TRUE;

	ObjectID dockerID = docker->getID();
	for( size_t positionIndex = 0; positionIndex < m_approachPositionOwners.size(); ++positionIndex )
	{
		if( m_approachPositionOwners[positionIndex] == dockerID )
		{
			m_approachPositionOwners[positionIndex] = INVALID_ID;
			m_approachPositionReached[positionIndex] = FALSE;
			return;
		}
	}
}

void DockUpdate::onDockReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionState( MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING_ACTIVE );
	docker->clearAndSetModelConditionState( MODELCONDITION_DOCKING_BEGINNING, MODELCONDITION_DOCKING_ACTIVE );
}

void DockUpdate::onExitReached( Object* docker )
{
	Object *me = getObject();
	me->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_ACTIVE, MODELCONDITION_DOCKING),
																			MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING) );
	docker->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2(MODELCONDITION_DOCKING_ACTIVE, MODELCONDITION_DOCKING),
																					MAKE_MODELCONDITION_MASK(MODELCONDITION_DOCKING_ENDING) );
	m_dockerInside = FALSE;

	ObjectID dockerID = docker->getID();
	if( dockerID == m_activeDocker )
		m_activeDocker = INVALID_ID;
	else
	{

		//
		// we only assert here if the dock is open, for closed docks it's OK to allow somebody
		// to continue moving to the exit position cause they are leaving after all
		//
		if( isDockOpen() )
			engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "Fiddle.  Someone said goodbye to a dock when the dock didn't think it was talking to that someone.");

	}
}

void DockUpdate::cancelDock( Object* docker )
{
	ObjectID dockerID = docker->getID();
	m_readyDockers.erase(std::remove(m_readyDockers.begin(), m_readyDockers.end(), dockerID), m_readyDockers.end());
	for( size_t positionIndex = 0; positionIndex < m_approachPositionOwners.size(); ++positionIndex )
	{
		if( m_approachPositionOwners[positionIndex] == dockerID )
		{
			m_approachPositionOwners[positionIndex] = INVALID_ID;
			m_approachPositionReached[positionIndex] = FALSE;
		}
	}
	if( m_activeDocker == dockerID )
	{
		Object *dockingObject = TheGameLogic->findObjectByID(m_activeDocker);
		m_activeDocker = INVALID_ID;
		m_dockerInside = FALSE;
		// clear any model conditions related to docking that may be set on us and them.
		// (Normal clear is part of each stage, but we won't get there.)
		ModelConditionFlags clear;
		clear.set( MODELCONDITION_DOCKING_ENDING );
		clear.set( MODELCONDITION_DOCKING_BEGINNING );
		clear.set( MODELCONDITION_DOCKING_ACTIVE );
		clear.set( MODELCONDITION_DOCKING );
		getObject()->clearModelConditionFlags( clear );
		if( dockingObject )
			dockingObject->clearModelConditionFlags( clear );
	}
}

void DockUpdate::setDockCrippled( Bool setting )
{
	// At this level, Crippling means I will accept Approach requests, but I will never grant Enter clearance.
	m_dockCrippled = setting;
}

UpdateSleepTime DockUpdate::update()
{
	if( m_activeDocker == INVALID_ID  &&  !m_dockCrippled )
	{
		// if setDockCrippled has been called, I will never give entrance permission.
		// Physical approach slots are reusable. Choosing the lowest slot lets a
		// returning truck repeatedly overtake an earlier arrival in a higher slot.
		for (auto ready = m_readyDockers.begin(); ready != m_readyDockers.end(); ++ready)
		{
			const auto owner = std::find(m_approachPositionOwners.begin(), m_approachPositionOwners.end(), *ready);
			if (owner != m_approachPositionOwners.end() &&
				m_approachPositionReached[owner - m_approachPositionOwners.begin()])
			{
				m_activeDocker = *ready;
				m_readyDockers.erase(ready);
				return UPDATE_SLEEP_NONE;
			}
		}
	}
	else if ( getObject()->isKindOf( KINDOF_SUPPLY_SOURCE ) )
	{
		Object *docker = TheGameLogic->findObjectByID( m_activeDocker );
		if ( docker && docker->isKindOf( KINDOF_DOZER ) && docker->isKindOf( KINDOF_HARVESTER ))// a worker
		{
			ModelConditionFlags test;
			test.set( MODELCONDITION_DOCKING_BEGINNING );
			Drawable *dockerDraw = docker->getDrawable();
			if ( dockerDraw && dockerDraw->getModelConditionFlags().anyIntersectionWith( test ) )
				dockerDraw->clearModelConditionFlags( MAKE_MODELCONDITION_MASK(MODELCONDITION_MOVING) );
		}
	}


	return UPDATE_SLEEP_NONE;
}

Coord3D DockUpdate::computeApproachPosition( Int positionIndex, Object *forWhom )
{
	// load dock positions if not loaded yet
	if( m_positionsLoaded == FALSE )
		loadDockPositions();

	Coord3D bestPosition;// This answer is the best, as it includes findPositionAround
	Coord3D workingPosition;// But if findPositionAround fails, we need to say something.

	FindPositionOptions fpOptions;
	// Start with the pristine bone, then convert it to the world, then find a clean spot around it.

	Object *us = getObject();
	us->transformBoneToWorld( &m_approachPositions[positionIndex], nullptr, &workingPosition, nullptr );

	if( m_numberApproachPositionBones == 0 )
	{
		Coord3D ourPosition = *us->getPosition();
		Coord3D theirPosition = *forWhom->getPosition();
		// A Boneless building wants to bias towards the caller for the arbitrary position
		Engine::Math::Vector3 offset{
			theirPosition.x - ourPosition.x, theirPosition.y - ourPosition.y, theirPosition.z - ourPosition.z};
		offset = offset.Normalized_Legacy();
		offset = offset * (us->getGeometryInfo().getMajorRadius() / 2);

		workingPosition.x += offset.x;
		workingPosition.y += offset.y;
		workingPosition.z += offset.z;
	}

	fpOptions.minRadius = 0.0f;
	fpOptions.maxRadius = 100.0f;
	fpOptions.sourceToPathToDest = forWhom;// This makes it find a place forWhom can get to.
	if( forWhom->isUsingAirborneLocomotor() )
		fpOptions.ignoreObject = getObject();// Flyers can ignore us, so they can approach right over us if they want.

	Bool spotFound = ThePartitionManager->findPositionAround( &workingPosition, &fpOptions, &bestPosition );

	if( spotFound)
		return bestPosition;

	return workingPosition;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void DockUpdate::loadDockPositions()
{
	Object *obj = getObject();
	Drawable *myDrawable = obj->getDrawable();

	if (myDrawable)
	{
		//Patch 1.03 - Kris - Jan 19, 2005
		//Some of the GLASupplyStash assets still have docking bones in them. When found, the docking positions must be
		//observed. This occurs when upgrading to fortified structures which still have the bones. This has the negative
		//side-effect of workers suddenly slowing down their gathering rate. The proper fix would be to remove the bones
		//from the assets, but the artists could not find the original max files, hence the code solution.
		if( !obj->isKindOf( KINDOF_IGNORE_DOCKING_BONES ) )
		{

			myDrawable->getPristineBonePositions( "DockStart", 0, &m_enterPosition, 1);
			myDrawable->getPristineBonePositions( "DockAction", 0, &m_dockPosition, 1);
			myDrawable->getPristineBonePositions( "DockEnd", 0, &m_exitPosition, 1);
			if( m_numberApproachPositions != DYNAMIC_APPROACH_VECTOR_FLAG )
			{
				// Dynamic means no bones

				// TheSuperHackers @logic-client-separation helmutbuhler 11/04/2025
				// We shouldn't depend on bones of a drawable here!

				// TheSuperHackers @fix helmutbuhler 19/04/2025 Zero initialize array to prevent uninitialized memory reads.
				// Important: the entire target vector is used for serialization and crc and must not contain random data.
				Coord3D approachBones[DEFAULT_APPROACH_VECTOR_SIZE] = {0};
				m_numberApproachPositionBones = myDrawable->getPristineBonePositions( "DockWaiting", 1, approachBones, m_numberApproachPositions);
				if( m_numberApproachPositions == m_approachPositions.size() )//safeguard: will always be true
				{
					for( Int copyIndex = 0; copyIndex < m_numberApproachPositions; ++copyIndex )
					{
						m_approachPositions[copyIndex] = approachBones[copyIndex];
					}
				}
			}
			else
				m_numberApproachPositionBones = 0;

			m_positionsLoaded = TRUE;
		}
		else
		{
			m_numberApproachPositionBones = 0;
			m_positionsLoaded = TRUE;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool DockUpdate::isAllowPassthroughType()
{
	return getDockUpdateModuleData()->m_isAllowPassthrough;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DockUpdate::crc( Xfer *xfer )
{

	// extend base class
	UpdateModule::crc( xfer );
	UnsignedInt readyCount = static_cast<UnsignedInt>(m_readyDockers.size());
	xfer->xferUnsignedInt(&readyCount);
	for (auto id : m_readyDockers) xfer->xferObjectID(&id);

}

// ------------------------------------------------------------------------------------------------
/** Xfer Method */
// ------------------------------------------------------------------------------------------------
void DockUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// call base class
	UpdateModule::xfer( xfer );

	// enter position
	xfer->xferCoord3D( &m_enterPosition );

	// dock position
	xfer->xferCoord3D( &m_dockPosition );

	// exit position
	xfer->xferCoord3D( &m_exitPosition );

	// # approach positions
	xfer->xferInt( &m_numberApproachPositions );

	// positions loaded
	xfer->xferBool( &m_positionsLoaded );

	// approach positions
	Int vectorSize = m_approachPositions.size();
	xfer->xferInt( &vectorSize );
	m_approachPositions.resize(vectorSize);
	Int vectorIndex = 0;
	for( ; vectorIndex < vectorSize; ++vectorIndex )
	{
		// Okay, this is cool.  On save, the size and a bunch of coords will be written.
		// on load, vectorSize will be at 0 from the .size, but will then get set
		// by the xfer, and properly control the number of Coords.
		xfer->xferCoord3D( &m_approachPositions[vectorIndex] );
	}

	// approach position owners
	vectorSize = m_approachPositionOwners.size();
	xfer->xferInt( &vectorSize );
	m_approachPositionOwners.resize(vectorSize);
	for( vectorIndex = 0; vectorIndex < vectorSize; ++vectorIndex )
	{
		xfer->xferObjectID( &m_approachPositionOwners[vectorIndex] );
	}

	// approach positions reached
	vectorSize = m_approachPositionReached.size();
	xfer->xferInt( &vectorSize );
	m_approachPositionReached.resize(vectorSize);
	for( vectorIndex = 0; vectorIndex < vectorSize; ++vectorIndex )
	{
		// Vector of Bool gets packed as bitfield internally
		Bool unpack = m_approachPositionReached[vectorIndex];
		xfer->xferBool( &unpack );
		m_approachPositionReached[vectorIndex] = unpack;
	}

	// active docker
	xfer->xferObjectID( &m_activeDocker );

	// docker inside
	xfer->xferBool( &m_dockerInside );

	// docker crippled
	xfer->xferBool( &m_dockCrippled );

	// dock open
	xfer->xferBool( &m_dockOpen );
	if (version >= 2) {
		Int readyCount = static_cast<Int>(m_readyDockers.size());
		xfer->xferInt(&readyCount);
		m_readyDockers.resize(readyCount);
		for (auto& id : m_readyDockers) xfer->xferObjectID(&id);
	} else {
		m_readyDockers.clear();
		for (size_t i = 0; i < m_approachPositionOwners.size(); ++i)
			if (m_approachPositionReached[i] && m_approachPositionOwners[i] != INVALID_ID &&
				m_approachPositionOwners[i] != m_activeDocker)
				m_readyDockers.push_back(m_approachPositionOwners[i]);
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DockUpdate::loadPostProcess()
{

	// call base class
	UpdateModule::loadPostProcess();

}
