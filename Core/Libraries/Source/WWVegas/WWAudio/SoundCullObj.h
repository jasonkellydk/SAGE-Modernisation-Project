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
 *                 Project Name : WWAudio                                                      *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/WWAudio/SoundCullObj.h        $*
 *                                                                                             *
 *                       Author:: Patrick Smith                                                *
 *                                                                                             *
 *                     $Modtime:: 6/26/01 5:36p                                               $*
 *                                                                                             *
 *                    $Revision:: 8                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "SoundSceneObj.h"
#include "WWLib/mempool.h"
#include "WWLib/multilist.h"
#include "WWLib/refcount.h"

import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.SpatialGrid3;
import Engine.Core.Math.Vector3;

class SoundCullObjClass;

//////////////////////////////////////////////////////////////////////////////////
//	The spatial index a SoundCullObjClass is bucketed in (replaces the legacy
// grid / AAB-tree culling systems).
//////////////////////////////////////////////////////////////////////////////////
using SoundSpatialIndex = Engine::Math::SpatialGrid3<SoundCullObjClass>;


/////////////////////////////////////////////////////////////////////////////
//
//	SoundCullObjClass
//
//	Simple 'sound physics' object that wraps a SoundClass object and is derived
// from PhysClass so it can be used with the different culling systems.
//
class SoundCullObjClass : public MultiListObjectClass, public RefCountClass
{
	public:

		//////////////////////////////////////////////////////////////////////
		//	Public constructors/destructors
		//////////////////////////////////////////////////////////////////////
		SoundCullObjClass ()
			: m_SoundObj (nullptr),
			  m_Transform (Engine::Math::AffineTransform3::Identity()),
			  m_CullingSystem (nullptr),
			  m_CullCenter (),
			  m_CullExtent () {}

		virtual ~SoundCullObjClass () override { REF_PTR_RELEASE (m_SoundObj); }

		//////////////////////////////////////////////////////////////////////
		//	Get the 'bounds' of this sound
		//////////////////////////////////////////////////////////////////////
		Engine::Math::AxisAlignedBox3 Get_Spatial_Bounds () const;

		//////////////////////////////////////////////////////////////////////
		//	Culling system linkage (mirrors the legacy CullableClass behaviour:
		// the cull box is cached and the owning culling system is re-bucketed
		// whenever the box is refreshed).
		//////////////////////////////////////////////////////////////////////
		void Set_Culling_System (SoundSpatialIndex *system) { m_CullingSystem = system; }
		SoundSpatialIndex *Get_Culling_System () const { return m_CullingSystem; }

		// Refresh the cached cull box from the current bounds and update the
		// owning culling system (legacy Set_Cull_Box (Get_Bounding_Box ())).
		void Update_Cull_Box ();

		// Legacy AABoxClass::Contains (point) against the cached cull box.
		bool Cull_Box_Contains (Engine::Math::Vector3 point) const;

		//////////////////////////////////////////////////////////////////////
		//	Access to the Position/Orientation state of the object
		//////////////////////////////////////////////////////////////////////
		virtual Engine::Math::AffineTransform3 Get_Transform () const;
		virtual void Set_Transform (const Engine::Math::AffineTransform3 &transform);

		//////////////////////////////////////////////////////////////////////
		//	Timestep methods
		//////////////////////////////////////////////////////////////////////
		virtual void					Timestep (float dt) {}

		//////////////////////////////////////////////////////////////////////
		//	Sound object wrapping
		//////////////////////////////////////////////////////////////////////
		virtual void Set_Sound_Obj (SoundSceneObjClass *sound_obj);
		virtual SoundSceneObjClass *	Peek_Sound_Obj () const					{ return m_SoundObj; }

	protected:

		//////////////////////////////////////////////////////////////////////
		//	Protected methods
		//////////////////////////////////////////////////////////////////////

	private:

		//////////////////////////////////////////////////////////////////////
		//	Private member data
		//////////////////////////////////////////////////////////////////////
		SoundSceneObjClass *		m_SoundObj;
		mutable Engine::Math::AffineTransform3 m_Transform;
		SoundSpatialIndex *		m_CullingSystem;
		Engine::Math::Vector3	m_CullCenter;
		Engine::Math::Vector3	m_CullExtent;
};
