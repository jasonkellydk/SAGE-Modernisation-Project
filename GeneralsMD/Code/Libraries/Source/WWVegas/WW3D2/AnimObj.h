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
 *                     $Archive:: /Commando/Code/ww3d2/AnimObj.h                              $*
 *                                                                                             *
 *                       Author:: Greg_h                                                       *
 *                                                                                             *
 *                     $Modtime:: 12/10/01 11:18a                                             $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Animatable3DObjClass::Base_Update -- animation update function for the base pose          *
 *   Animatable3DObjClass::Anim_Update -- Update function for a single animation               *
 *   Animatable3DObjClass::Blend_Update -- update function for a blend of two animations       *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
import Assets.Cache.Animations;


#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "WWLib/always.h"
#include "WW3D2/Composite.h"
import Graphics.Scene.Models.Hierarchy;

class SkinClass;
class RenderInfoClass;



/**
** Animatable3DObjClass
** This class performs some of the work necessary to implement hierarchical animation.
** It implements much of the bone and animation interface of RenderObjClass.
*/
class Animatable3DObjClass : public CompositeRenderObjClass
{
public:

	Animatable3DObjClass(const char * htree_name);
	Animatable3DObjClass(const Animatable3DObjClass & src);
	Animatable3DObjClass & operator = (const Animatable3DObjClass &);
	virtual ~Animatable3DObjClass() override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Rendering
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Render(RenderInfoClass & rinfo) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - "Scene Graph"
	/////////////////////////////////////////////////////////////////////////////
	virtual void 					Set_Transform(const Matrix3D &m) override;
	virtual void 					Set_Position(const Vector3 &v) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Hierarchical Animation
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Set_Animation() override;
	virtual void					Set_Animation( Assets::AnimationAssetHandle motion,
															float frame, int anim_mode = ANIM_MODE_MANUAL) override;
	virtual void					Set_Animation( Assets::AnimationAssetHandle motion0,
															float frame0,
															Assets::AnimationAssetHandle motion1,
															float frame1,
															float percentage) override;

	virtual void					Set_Animation_Frame_Rate_Multiplier(float multiplier);	// 020607 srj -- added

	virtual Assets::AnimationAssetHandle Peek_Animation_And_Info(float& frame, int& numFrames, int& mode, float& mult);	// 020710 srj -- added

	virtual Assets::AnimationAssetHandle Peek_Animation() override;
	virtual bool					Is_Animation_Complete() const;
	virtual int						Get_Num_Bones() override;
	virtual const char *			Get_Bone_Name(int bone_index) override;
	virtual int						Get_Bone_Index(const char * bonename) override;
	virtual Matrix3D 	Get_Bone_Transform(const char * bonename) override;
	virtual Matrix3D 	Get_Bone_Transform(int boneindex) override;
	virtual void					Capture_Bone(int boneindex) override;
	virtual void					Release_Bone(int boneindex) override;
	virtual bool					Is_Bone_Captured(int boneindex) const override;
	virtual void					Control_Bone(int bindex,const Matrix3D & objtm,bool world_space_translation = false) override;
	virtual const Graphics::ModelHierarchy *	Get_Model_Hierarchy() const override { return Hierarchy; }

	//
	//	Simple bone evaluation methods for when the caller doesn't want
	// to update the hierarchy, but needs to know the transform of
	// a bone at a given frame.
	//
	virtual bool					Simple_Evaluate_Bone(int boneindex, Matrix3D *tm) const;
	virtual bool					Simple_Evaluate_Bone(int boneindex, float frame, Matrix3D *tm) const;

	// (gth) TESTING DYNAMICALLY SWAPPING SKELETONS!
	///Generals change so we can set sub-object transforms directly without having them revert to base pose
	///when marked dirty.  DON'T USE THIS UNLESS YOU HAVE A GOOD REASON! -MW
	void							Friend_Set_Hierarchy_Valid(bool onoff) const  	{ IsTreeValid = onoff; }

protected:

	// internally used to compute the current frame if the object is in ANIM_MODE_MANUAL
	float								Compute_Current_Frame(float *newDirection=nullptr) const;

	// Update the sub-object transforms according to the current anim state and root transform.
	virtual	void					Update_Sub_Object_Transforms() override;

	// Update the transforms using the base pose only
	void								Base_Update(const Matrix3D & root);

	// Update the transforms using a single frame of motion data
	void								Anim_Update(	const Matrix3D & root,
															Assets::AnimationAssetHandle motion,
															float frame);

	// Update the transforms blending two frames of motion data
	void								Blend_Update(	const Matrix3D & root,
															Assets::AnimationAssetHandle motion0,
															float frame0,
															Assets::AnimationAssetHandle motion1,
															float frame1,
															float percentage);


	// flag to keep track of whether the hierarchy tree transforms are currently valid
	bool								Is_Hierarchy_Valid() const				{ return IsTreeValid; }
	void								Set_Hierarchy_Valid(bool onoff) const  	{ IsTreeValid = onoff; }

	// Progress animations for single anim (loop and once)
	void								Single_Anim_Progress();

	// Release any animations
	void								Release();

protected:

	// Is the hierarchy tree currently valid
	mutable bool  					IsTreeValid;

	// Hierarchy Tree
	Graphics::ModelHierarchy *					Hierarchy;

	// Animation state for the next frame.  When we add more flexible motion
	// compositing, add a new state and its associated data to the records below
	enum {
		NONE = 0,
		BASE_POSE,
		SINGLE_ANIM,
		DOUBLE_ANIM,
	};

	int								CurMotionMode;

	// CurMotionMode == SINGLE_ANIM
    struct {
			Assets::AnimationAssetHandle Motion;
			float		  				Frame;
			int						AnimMode;
			int								LastSyncTime;
			float							animDirection;
			float							frameRateMultiplier;	// 020607 srj -- added
		} ModeAnim;

		// CurMotionMode == DOUBLE_ANIM
		struct {

			Assets::AnimationAssetHandle Motion0;
			Assets::AnimationAssetHandle Motion1;
			float		  				Frame0;
			float		  				Frame1;
			float		  				Percentage;
		} ModeInterp;


	friend class SkinClass;
};




/***********************************************************************************************
 * Animatable3DObjClass::Base_Update -- animation update function for the base pose            *
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
inline void Animatable3DObjClass::Base_Update(const Matrix3D & root)
{
	/*
	** This method simply puts the meshes in the base pose's configuration
	*/
	if (Hierarchy) {
		Hierarchy->Evaluate_Rest(Graphics::Import_Affine_Transform(root));
	}
	Set_Hierarchy_Valid(true);
}
