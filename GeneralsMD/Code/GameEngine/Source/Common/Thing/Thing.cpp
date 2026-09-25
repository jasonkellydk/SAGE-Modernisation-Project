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

// FILE: Thing.cpp ////////////////////////////////////////////////////////////
// Created:   Colin Day, May 2001
//
// Desc:      Things are the base class for objects and drawables, objects
//						are logic side representations while drawables are client
//						side.  Common data will be held in the Thing defined here
//						and systems that need to work with both of them will work with
//						"Things"
//
//-----------------------------------------------------------------------------
#include "PreRTS.h"
#include <cstddef>
#include <cmath>
import engine.profiling;
import engine.debug;	// This must go first in EVERY cpp file in the GameEngine
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Vector3;


#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Lib/trig.h"
#include "GameLogic/TerrainLogic.h"


static constexpr const Real InitialThingPosX = 0.0f;
static constexpr const Real InitialThingPosY = 0.0f;

//=============================================================================
/** Constructor */
//=============================================================================
Thing::Thing( const ThingTemplate *thingTemplate )
{
	// sanity
	if( thingTemplate == nullptr )
	{

		// cannot create thing without template
		engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "no template" );
		return;

	}

	m_template = thingTemplate;
#if defined(RTS_DEBUG)
	m_templateName = thingTemplate->getName();
#endif
	m_transform = Engine::Math::AffineTransform3::Identity();
	m_cachedPos.x = InitialThingPosX;
	m_cachedPos.y = InitialThingPosY;
	m_cachedPos.z = 0.0f;
	m_cachedAngle = 0.0f;
	m_cachedDirVector.zero();
	m_cachedAltitudeAboveTerrain = 0;
	m_cachedAltitudeAboveTerrainOrWater = 0;
	m_cacheFlags = 0;

}

//=============================================================================
/** Destructor */
//=============================================================================
Thing::~Thing()
{
}

////=============================================================================
const ThingTemplate *Thing::getTemplate() const
{
	return m_template;
}

//=============================================================================
Bool Thing::isPositioned() const
{
	return m_cachedPos.x != InitialThingPosX || m_cachedPos.y != InitialThingPosY;
}

//=============================================================================
const Coord3D* Thing::getUnitDirectionVector2D() const
{
	//engine::profiling::Scope profile_scope_106("ThingMatrixStuff")
	if (!(m_cacheFlags & VALID_DIRVECTOR))
	{
		Real angle = getOrientation();
		m_cachedDirVector.x = Cos( angle );
		m_cachedDirVector.y = Sin( angle );
		m_cachedDirVector.z = 0;
		m_cacheFlags |= VALID_DIRVECTOR;
	}

	return &m_cachedDirVector;
}

//=============================================================================
void Thing::getUnitDirectionVector2D(Coord3D& dir) const
{
	dir = *getUnitDirectionVector2D();
}

//=============================================================================
void Thing::getUnitDirectionVector3D(Coord3D& dir) const
{
	const Engine::Math::Vector3 direction = m_transform.Basis_X().Normalized_Legacy();
	dir.x = direction.x;
	dir.y = direction.y;
	dir.z = direction.z;
}

//=============================================================================
// the nice thing about this is that we don't have to recalc out cached terrain stuff.
void Thing::setPositionZ( Real z )
{
	//engine::profiling::Scope profile_scope_139("ThingMatrixStuff")
	if( !m_template->isKindOf( KINDOF_STICK_TO_TERRAIN_SLOPE) )
	{
		Real oldAngle = m_cachedAngle;
		Coord3D oldPos = m_cachedPos;
		Engine::Math::Vector3 translation = m_transform.Translation();
		translation.z = z;
		m_transform.Set_Translation(translation);
		m_cachedPos.z = z;

		if (m_cacheFlags & VALID_ALTITUDE_TERRAIN)
		{
			m_cachedAltitudeAboveTerrain += (z - oldPos.z);
		}
		if (m_cacheFlags & VALID_ALTITUDE_SEALEVEL)
		{
			m_cachedAltitudeAboveTerrainOrWater += (z - oldPos.z);
		}

		reactToTransformChange(&oldPos, oldAngle);
	}
	else
	{
		Engine::Math::AffineTransform3 transform;
		const Bool stickToGround = true;	// yes, set the "z" pos
		Coord3D pos = m_cachedPos;
		pos.z = z;
		TheTerrainLogic->alignOnTerrain(getOrientation(), pos, stickToGround, transform);
		setWorldTransform(transform);
	}
	engine::debug::invariant((!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))), "!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))", __FILE__, __LINE__, "Drawable/Object position NAN! '%s'", m_template->getName().str() );
}

