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
 *                 Project Name : WWPhys                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/wwphys/camerashakesystem.cpp                 $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Greg_h                                                      $*
 *                                                                                             *
 *                     $Modtime:: 6/12/01 10:25a                                              $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdlib.h>
#include <cstdint>
#include "W3DDevice/GameClient/W3DAssetCatalog.h"
#include <W3DDevice/GameClient/W3DTextureHandle.h>
#include <W3DDevice/GameClient/W3DCastQuery.h>
#include "W3DDevice/GameClient/W3DCamera.h"
#include "Common/GlobalData.h"


#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/Water.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DBibBuffer.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "W3DDevice/GameClient/W3DRoadBuffer.h"
#include "W3DDevice/GameClient/W3DBridgeBuffer.h"
#include "W3DDevice/GameClient/W3DWaypointBuffer.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"

#include "W3DDevice/GameClient/CameraShakeSystem.h"
#include "W3DDevice/GameClient/W3DCamera.h"
import Engine.Core.Math.Scalar;
import Engine.Core.Math.RandomStream;
import engine.debug;

//#include "W3DDevice/GameClient/camera.h"
//#include "W3DDevice/GameClient/wwmemlog.h"

/*
** (gth) According to my "research" the artists say that there are several factors that
** go into a good camera shake.
** - The motion should be sinusoidal.
** - Camera rotation is more effective than camera motion (good, I won't use any translation)
** - The camera should pitch up and down a lot more than it yaws left and right.
*/


const float MIN_OMEGA			= ((12.5f*360.0f)*Engine::Math::Pi/180.0f);
const float MAX_OMEGA			= ((15.0f*360.0f)*Engine::Math::Pi/180.0f);
const float END_OMEGA			= ((360.0f)*Engine::Math::Pi/180.0f);
const float MIN_PHI				= ((0.0f)*Engine::Math::Pi/180.0f);
const float MAX_PHI				= ((360.0f)*Engine::Math::Pi/180.0f);
const Engine::Math::Vector3 AXIS_ROTATION{
	((7.5f)*Engine::Math::Pi/180.0f),
	((15.0f)*Engine::Math::Pi/180.0f),
	((5.0f)*Engine::Math::Pi/180.0f)};

// The legacy shakers all drew from the single global WWMath::Random_Float
// stream, so consecutive shakers were decorrelated. Keep one shared stream for
// every shaker (client-side visual only; never feeds GameLogic).
static Engine::Math::RandomStream &sharedShakeRandom()
{
	static Engine::Math::RandomStream random(0x43414D5348414B45ull);
	return random;
}


/************************************************************************************************
**
** CameraShakeSystemClass::CameraShakerClass Implementation
**
************************************************************************************************/
CameraShakeSystemClass::CameraShakerClass::CameraShakerClass
(
	const Engine::Math::Vector3 & position,
	float radius,
	float duration,
	float intensity
) :
	Position(position),
	Radius(radius),
	Duration(duration),
	Intensity(intensity),
	ElapsedTime(0.0f)
{
	/*
	** Initialize random sinusoid values
	*/
	Omega.x = sharedShakeRandom().NextFloat(MIN_OMEGA,MAX_OMEGA);
	Omega.y = sharedShakeRandom().NextFloat(MIN_OMEGA,MAX_OMEGA);
	Omega.z = sharedShakeRandom().NextFloat(MIN_OMEGA,MAX_OMEGA);
	Phi.x = sharedShakeRandom().NextFloat(MIN_PHI,MAX_PHI);
	Phi.y = sharedShakeRandom().NextFloat(MIN_PHI,MAX_PHI);
	Phi.z = sharedShakeRandom().NextFloat(MIN_PHI,MAX_PHI);
}

CameraShakeSystemClass::CameraShakerClass::~CameraShakerClass()
{
}


