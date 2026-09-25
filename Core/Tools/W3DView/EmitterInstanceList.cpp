/*
**	Command & Conquer Renegade(tm)
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
 *                 Project Name : W3DView                                                      *
 *                                                                                             *
 *                     $Archive:: /VSS_Sync/W3DView/EmitterInstanceList.cpp                                                                                                                                                                                                                                                                                                                                $Modtime::                                                             $*
 *                                                                                             *
 *                    $Revision:: 7                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "StdAfx.h"
#include "EmitterInstanceList.h"
#include "Utils.h"

#include <vector>

/////////////////////////////////////////////////////////////////////
//
//	~EmitterInstanceListClass
//
/////////////////////////////////////////////////////////////////////
EmitterInstanceListClass::~EmitterInstanceListClass ()
{
	Free_List ();
}


/////////////////////////////////////////////////////////////////////
//
//	Free_List
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Free_List ()
{
	//
	//	Release our hold on each of the emitter pointers
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		REF_PTR_RELEASE (m_List[index]);
	}

	m_List.Delete_All ();
}


/////////////////////////////////////////////////////////////////////
//
//	Add_Emitter
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Add_Emitter (ParticleEmitterClass *emitter)
{
	ASSERT (emitter != nullptr);
	if (emitter != nullptr) {

		//
		//	If this is the first emitter in the list, then initialize
		// the definition to it's state
		//
		if (m_List.Count () == 0) {
			ParticleEmitterDefClass *def = emitter->Build_Definition ();
			if (def != nullptr) {
				ParticleEmitterDefClass::operator= (*def);
				SAFE_DELETE (def);
			}
		}

		//
		//	Add this emitter to the list and put a hold on its reference
		//
		if (emitter)
			emitter->Add_Ref();
		m_List.Add (emitter);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Velocity
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Velocity (const Vector3 &value)
{
	ParticleEmitterDefClass::Set_Velocity (value);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Set_Base_Velocity (value);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Acceleration
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Acceleration (const Vector3 &value)
{
	ParticleEmitterDefClass::Set_Acceleration (value);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Set_Acceleration (value);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Burst_Size
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Burst_Size (unsigned int count)
{
	ParticleEmitterDefClass::Set_Burst_Size (count);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Set_Burst_Size (count);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Outward_Vel
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Outward_Vel (float value)
{
	ParticleEmitterDefClass::Set_Outward_Vel (value);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Set_Outwards_Velocity (value);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Vel_Inherit
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Vel_Inherit (float value)
{
	ParticleEmitterDefClass::Set_Vel_Inherit (value);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Set_Velocity_Inheritance_Factor (value);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Velocity_Random
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Velocity_Random (Engine::Math::RandomVector3Generator *randomizer)
{
	ParticleEmitterDefClass::Set_Velocity_Random (randomizer);
	if (randomizer != nullptr) {

		//
		//	Pass this setting onto the emitters immediately
		//
		for (int index = 0; index < m_List.Count (); index ++) {
			m_List[index]->Set_Velocity_Randomizer (new Engine::Math::RandomVector3Generator (*randomizer));
		}
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Color_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Color_Keyframes (ParticlePropertyStruct<Vector3> &keyframes)
{
	//
	//	Make sure tha any value that is supposed to go to zero, really
	//	does even if its got a randomizer.
	//
	if (	(keyframes.Rand.X != 0) ||
			(keyframes.Rand.Y != 0) ||
			(keyframes.Rand.Z != 0))
	{
		for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
			if ((keyframes.Values[index].X <= 0.000001F) &&
				 (keyframes.Values[index].Y <= 0.000001F) &&
				 (keyframes.Values[index].Z <= 0.000001F)) {
				keyframes.Values[index].X = -keyframes.Rand.X;
				keyframes.Values[index].Y = -keyframes.Rand.Y;
				keyframes.Values[index].Z = -keyframes.Rand.Z;
			}
		}
	}

	ParticleEmitterDefClass::Set_Color_Keyframes (keyframes);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Colors (keyframes);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Color_Keyframes (Engine::Math keyframes)
//
//	Converts the property-page keyframes to the emitter definition's
// type and forwards them to the virtual override above. The in-place
// normalization of the override is reflected back into 'keyframes'.
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Color_Keyframes (ParticlePropertyStruct<Engine::Math::Vector3> &keyframes)
{
	ParticlePropertyStruct<Vector3> render_keyframes{};
	render_keyframes.Start = Vector3(keyframes.Start.x, keyframes.Start.y, keyframes.Start.z);
	render_keyframes.Rand = Vector3(keyframes.Rand.x, keyframes.Rand.y, keyframes.Rand.z);
	render_keyframes.NumKeyFrames = keyframes.NumKeyFrames;
	std::vector<float> render_key_times(keyframes.NumKeyFrames);
	std::vector<Vector3> render_values(keyframes.NumKeyFrames);
	render_keyframes.KeyTimes = render_key_times.data();
	render_keyframes.Values = render_values.data();
	for (UINT index = 0; index < keyframes.NumKeyFrames; ++index) {
		render_keyframes.KeyTimes[index] = keyframes.KeyTimes[index];
		render_keyframes.Values[index] = Vector3(
			keyframes.Values[index].x,
			keyframes.Values[index].y,
			keyframes.Values[index].z);
	}

	Set_Color_Keyframes (render_keyframes);

	for (UINT index = 0; index < keyframes.NumKeyFrames; ++index) {
		keyframes.Values[index] = {render_keyframes.Values[index].X,
			render_keyframes.Values[index].Y, render_keyframes.Values[index].Z};
	}
	render_keyframes.KeyTimes = nullptr;
	render_keyframes.Values = nullptr;
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Opacity_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Opacity_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	//
	//	Make sure tha any value that is supposed to go to zero, really
	//	does even if its got a randomizer.
	//
	if (keyframes.Rand != 0)
	{
		for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
			if (keyframes.Values[index] <= 0.000001F) {
				keyframes.Values[index] = -keyframes.Rand;
			}
		}
	}

	ParticleEmitterDefClass::Set_Opacity_Keyframes (keyframes);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Opacity (keyframes);
	}
}


/////////////////////////////////////////////////////////////////////
//
//	Set_Size_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Size_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	//
	//	Make sure tha any value that is supposed to go to zero, really
	//	does even if its got a randomizer.
	//
	if (keyframes.Rand != 0)
	{
		for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
			if (keyframes.Values[index] <= 0.000001F) {
				keyframes.Values[index] = -keyframes.Rand;
			}
		}
	}

	ParticleEmitterDefClass::Set_Size_Keyframes (keyframes);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Size (keyframes);
	}
}



/////////////////////////////////////////////////////////////////////
//
//	Set_Rotation_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Rotation_Keyframes (ParticlePropertyStruct<float> &keyframes, float orient_rnd)
{
	ParticleEmitterDefClass::Set_Rotation_Keyframes (keyframes, orient_rnd);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Rotations (keyframes, orient_rnd);
	}
}

/////////////////////////////////////////////////////////////////////
//
//	Set_Frame_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Frame_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	ParticleEmitterDefClass::Set_Frame_Keyframes (keyframes);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Frames (keyframes);
	}
}

/////////////////////////////////////////////////////////////////////
//
//	Set_Blur_Time_Keyframes
//
/////////////////////////////////////////////////////////////////////
void
EmitterInstanceListClass::Set_Blur_Time_Keyframes (ParticlePropertyStruct<float> &keyframes)
{
	ParticleEmitterDefClass::Set_Blur_Time_Keyframes (keyframes);

	//
	//	Pass this setting onto the emitters immediately
	//
	for (int index = 0; index < m_List.Count (); index ++) {
		m_List[index]->Reset_Blur_Times (keyframes);
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Color_Keyframes
//
void
EmitterInstanceListClass::Get_Color_Keyframes (ParticlePropertyStruct<Vector3> &keyframes) const
{
	ParticleEmitterDefClass::Get_Color_Keyframes (keyframes);

	//
	//	Normalize the data
	//
	for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
		if (keyframes.Values[index].X <= 0.000001F) {
			keyframes.Values[index].X = 0;
		}
		if (keyframes.Values[index].Y <= 0.000001F) {
			keyframes.Values[index].Y = 0;
		}
		if (keyframes.Values[index].Z <= 0.000001F) {
			keyframes.Values[index].Z = 0;
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Color_Keyframes (Engine::Math keyframes)
//
//	Reads the (normalized) keyframes through the virtual override above and
// converts them to the property-page type. The caller owns the returned arrays.
//
void
EmitterInstanceListClass::Get_Color_Keyframes (ParticlePropertyStruct<Engine::Math::Vector3> &keyframes) const
{
	ParticlePropertyStruct<Vector3> render_keyframes{};
	Get_Color_Keyframes (render_keyframes);
	keyframes.Start = {render_keyframes.Start.X, render_keyframes.Start.Y, render_keyframes.Start.Z};
	keyframes.Rand = {render_keyframes.Rand.X, render_keyframes.Rand.Y, render_keyframes.Rand.Z};
	keyframes.NumKeyFrames = render_keyframes.NumKeyFrames;
	keyframes.KeyTimes = keyframes.NumKeyFrames > 0 ? new float[keyframes.NumKeyFrames] : nullptr;
	keyframes.Values = keyframes.NumKeyFrames > 0 ? new Engine::Math::Vector3[keyframes.NumKeyFrames] : nullptr;
	for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
		keyframes.KeyTimes[index] = render_keyframes.KeyTimes[index];
		keyframes.Values[index] = {render_keyframes.Values[index].X,
			render_keyframes.Values[index].Y, render_keyframes.Values[index].Z};
	}
	SAFE_DELETE_ARRAY(render_keyframes.KeyTimes);
	SAFE_DELETE_ARRAY(render_keyframes.Values);
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Opacity_Keyframes
//
void
EmitterInstanceListClass::Get_Opacity_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	ParticleEmitterDefClass::Get_Opacity_Keyframes (keyframes);

	//
	//	Normalize the data
	//
	for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
		if (keyframes.Values[index] <= 0.000001F) {
			keyframes.Values[index] = 0;
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////
//
//	Get_Size_Keyframes
//
void
EmitterInstanceListClass::Get_Size_Keyframes (ParticlePropertyStruct<float> &keyframes) const
{
	ParticleEmitterDefClass::Get_Size_Keyframes (keyframes);

	//
	//	Normalize the data
	//
	for (UINT index = 0; index < keyframes.NumKeyFrames; index ++) {
		if (keyframes.Values[index] <= 0.000001F) {
			keyframes.Values[index] = 0;
		}
	}
}