//=============================================================================
void Thing::setPosition( const Coord3D *pos )
{
	//engine::profiling::Scope profile_scope_175("ThingMatrixStuff")
	if( !m_template->isKindOf( KINDOF_STICK_TO_TERRAIN_SLOPE) )
	{
		Real oldAngle = m_cachedAngle;
		Coord3D oldPos = m_cachedPos;
		m_transform.Set_Translation({pos->x, pos->y, pos->z});
		m_cachedPos = *pos;
		m_cacheFlags &= ~(VALID_ALTITUDE_TERRAIN | VALID_ALTITUDE_SEALEVEL);	// but don't clear the dir flags.

		reactToTransformChange(&oldPos, oldAngle);
	}
	else
	{
		Engine::Math::AffineTransform3 transform;
		const Bool stickToGround = true;	// yes, set the "z" pos
		TheTerrainLogic->alignOnTerrain(getOrientation(), *pos, stickToGround, transform);
		setWorldTransform(transform);
	}
	engine::debug::invariant((!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))), "!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))", __FILE__, __LINE__, "Drawable/Object position NAN! '%s'", m_template->getName().str() );
}

//=============================================================================
void Thing::setOrientation( Real angle )
{
	//engine::profiling::Scope profile_scope_204("ThingMatrixStuff")
	Coord3D u, x, y, z, pos;

	// setOrientation always forces us straight up in the Z axis,
	// or aligned with the terrain if we have the magic flag set.
	// don't want this? call setWorldTransform instead.

	Real oldAngle = m_cachedAngle;
	Coord3D oldPos = m_cachedPos;
	const Engine::Math::Vector3 translation = m_transform.Translation();
	pos.x = translation.x;
	pos.y = translation.y;
	pos.z = translation.z;
	if( m_template->isKindOf( KINDOF_STICK_TO_TERRAIN_SLOPE) )
	{
		const Bool stickToGround = true;	// yes, set the "z" pos
		TheTerrainLogic->alignOnTerrain(angle, pos, stickToGround, m_transform);
	}
	else
	{
		z.x = 0.0f;
		z.y = 0.0f;
		z.z = 1.0f;

		u.x = Cos(angle);
		u.y = Sin(angle);
		u.z = 0.0f;

		y.crossProduct( z, u, y );
		x.crossProduct( y, z, x );

		m_transform = Engine::Math::AffineTransform3::From_Basis(
			{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}, {pos.x, pos.y, pos.z});
	}

	//engine::debug::invariant((-PI <= angle && angle <= PI), "-PI <= angle && angle <= PI", __FILE__, __LINE__, "Please pass only normalized (-PI..PI) angles to setOrientation (%f).", angle);
	m_cachedAngle = normalizeAngle(angle);
	m_cachedPos = pos;
	m_cacheFlags &= ~VALID_DIRVECTOR;	// but don't clear the altitude flags.

	reactToTransformChange(&oldPos, oldAngle);
	engine::debug::invariant((!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))), "!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))", __FILE__, __LINE__, "Drawable/Object position NAN! '%s'", m_template->getName().str() );
}

//=============================================================================
/** Set the world transformation matrix */
//=============================================================================
void Thing::setWorldTransform(const Engine::Math::AffineTransform3& transform)
{
	//engine::profiling::Scope profile_scope_256("ThingMatrixStuff")
	Real oldAngle = m_cachedAngle;
	Coord3D oldPos = m_cachedPos;
	m_transform = transform;
	const Engine::Math::Vector3 translation = m_transform.Translation();
	m_cachedPos.x = translation.x;
	m_cachedPos.y = translation.y;
	m_cachedPos.z = translation.z;
	m_cachedAngle = m_transform.Z_Rotation_Legacy();
	m_cacheFlags = 0;

	reactToTransformChange(&oldPos, oldAngle);
	engine::debug::invariant((!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))), "!(_isnan(getPosition()->x) || _isnan(getPosition()->y) || _isnan(getPosition()->z))", __FILE__, __LINE__, "Drawable/Object position NAN! '%s'", m_template->getName().str() );
}

