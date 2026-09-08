#include "../../../../../../engine/graphics/profiling/Tracy.h"
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
 *                     $Archive:: /Commando/Code/ww3d2/AnimObj.cpp                            $*
 *                                                                                             *
 *                       Author:: Greg_h                                                       *
 *                                                                                             *
 *                     $Modtime:: 12/13/01 6:56p                                              $*
 *                                                                                             *
 *                    $Revision:: 10                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Animatable3DObjClass::Animatable3DObjClass -- constructor                                 *
 *   Animatable3DObjClass::Animatable3DObjClass -- copy constructor                            *
 *   Animatable3DObjClass::~Animatable3DObjClass -- destructor                                 *
 *   Animatable3DObjClass::operator = -- assignment operator                                   *
 *   Animatable3DObjClass::Release -- Releases any anims being held by this object             *
 *   Animatable3DObjClass::Render -- Update this object for rendering                          *
 *   Animatable3DObjClass::Set_Transform -- sets the transform and marks sub-objects as dirty  *
 *   Animatable3DObjClass::Set_Position -- Sets the position and marks sub-objects as dirty    *
 *   Animatable3DObjClass::Get_Num_Bones -- returns number of bones in this object             *
 *   Animatable3DObjClass::Get_Bone_Name -- returns the name of the given bone                 *
 *   Animatable3DObjClass::Get_Bone_Index -- returns the index of the given bone               *
 *   Animatable3DObjClass::Set_Animation -- set the animation state to "none" (base pose)      *
 *   Animatable3DObjClass::Set_Animation -- Set the animation state to the given anim/frame    *
 *   Animatable3DObjClass::Set_Animation -- set the animation state to a blend of two anims    *
 *   Animatable3DObjClass::Set_Animation -- Set animation state with an anim combo             *
 *   Animatable3DObjClass::Get_Bone_Transform -- return the transform for the given bone       *
 *   Animatable3DObjClass::Get_Bone_Transform -- return the transform for the given bone       *
 *   Animatable3DObjClass::Capture_Bone -- capture the specified bone (override animation)     *
 *   Animatable3DObjClass::Release_Bone -- release the specified bone (allow animation)        *
 *   Animatable3DObjClass::Is_Bone_Captured -- returns whether the specified bone is captured  *
 *   Animatable3DObjClass::Control_Bone -- sets the transform for the bone                     *
 *   Animatable3DObjClass::Update_Sub_Object_Transforms -- recalculate the transforms for our  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include <algorithm>
#include "AnimObj.h"
import Graphics.Scene.Models.AnimationChannels;
import Graphics.Scene.Models.Hierarchy;
#include "AssetMgr.h"
#include "WW3D.h"
#include "WWDebug/wwmemlog.h"
import Assets.Cache.Animations;
import Graphics.Scene.Models.ClipSampling;
import Graphics.Scene.Models.AnimationRotation;


