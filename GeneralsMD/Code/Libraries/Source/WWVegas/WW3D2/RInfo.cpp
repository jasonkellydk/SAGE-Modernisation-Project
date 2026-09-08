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
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/RInfo.cpp                              $*
 *                                                                                             *
 *                   Org Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 15                                                          $*
 *                                                                                             *
 * 06/27/02 KM Render to shadow buffer texture support														*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "RInfo.h"
#include "Camera.h"
#include "Texture.h"


/***********************************************************************************************
**
** RenderInfoClass Implementation
**
***********************************************************************************************/
RenderInfoClass::RenderInfoClass(CameraClass & cam) :
	Camera(cam),
	fog_start(0.0f),
	fog_end(0.0f),
	fog_scale(0.0f),
	light_environment(nullptr),
	AdditionalMaterialPassCount(0),
	RejectedMaterialPasses(0),
	OverrideFlagLevel(0),
	Texture_Projector(nullptr),
	alphaOverride(1.0f),
	materialPassAlphaOverride(1.0f),
	materialPassEmissiveOverride(1.0f)
{
	// Need to have one entry in the override flags stack, initialize it to default values.
	OverrideFlag[OverrideFlagLevel]=RINFO_OVERRIDE_DEFAULT;
}

RenderInfoClass::~RenderInfoClass()
{
}

void RenderInfoClass::Push_Material_Pass(const std::shared_ptr<NativeMaterialPass>& matpass)
{
	// add to the end of the array
	if (AdditionalMaterialPassCount<MAX_ADDITIONAL_MATERIAL_PASSES-1) {

		AdditionalMaterialPassArray[AdditionalMaterialPassCount++]=matpass;
	} else {
		RejectedMaterialPasses++;
	}
}

void RenderInfoClass::Pop_Material_Pass()
{
	if (RejectedMaterialPasses == 0) {
		// remove from the end of the array
		WWASSERT(AdditionalMaterialPassCount>0);
		AdditionalMaterialPassCount--;
		AdditionalMaterialPassArray[AdditionalMaterialPassCount].reset();
	} else {
		RejectedMaterialPasses--;
	}
}

int RenderInfoClass::Additional_Pass_Count()
{
	return AdditionalMaterialPassCount;
}

NativeMaterialPass * RenderInfoClass::Peek_Additional_Pass(int i)
{
	return AdditionalMaterialPassArray[i].get();
}

void RenderInfoClass::Push_Override_Flags(RINFO_OVERRIDE_FLAGS flg)
{
	// copy to the end of the array
	WWASSERT(OverrideFlagLevel<MAX_OVERRIDE_FLAG_LEVEL - 1);
	OverrideFlagLevel++;
	OverrideFlag[OverrideFlagLevel]=flg;
}

void RenderInfoClass::Pop_Override_Flags()
{
	WWASSERT(OverrideFlagLevel>0);
	OverrideFlagLevel--;
}

RenderInfoClass::RINFO_OVERRIDE_FLAGS & RenderInfoClass::Current_Override_Flags()
{
	return OverrideFlag[OverrideFlagLevel];
}