void CameraShakeSystemClass::CameraShakerClass::Compute_Rotations(
	const Engine::Math::Vector3 &camera_position, Engine::Math::Vector3 *set_angles)
{
	engine::debug::assert_condition((set_angles != nullptr), "set_angles != nullptr", __FILE__, __LINE__, "assertion failed");

	/*
	** We want several different sinusiods, each with a different phase shift and
	** frequency.  The frequency is a function of time as well, stretching the
	** sine wave out.  These waves are modulated based on the distance from the
	** center of the "shake", the intensity of the shake, and based on the axis
	** being affected.  The vertical axis should have about 3x the amplitude of
	** the horizontal axis.
	*/

	const Engine::Math::Vector3 offset = camera_position - Position;
	const float len2 = offset.Dot(offset);


	if (len2 > Radius*Radius) {
		return;
	}


	/*
	** f(t) = intensity(t,pos) * sin( omega(t) * t + phi );
	** intensity(t,pos) = intensity * (radius/distance) * timeremaing/totaltime
	** omega(t) = start_omega + (end_omega - start_omega) * t
	** phi = random(0..start_omega)
	*/
	float intensity = Intensity * (1.0f - std::sqrt(len2) / Radius) * (1.0f - ElapsedTime / Duration);
	const float omega_values[] = {Omega.x, Omega.y, Omega.z};
	const float phase_values[] = {Phi.x, Phi.y, Phi.z};
	const float axis_rotation_values[] = {AXIS_ROTATION.x, AXIS_ROTATION.y, AXIS_ROTATION.z};
	float *angle_components[] = {&set_angles->x, &set_angles->y, &set_angles->z};
	for (int i=0; i<3; i++) {
		const float omega = omega_values[i] + (END_OMEGA - omega_values[i]) * ElapsedTime;
		*angle_components[i] += axis_rotation_values[i] * intensity
			* std::sin(omega * ElapsedTime + phase_values[i]);

		//WST 11/14/2002. Add in additional random fudge.  There seems to be a too mathematical pattern of shake with the above
		Engine::Math::Vector3 secondary_angles;
		float minor_intensity = intensity * 0.5f;
		secondary_angles.x = sharedShakeRandom().NextFloat(-minor_intensity,minor_intensity);
		secondary_angles.y = sharedShakeRandom().NextFloat(-minor_intensity,minor_intensity);
		secondary_angles.z = sharedShakeRandom().NextFloat(-minor_intensity,minor_intensity);
		*set_angles = *set_angles + secondary_angles;
	}
}


/************************************************************************************************
**
** CameraShakeSystemClass Implementation
**
************************************************************************************************/
CameraShakeSystemClass::CameraShakeSystemClass()
{
}

CameraShakeSystemClass::~CameraShakeSystemClass()
{
	/*
	** delete all of the objects out of the list
	*/
	while (!CameraShakerList.Is_Empty()) {
		CameraShakerClass * obj = CameraShakerList.Remove_Head();
		CameraShakerList.Remove(obj);
		delete obj;
	}
}

void CameraShakeSystemClass::Add_Camera_Shake
(
	const Engine::Math::Vector3 & position,
	float radius,
	float duration,
	float power
)
{
	//WWMEMLOG(MEM_PHYSICSDATA);
	/*
	** Allocate the visual state owned by the active shaker collection.
	*/

	//Power is in degrees of amplitude.
	power = power * PI/180.0f;

	CameraShakerClass * shaker = new CameraShakerClass(position,radius,duration,power);
	CameraShakerList.Add(shaker);
}

bool CameraShakeSystemClass::IsCameraShaking()
{
	/*
	** Loop through to find if there is any active camera shakers
	*/
	Graphics::SceneObjectList<CameraShakerClass,false>::Cursor iterator(&CameraShakerList);
	for (iterator.First(); !iterator.Is_Done(); iterator.Next()) {
		CameraShakerClass * obj = iterator.Peek_Obj();
		if (obj){
			return (true);
		}
	}
	return(false);
}


void CameraShakeSystemClass::Timestep(float dt)
{
	/*
	** Allow each camera shaker to timestep.  Any that expire are added to a temporary
	** list for deletion.
	*/
	Graphics::SceneObjectList<CameraShakerClass,false> deletelist;
	Graphics::SceneObjectList<CameraShakerClass,false>::Cursor iterator(&CameraShakerList);
	for (iterator.First(); !iterator.Is_Done(); iterator.Next()) {
		CameraShakerClass * obj = iterator.Peek_Obj();
		obj->Timestep(dt);
		if (obj->Is_Expired()) {
			deletelist.Add(obj);
		}
	}

	/*
	** Remove and delete all the ones that expired
	*/
	while (!deletelist.Is_Empty()) {
		CameraShakerClass * obj = deletelist.Remove_Head();
		CameraShakerList.Remove(obj);
		delete obj;
	}
}

void CameraShakeSystemClass::Update_Camera_Shaker(
	Engine::Math::Vector3 camera_position, Engine::Math::Vector3 *shaker_angle)
{
	Graphics::SceneObjectList<CameraShakerClass,false>::Cursor iterator(&CameraShakerList);

	Engine::Math::Vector3 angles{};

	//camera_transform = camera.Get_Transform();
	//camera_transform.Get_Translation(&camera_position);

	/*
	** Accumulate the effects of any active camera shakers
	*/

	for (iterator.First(); !iterator.Is_Done(); iterator.Next()) {
		iterator.Peek_Obj()->Compute_Rotations(camera_position,&angles);
	}

	/*
	** Clamp the result
	*/
	if (shaker_angle != nullptr)
		*shaker_angle = angles;

	/*
	** Apply to the camera
	*/
	/*
	camera_transform.Rotate_X(angles.X);
	camera_transform.Rotate_Y(angles.Y);
	camera_transform.Rotate_Z(angles.Z);
	camera.Set_Transform(camera_transform);
	*/
}

// The Instance of the system
CameraShakeSystemClass CameraShakerSystem; //WST 11/12/2002 This is the new Camera Shaker system upgrade