/***********************************************************************************************
 * Animatable3DObjClass::Animatable3DObjClass -- constructor                                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 * htree_name -- name of the hierarchy tree which defines the "bone" structure for this object *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Animatable3DObjClass::Animatable3DObjClass(const char * htree_name) :
	IsTreeValid(0),
	CurMotionMode(BASE_POSE)
{
	// Inline struct members can't be initialized in init list for some reason...
  ModeAnim.Motion=nullptr;
	ModeAnim.Frame=0.0f;
	ModeAnim.LastSyncTime=WW3D::Get_Logic_Time_Milliseconds();
	ModeAnim.frameRateMultiplier=1.0;	// 020607 srj -- added
	ModeAnim.animDirection=1.0;	// 020607 srj -- added
	ModeInterp.Motion0=nullptr;
	ModeInterp.Motion1=nullptr;
	ModeInterp.Frame0=0.0f;
	ModeInterp.Frame1=0.0f;
	ModeInterp.Percentage=0.0f;

	/*
	** Store a pointer to the htree
	*/
	if (htree_name == nullptr) {
		Hierarchy = nullptr;
	} else if (htree_name[0] == 0) {
		Hierarchy = W3DNEW Graphics::ModelHierarchy;
		Hierarchy->Initialize_Default ();
	} else {
		auto* assets = WW3DAssetManager::Get_Instance();
		const auto skeleton = assets->Get_Skeleton(htree_name);
		const auto* source = assets->Resolve_Skeleton(skeleton);
		if (source != nullptr) {
			Hierarchy = W3DNEW Graphics::ModelHierarchy(*source);
		} else {
			WWDEBUG_SAY(("Unable to find Hierarchy: %s",htree_name));
			Hierarchy = W3DNEW Graphics::ModelHierarchy;
			Hierarchy->Initialize_Default();
		}
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Animatable3DObjClass -- copy constructor                              *
 *                                                                                             *
 * INPUT:                                                                                      *
 * src -- animatable object to copy.                                                           *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Animatable3DObjClass::Animatable3DObjClass(const Animatable3DObjClass & src) :
	CompositeRenderObjClass(src),
	IsTreeValid(0),
	CurMotionMode(BASE_POSE),
	Hierarchy(nullptr)
{
   // Inline struct members can't be initialized in init list for some reason...
	ModeAnim.Motion=nullptr;
	ModeAnim.Frame=0.0f;
	ModeAnim.LastSyncTime=WW3D::Get_Logic_Time_Milliseconds();
	ModeAnim.frameRateMultiplier=1.0;	// 020607 srj -- added
	ModeAnim.animDirection=1.0;	// 020607 srj -- added
	ModeInterp.Motion0=nullptr;
	ModeInterp.Motion1=nullptr;
	ModeInterp.Frame0=0.0f;
	ModeInterp.Frame1=0.0f;
	ModeInterp.Percentage=0.0f;

	*this = src;
}


/***********************************************************************************************
 * Animatable3DObjClass::~Animatable3DObjClass -- destructor                                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Animatable3DObjClass::~Animatable3DObjClass()
{
	Release();

	delete Hierarchy;
}


/***********************************************************************************************
 * Animatable3DObjClass::operator = -- assignment operator                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
Animatable3DObjClass & Animatable3DObjClass::operator = (const Animatable3DObjClass & that)
{
	if (&that != this) {
		Release();

		CompositeRenderObjClass::operator = (that);

		IsTreeValid = 0;
		CurMotionMode = BASE_POSE;
		ModeAnim.Motion = nullptr;
		ModeAnim.Frame = 0.0f;
		ModeAnim.LastSyncTime = WW3D::Get_Logic_Time_Milliseconds();
		ModeAnim.frameRateMultiplier=1.0;	// 020607 srj -- added
		ModeAnim.animDirection=1.0;	// 020607 srj -- added
		ModeInterp.Motion0 = nullptr;
		ModeInterp.Motion1 = nullptr;
		ModeInterp.Frame0 = 0.0f;
		ModeInterp.Frame1 = 0.0f;
		ModeInterp.Percentage = 0.0f;

		delete Hierarchy;
		Hierarchy = W3DNEW Graphics::ModelHierarchy(*that.Hierarchy);
	}
	return *this;
}

/***********************************************************************************************
 * Animatable3DObjClass::Release -- Releases any anims being held by this object               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Release()
{
	switch (CurMotionMode) {

		case BASE_POSE:
			break;

		case SINGLE_ANIM:
			if ( ModeAnim.Motion != nullptr ) {
				Assets::Get_Animation_Cache().Release(ModeAnim.Motion);
				ModeAnim.Motion = nullptr;
			}
			break;

		case DOUBLE_ANIM:
			if ( ModeInterp.Motion0 != nullptr ) {
				Assets::Get_Animation_Cache().Release(ModeInterp.Motion0);
				ModeInterp.Motion0 = nullptr;
			}

			if ( ModeInterp.Motion1 != nullptr ) {
				Assets::Get_Animation_Cache().Release(ModeInterp.Motion1);
				ModeInterp.Motion1 = nullptr;
			}
			break;


		default:
			break;
	}
}

/***********************************************************************************************
 * Animatable3DObjClass::Render -- Update this object for rendering                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Render(RenderInfoClass & rinfo)
{
	if (Hierarchy == nullptr) return;

	if (Is_Not_Hidden_At_All() == false) {
		return;
	}

	//
	// Force the hierarchy to be recalculated for single animations.
	//
	const bool isSingleAnim = CurMotionMode == SINGLE_ANIM && ModeAnim.AnimMode != ANIM_MODE_MANUAL;

	if (isSingleAnim || !Is_Hierarchy_Valid() || Are_Sub_Object_Transforms_Dirty()) {
		Update_Sub_Object_Transforms();
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Set_Transform -- sets the transform and marks sub-objects as dirty    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Transform(const Matrix3D &m)
{
	CompositeRenderObjClass::Set_Transform(m);
	Set_Hierarchy_Valid(false);
}


/***********************************************************************************************
 * Animatable3DObjClass::Set_Position -- Sets the position and marks sub-objects as dirty      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Position(const Vector3 &v)
{
	CompositeRenderObjClass::Set_Position(v);
	Set_Hierarchy_Valid(false);
}


/***********************************************************************************************
 * Animatable3DObjClass::Get_Num_Bones -- returns number of bones in this object               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
int Animatable3DObjClass::Get_Num_Bones()
{
	if (Hierarchy) {
		return Hierarchy->Bone_Count();
	} else {
		return 1;
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Get_Bone_Name -- returns the name of the given bone                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
const char * Animatable3DObjClass::Get_Bone_Name(int bone_index)
{
	if (Hierarchy) {
		return Hierarchy->Bone_Name(bone_index);
	} else {
		return "RootTransform";
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Get_Bone_Index -- returns the index of the given bone                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
int Animatable3DObjClass::Get_Bone_Index(const char * bonename)
{
	if (Hierarchy) {
		return Hierarchy->Bone_Index(bonename);
	} else {
		return 0;
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Set_Animation -- set the animation state to "none" (base pose)        *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Animation()
{
	Release();
	CurMotionMode = BASE_POSE;
	Set_Hierarchy_Valid(false);
}


/***********************************************************************************************
 * Animatable3DObjClass::Set_Animation -- Set the animation state to the given anim/frame      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Animation(Assets::AnimationAssetHandle motion, float frame, int mode)
{

	if ( motion ) {
		// Retain before releasing the previous playback state, which may alias it.
		Assets::Get_Animation_Cache().Retain(motion);
		Release();
		CurMotionMode = SINGLE_ANIM;
		ModeAnim.Motion = motion;
		ModeAnim.Frame = frame;
		ModeAnim.LastSyncTime = WW3D::Get_Logic_Time_Milliseconds();
		ModeAnim.frameRateMultiplier=1.0;	// 020607 srj -- added
		ModeAnim.animDirection=1.0;	// 020607 srj -- added

		ModeAnim.AnimMode = mode;

		if (mode < ANIM_MODE_LOOP_BACKWARDS)
			ModeAnim.animDirection = 1.0f;	//assume playing forwards
		else
			ModeAnim.animDirection = -1.0f;	//reverse animation playback

	} else {
		Release();
		CurMotionMode = BASE_POSE;
	}

	Set_Hierarchy_Valid(false);
}

/***********************************************************************************************
 * Animatable3DObjClass::Set_Animation -- set the animation state to a blend of two anims      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Animation
(
	Assets::AnimationAssetHandle motion0,
	float frame0,
	Assets::AnimationAssetHandle motion1,
	float frame1,
	float percentage
)
{
	Assets::Get_Animation_Cache().Retain(motion0);
	Assets::Get_Animation_Cache().Retain(motion1);
	Release();

	CurMotionMode = DOUBLE_ANIM;
	ModeInterp.Motion0 = motion0;
	ModeInterp.Motion1 = motion1;
	ModeInterp.Frame0 = frame0;
	ModeInterp.Frame1 = frame1;
	ModeInterp.Percentage = percentage;
	Set_Hierarchy_Valid(false);

}


/***********************************************************************************************
 * Animatable3DObjClass::Peek_Animation													                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Assets::AnimationAssetHandle Animatable3DObjClass::Peek_Animation()
{
	if ( CurMotionMode == SINGLE_ANIM ) {
		return ModeAnim.Motion;
	} else {
		return nullptr;
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Get_Bone_Transform -- return the transform for the given bone         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Matrix3D 	Animatable3DObjClass::Get_Bone_Transform(const char * bonename)
{
	if (Hierarchy) {
		WWASSERT(Hierarchy);
		WWASSERT(bonename);

		int idx = Hierarchy->Bone_Index(bonename);
		return Get_Bone_Transform(idx);
	} else {
		return Get_Transform();
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Get_Bone_Transform -- return the transform for the given bone         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
Matrix3D 	Animatable3DObjClass::Get_Bone_Transform(int boneindex)
{
	Validate_Transform();

	if (Hierarchy) {
		/*
		** If our hierarchy isn't valid, we just need to evaluate our animation
		** state.
		*/
		if (!Is_Hierarchy_Valid()) {
			Update_Sub_Object_Transforms();
		}

		return Graphics::Export_Affine_Transform<Matrix3D>(Hierarchy->World_Transform(boneindex));
	} else {
		return Transform;
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Capture_Bone -- capture the specified bone (override animation)       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Capture_Bone(int boneindex)
{
	if (Hierarchy) {
		Hierarchy->Capture(boneindex);
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Release_Bone -- release the specified bone (allow animation)          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Release_Bone(int boneindex)
{
	if (Hierarchy) {
		Hierarchy->Release(boneindex);
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Is_Bone_Captured -- returns whether the specified bone is captured    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
bool Animatable3DObjClass::Is_Bone_Captured(int boneindex) const
{
	if (Hierarchy) {
		return Hierarchy->Is_Captured(boneindex);
	} else {
		return false;
	}
}


/***********************************************************************************************
 * Animatable3DObjClass::Control_Bone -- sets the transform for the bone                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/2/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Control_Bone(int bindex,const Matrix3D & objtm,bool world_space_translation)
{
#ifdef WWDEBUG
	for (int j=0; j<3; j++) {
		for (int i=0; i<4; i++) {
			WWASSERT(WWMath::Is_Valid_Float(objtm[j][i]));
		}
	}
#endif

	if (Hierarchy) {
		Hierarchy->Control(bindex,Graphics::Import_Affine_Transform(objtm),world_space_translation);
		Set_Hierarchy_Valid(false);
	}
}

/***********************************************************************************************
 * Animatable3DObjClass::Update_Sub_Object_Transforms -- recalculate the transforms for our su *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/8/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Update_Sub_Object_Transforms()
{
	/*
	** The RenderObj implementation will cause our 'container'
	** to update if we are not valid yet
	*/
	CompositeRenderObjClass::Update_Sub_Object_Transforms();

	/*
	** Update the transforms
	*/
	switch (CurMotionMode) {

		case BASE_POSE:
			Base_Update(Transform);
			break;

		case SINGLE_ANIM:

			if ( ModeAnim.AnimMode != ANIM_MODE_MANUAL ) {
				Single_Anim_Progress();
			}
			Anim_Update(Transform,ModeAnim.Motion,ModeAnim.Frame);

			break;

		case DOUBLE_ANIM:
			Blend_Update(Transform,ModeInterp.Motion0,ModeInterp.Frame0,
				ModeInterp.Motion1,ModeInterp.Frame1,ModeInterp.Percentage);

			break;

		default:
			break;
	}
	Set_Hierarchy_Valid(true);
}


/***********************************************************************************************
 * Animatable3DObjClass::Simple_Evaluate_Bone -- If the animation is 'single', evaluate the    *
 *																	given pivot and return its transform.		  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   04/13/2000    PDS : Created.                                                                *
 *=============================================================================================*/
bool Animatable3DObjClass::Simple_Evaluate_Bone(int boneindex, Matrix3D *tm) const
{
    if (!tm) return false;
    if (CurMotionMode == NONE || CurMotionMode == BASE_POSE || CurMotionMode == SINGLE_ANIM) {
        const float frame = Compute_Current_Frame();
        return Simple_Evaluate_Bone(boneindex,frame,tm);
    }
    const_cast<Animatable3DObjClass*>(this)->Update_Sub_Object_Transforms();
    *tm = Graphics::Export_Affine_Transform<Matrix3D>(Hierarchy->World_Transform(boneindex));
    return false;
}


/***********************************************************************************************
 * Animatable3DObjClass::Compute_Current_Frame -- Returns the animation frame for the next rend*
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS: Only works for Single and CSingle!                                                *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   04/13/2000    PDS : Created.                                                              *
 *=============================================================================================*/
float Animatable3DObjClass::Compute_Current_Frame(float *newDirection) const
{
	float frame = 0;
	float direction = ModeAnim.animDirection;

	switch (CurMotionMode)
	{
		case SINGLE_ANIM:
		{
			frame = ModeAnim.Frame;

			if (ModeAnim.AnimMode != ANIM_MODE_MANUAL) {
				//
				//	Compute the current frame based on elapsed time.
				//	TheSuperHackers @info Is using elapsed time because frame computation is not guaranteed to be called every render frame!
				//
				// TheSuperHackers @tweak The animation render update is now decoupled from the logic step.
				const float syncMilliseconds = WW3D::Get_Logic_Time_Milliseconds() - ModeAnim.LastSyncTime;
				const float animMilliseconds = Assets::Get_Animation_Cache().Resolve(ModeAnim.Motion)->frame_rate * ModeAnim.frameRateMultiplier * ModeAnim.animDirection * syncMilliseconds;
				const float animSeconds = animMilliseconds * 0.001f;
				frame += animSeconds;

				//
				//	Wrap the frame
				//
				const int numFrames = static_cast<int>(Assets::Get_Animation_Cache().Resolve(ModeAnim.Motion)->frame_count) - 1;

				switch (ModeAnim.AnimMode)
				{
					case ANIM_MODE_ONCE:
						if (frame >= numFrames) {
							frame = numFrames;
						}
						break;
					case ANIM_MODE_LOOP:
						if ( frame >= numFrames ) {
							frame -= numFrames;
							// If it is still too far out, reset
							if ( frame >= numFrames ) {
								frame = 0;
							}
						}
						break;
					case ANIM_MODE_ONCE_BACKWARDS:	//play animation one time but backwards
						if (frame < 0) {
							frame = 0;
						}
						break;
					case ANIM_MODE_LOOP_BACKWARDS:	//play animation backwards in a loop
						if ( frame < 0 ) {
							frame += numFrames;
							// If it is still too far out, reset
							if ( frame < 0 ) {
								frame = numFrames;
							}
						}
						break;
					case ANIM_MODE_LOOP_PINGPONG:
						if (ModeAnim.animDirection >= 1.0f)
						{	//playing forwards, reverse direction
							if (frame >= numFrames)
							{	//step backwards in animation by excess time
								frame = numFrames * 2 - frame;
								// If it is still too far out, reset
								if ( frame >= numFrames - 1 )
									frame = numFrames;
								direction = ModeAnim.animDirection * -1.0f;
							}
						}
						else
						{	//playing backwards, reverse direction
							if (frame < 0)
							{	//step forwards in animation by excess time
								frame = -frame;
								// If it is still too far out, reset
								if ( frame >= numFrames )
										frame = 0;
								direction = ModeAnim.animDirection * -1.0f;
							}
						}
						break;
				}
			}
		}
		break;
	}

	if (newDirection)
		*newDirection = direction;
	return frame;
}

/***********************************************************************************************
 * Animatable3DObjClass::Single_Anim_Progress -- progress anims for loop and once               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:  Only works for Single and CSingle                                                *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/26/99    BMG : Created.                                                                 *
 *=============================================================================================*/
void Animatable3DObjClass::Single_Anim_Progress ()
{
	//
	//	Update the current frame (only works in "SINGLE_ANIM" mode!)
	//
	WWASSERT(CurMotionMode == SINGLE_ANIM);

	//
	// Update the frame number and sync time
	//
	ModeAnim.Frame				= Compute_Current_Frame(&ModeAnim.animDirection);
	ModeAnim.LastSyncTime	= WW3D::Get_Logic_Time_Milliseconds();
}


/***********************************************************************************************
 * Animatable3DObjClass::Is_Animation_Complete -- is the current animation on the last frame?  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS: Only works for Single, ONCE anims                                                 *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/13/99    BMG : Created.                                                                 *
 *=============================================================================================*/
bool	Animatable3DObjClass::Is_Animation_Complete() const
{
	if (CurMotionMode == SINGLE_ANIM) {

		if ( ModeAnim.AnimMode == ANIM_MODE_ONCE ) {
			return ( ModeAnim.Frame == static_cast<int>(Assets::Get_Animation_Cache().Resolve(ModeAnim.Motion)->frame_count) - 1 );
		}
		else
		if ( ModeAnim.AnimMode == ANIM_MODE_ONCE_BACKWARDS)
		{	return ( ModeAnim.Frame == 0);
		}
	}
	return false;
}

/***********************************************************************************************
 * Animatable3DObjClass::Peek_Animation_And_Info *
 *=============================================================================================*/
Assets::AnimationAssetHandle Animatable3DObjClass::Peek_Animation_And_Info(float& frame, int& numFrames, int& mode, float& mult)
{
	if ( CurMotionMode == SINGLE_ANIM ) {
		frame = ModeAnim.Frame;
		numFrames = ModeAnim.Motion ? static_cast<int>(Assets::Get_Animation_Cache().Resolve(ModeAnim.Motion)->frame_count) : 0;
		mode = ModeAnim.AnimMode;
		mult = ModeAnim.frameRateMultiplier;
		return ModeAnim.Motion;
	} else {
		return nullptr;
	}
}

/***********************************************************************************************
 * Animatable3DObjClass::Set_Animation_Frame_Rate_Multiplier *
 *=============================================================================================*/
void Animatable3DObjClass::Set_Animation_Frame_Rate_Multiplier(float multiplier)
{
	// 020607 srj -- added
	ModeAnim.frameRateMultiplier = multiplier;
}

// (gth) TESTING DYNAMICALLY SWAPPING SKELETONS!

// EOF - AnimObj.cpp

void Animatable3DObjClass::Anim_Update(const Matrix3D & root,Assets::AnimationAssetHandle motion,float frame)
{
    if (!motion || !Hierarchy) return;
    GRAPHICS_PROFILE_SCOPE("Graphics.Models.SampleHierarchy");
#if !WW3D_ENABLE_RAW_ANIM_INTERPOLATION
    if (Assets::Get_Animation_Cache().Resolve(motion)->sampling == Assets::AnimationSampling::Consecutive) {
        if (WW3D::Get_Sync_Frame_Time() == 0 && (int)Assets::Get_Animation_Cache().Resolve(motion)->frame_rate == WWSyncPerSecond) {
            Set_Hierarchy_Valid(true);
            return;
        }
        int integer_frame = WWMath::Float_To_Long(frame);
        if (integer_frame >= static_cast<int>(Assets::Get_Animation_Cache().Resolve(motion)->frame_count)) integer_frame = 0;
        const auto& data = Assets::Get_Animation_Cache().Resolve(motion)->channels;
        const int count = static_cast<int>(Assets::Get_Animation_Cache().Resolve(motion)->bone_count);
        Hierarchy->Evaluate(Graphics::Import_Affine_Transform(root),[&](int bone) {
            Graphics::BoneMotion sample;
            if (bone >= count) return sample;
            sample.translate = sample.set_visibility = true;
            if (data.Channel(bone,Assets::ModelChannelComponent::TranslationX)) Graphics::Copy_Animation_Frame(*data.Channel(bone,Assets::ModelChannelComponent::TranslationX),integer_frame,&sample.translation[0]);
            if (data.Channel(bone,Assets::ModelChannelComponent::TranslationY)) Graphics::Copy_Animation_Frame(*data.Channel(bone,Assets::ModelChannelComponent::TranslationY),integer_frame,&sample.translation[1]);
            if (data.Channel(bone,Assets::ModelChannelComponent::TranslationZ)) Graphics::Copy_Animation_Frame(*data.Channel(bone,Assets::ModelChannelComponent::TranslationZ),integer_frame,&sample.translation[2]);
            if (data.Channel(bone,Assets::ModelChannelComponent::Rotation)) {
                sample.rotate = true;
                sample.orientation = Graphics::Read_Animation_Frame(*data.Channel(bone,Assets::ModelChannelComponent::Rotation),integer_frame);
            }
            if (data.Channel(bone,Assets::ModelChannelComponent::Visibility)) sample.visible = Graphics::Sample_Animation_Visibility(*data.Channel(bone,Assets::ModelChannelComponent::Visibility),integer_frame);
            return sample;
        });
        Set_Hierarchy_Valid(true);
        return;
    }
#endif
    const int count = static_cast<int>(Assets::Get_Animation_Cache().Resolve(motion)->bone_count);
    Hierarchy->Evaluate(Graphics::Import_Affine_Transform(root),[&](int bone) {
        Graphics::BoneMotion sample;
        if (bone >= count) return sample;
        sample.translation = Graphics::Sample_Clip_Translation(Assets::Get_Animation_Cache(),motion,bone,frame);
        sample.orientation = Graphics::Sample_Clip_Rotation(Assets::Get_Animation_Cache(),motion,bone,frame);
        sample.translate = sample.rotate = sample.set_visibility = true;
        sample.visible = Graphics::Sample_Clip_Visibility(Assets::Get_Animation_Cache(),motion,bone,frame);
        return sample;
    });
    Set_Hierarchy_Valid(true);
}

void Animatable3DObjClass::Blend_Update(const Matrix3D & root,Assets::AnimationAssetHandle motion0,float frame0,
    Assets::AnimationAssetHandle motion1,float frame1,float percentage)
{
    if (!Hierarchy) return;
    GRAPHICS_PROFILE_SCOPE("Graphics.Models.BlendHierarchy");
    const int count = std::min(static_cast<int>(Assets::Get_Animation_Cache().Resolve(motion0)->bone_count),static_cast<int>(Assets::Get_Animation_Cache().Resolve(motion1)->bone_count));
    Hierarchy->Evaluate(Graphics::Import_Affine_Transform(root),[&](int bone) {
        Graphics::BoneMotion sample;
        if (bone >= count) return sample;
        const auto t0=Graphics::Sample_Clip_Translation(Assets::Get_Animation_Cache(),motion0,bone,frame0);
        const auto t1=Graphics::Sample_Clip_Translation(Assets::Get_Animation_Cache(),motion1,bone,frame1);
        const Vector3 translation0(t0[0],t0[1],t0[2]),translation1(t1[0],t1[1],t1[2]);
        const Vector3 translation = (1.0 - percentage) * translation0 + percentage * translation1;
        const auto orientation0=Graphics::Sample_Clip_Rotation(Assets::Get_Animation_Cache(),motion0,bone,frame0);
        const auto orientation1=Graphics::Sample_Clip_Rotation(Assets::Get_Animation_Cache(),motion1,bone,frame1);
        const auto orientation=Graphics::Interpolate_Animation_Rotation(orientation0,orientation1,percentage);
        sample.translation = {translation.X,translation.Y,translation.Z};
        sample.orientation = {orientation[0],orientation[1],orientation[2],orientation[3]};
        sample.translate = sample.rotate = sample.set_visibility = true;
        sample.visible = Graphics::Sample_Clip_Visibility(Assets::Get_Animation_Cache(),motion0,bone,frame0) || Graphics::Sample_Clip_Visibility(Assets::Get_Animation_Cache(),motion1,bone,frame1);
        return sample;
    });
    Set_Hierarchy_Valid(true);
}

bool Animatable3DObjClass::Simple_Evaluate_Bone(int boneindex,float frame,Matrix3D * tm) const
{
    GRAPHICS_PROFILE_SCOPE("Graphics.Models.EvaluateBone");
    if (!tm) return false;
    if (!Hierarchy || (CurMotionMode != SINGLE_ANIM && CurMotionMode != NONE && CurMotionMode != BASE_POSE)) {
        *tm = Transform;
        return false;
    }
    Graphics::RenderTransform result;
    const bool valid = Hierarchy->Evaluate_Bone(boneindex,Graphics::Import_Affine_Transform(Get_Transform()),
        [&](int bone) {
            if (CurMotionMode != SINGLE_ANIM) return Graphics::Affine_Identity();
            return Graphics::Sample_Clip_Transform(Assets::Get_Animation_Cache(),ModeAnim.Motion,bone,frame);
        },result);
    *tm = Graphics::Export_Affine_Transform<Matrix3D>(result);
    return valid;
}