Engine::Math::AffineTransform3 Thing::toWorldTransform(
	const Engine::Math::AffineTransform3& localTransform) const
{
	return Compose(m_transform, localTransform);
}

//-------------------------------------------------------------------------------------------------
Bool Thing::isKindOf(KindOfType t) const
{
	return getTemplate()->isKindOf(t);
}

//-------------------------------------------------------------------------------------------------
Bool Thing::isKindOfMulti(const KindOfMaskType& mustBeSet, const KindOfMaskType& mustBeClear) const
{
	return getTemplate()->isKindOfMulti(mustBeSet, mustBeClear);
}

// ------------------------------------------------------------------------------------------------
Bool Thing::isAnyKindOf( const KindOfMaskType& anyKindOf ) const
{
	return getTemplate()->isAnyKindOf( anyKindOf );
}

// ------------------------------------------------------------------------------------------------
Real Thing::calculateHeightAboveTerrain() const
{
	//engine::profiling::Scope profile_scope_293("ThingMatrixStuff")
	const Coord3D* pos = getPosition();
	Real terrainZ = TheTerrainLogic->getGroundHeight( pos->x, pos->y );
	Real myZ = pos->z;
	return myZ - terrainZ;
}

//-------------------------------------------------------------------------------------------------
Real Thing::getHeightAboveTerrain() const
{
	if (!(m_cacheFlags & VALID_ALTITUDE_TERRAIN))
	{
		m_cachedAltitudeAboveTerrain = calculateHeightAboveTerrain();
		m_cacheFlags |= VALID_ALTITUDE_TERRAIN;
	}
	return m_cachedAltitudeAboveTerrain;
}

//-------------------------------------------------------------------------------------------------
Real Thing::getHeightAboveTerrainOrWater() const
{
	//engine::profiling::Scope profile_scope_314("ThingMatrixStuff")
	if (!(m_cacheFlags & VALID_ALTITUDE_SEALEVEL))
	{
		const Coord3D* pos = getPosition();
		Real waterZ;
		if (TheTerrainLogic->isUnderwater(pos->x, pos->y, &waterZ))
		{
			m_cachedAltitudeAboveTerrainOrWater = pos->z - waterZ;
		}
		else
		{
			m_cachedAltitudeAboveTerrainOrWater = getHeightAboveTerrain();
		}
		m_cacheFlags |= VALID_ALTITUDE_SEALEVEL;
	}
	return m_cachedAltitudeAboveTerrainOrWater;
}

//=============================================================================
/** If we treat this as airborne, then they slide down slopes.  This checks whether
they are high enough that we should let them act like they're flying. jba. */
//=============================================================================
Bool Thing::isSignificantlyAboveTerrain() const
{
	// If it's high enough that it will take more than 3 frames to return to the ground,
	// then it's significantly airborne.  jba
	return (getHeightAboveTerrain() > -(3*3)*TheGlobalData->m_gravity);
}


//-------------------------------------------------------------------------------------------------
void Thing::transformBoneToWorld(const Coord3D* bonePosition,
	const Engine::Math::AffineTransform3* boneTransform, Coord3D* worldPosition,
	Engine::Math::AffineTransform3* worldTransform) const
{
	if (worldTransform)
		*worldTransform = boneTransform ? Compose(m_transform, *boneTransform) : m_transform;
	if (worldPosition && bonePosition)
	{
		const Engine::Math::Vector3 transformed = m_transform.Transform_Point(
			{bonePosition->x, bonePosition->y, bonePosition->z});
		worldPosition->x = transformed.x;
		worldPosition->y = transformed.y;
		worldPosition->z = transformed.z;
	}
}

// ------------------------------------------------------------------------------------------------
/** Push the 'in' parameter through our transformation matrix and store in 'out' */
// ------------------------------------------------------------------------------------------------
void Thing::transformPoint( const Coord3D *in, Coord3D *out )
{

	// sanity
	if( in == nullptr || out == nullptr )
		return;

	const Engine::Math::Vector3 transformed = m_transform.Transform_Point({in->x, in->y, in->z});
	out->x = transformed.x;
	out->y = transformed.y;
	out->z = transformed.z;

}
