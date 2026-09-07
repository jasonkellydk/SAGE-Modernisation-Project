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
 *                     $Archive:: /Commando/Code/ww3d2/Collect.cpp                            $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 1/08/01 10:04a                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   CollectionClass::CollectionClass -- default constructor for collection render object      *
 *   CollectionClass::CollectionClass -- constructor for collection render object              *
 *   CollectionClass::CollectionClass -- copy constructor                                      *
 *   CollectionClass::CollectionClass -- assignment operator                                   *
 *   CollectionClass::~CollectionClass -- destructor                                           *
 *   CollectionClass::Clone -- virtual copy constructor                                        *
 *   CollectionClass::Free -- releases all assets in use by this collection                    *
 *   CollectionClass::Class_ID -- returns class id for collection render objects               *
 *   CollectionClass::Get_Num_Polys -- returns the number of polygons in this collection       *
 *   CollectionClass::Render -- render this collection                                         *
 *   CollectionClass::Set_Transform -- set the transform for this collection                   *
 *   CollectionClass::Set_Position -- set the position for this collection                     *
 *   CollectionClass::Get_Num_Sub_Objects -- returns the number of sub objects                 *
 *   CollectionClass::Get_Sub_Object -- returns a pointer to the desired sub object            *
 *   CollectionClass::Add_Sub_Object -- adds another object into this collection               *
 *   CollectionClass::Remove_Sub_Object -- removes a sub object from this collection           *
 *   CollectionClass::Cast_Ray -- passes the ray test to each sub object                       *
 *   CollectionClass::Cast_AABox -- passes the axis-aligned box test to each sub object        *
 *   CollectionClass::Cast_OBBox -- passes the oriented box test to each sub object            *
 *   CollectionClass::Intersect_AABox -- test for intersection with an AABox                   *
 *   CollectionClass::Intersect_OBBox -- test for intersection with an OBBox                   *
 *   CollectionClass::Get_Obj_Space_Bounding_Sphere -- returns the object space bounding spher *
 *   CollectionClass::Get_Obj_Space_Bounding_Box -- returns the object-space bounding box      *
 *   CollectionClass::Snap_Point_Count -- returns the number of snap points in this collecion  *
 *   CollectionClass::Get_Snap_Point -- return the desired snap point                          *
 *   CollectionClass::Scale -- scale the objects in this collection                            *
 *   CollectionClass::Scale -- scale the objects in this collection                            *
 *   CollectionClass::Update_Obj_Space_Bounding_Volumes -- recomputes the object space boundin *
 *   CollectionClass::Update_Sub_Object_Transforms -- recomputes all sub object transforms     *
 *   CollectionLoaderClass::Load -- reads a collection from a w3d file                         *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "Collect.h"
import Assets.Adapters.W3D.Collection;
#include "WWLib/chunkio.h"
#include "Camera.h"
#include "WWDebug/wwdebug.h"
#include "AssetMgr.h"
#include "WW3D.h"
#include "W3DErr.h"
//#include "sr.hpp"







/***********************************************************************************************
 * CollectionClass::CollectionClass -- default constructor for collection render object        *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   23/8/00    GTH : Created.                                                                 *
 *=============================================================================================*/
CollectionClass::CollectionClass() :
	SnapPoints()
{
	Update_Obj_Space_Bounding_Volumes();
}


/***********************************************************************************************
 * CollectionClass::CollectionClass -- constructor for collection render object                *
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
CollectionClass::CollectionClass(const Assets::ModelCollectionDesc & def) :
	SubObjects(static_cast<int>(def.children.size())),
	SnapPoints()
{
	// Set our name
	Set_Name (def.name.c_str());

	// create the sub objects
	SubObjects.Resize(static_cast<int>(def.children.size()));
	for (int i=0; i<static_cast<int>(def.children.size()); i++) {
		WWASSERT(SubObjects.Count() == i);
		SubObjects.Add(WW3DAssetManager::Get_Instance()->Create_Render_Obj(def.children[i].c_str()));
		SubObjects[i]->Set_Container(this);
	}

	// Copy the list of placeholder objects from the definition
	ProxyList = def.proxies;

	// grab ahold of the snap points.
	SnapPoints = def.snap_points;


	// set up our collision typeas the union of all of our sub-objects
	Update_Sub_Object_Bits();

	// update the object bounding volumes
	Update_Obj_Space_Bounding_Volumes();
}


/***********************************************************************************************
 * CollectionClass::CollectionClass -- copy constructor                                        *
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
CollectionClass::CollectionClass(const CollectionClass & src) :
	CompositeRenderObjClass(src),
	SubObjects(src.SubObjects.Count()),
	SnapPoints()
{
	*this = src;
}


/***********************************************************************************************
 * CollectionClass::CollectionClass -- assignment operator                                     *
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
CollectionClass & CollectionClass::operator = (const CollectionClass & that)
{
	if (this != &that) {
		Free();
		CompositeRenderObjClass::operator = (that);

		SubObjects.Resize(that.SubObjects.Count());
		for (int i=0; i<that.SubObjects.Count(); i++) {
			WWASSERT(SubObjects.Count() == i);
			SubObjects.Add(that.SubObjects[i]->Clone());
			SubObjects[i]->Set_Container(this);
		}

		// Copy the list of placeholder objects from the definition
		ProxyList = that.ProxyList;

		SnapPoints = that.SnapPoints;


		Update_Sub_Object_Bits();
		Update_Obj_Space_Bounding_Volumes();
	}
	return * this;
}


/***********************************************************************************************
 * CollectionClass::~CollectionClass -- destructor                                             *
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
CollectionClass::~CollectionClass()
{
	Free();
}


/***********************************************************************************************
 * CollectionClass::Clone -- virtual copy constructor                                          *
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
RenderObjClass * CollectionClass::Clone() const
{
	return NEW_REF( CollectionClass, (*this));
}


/***********************************************************************************************
 * CollectionClass::Free -- releases all assets in use by this collection                      *
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
void CollectionClass::Free()
{
	for (int i=0; i<SubObjects.Count(); i++) {
		SubObjects[i]->Set_Container(nullptr);
		SubObjects[i]->Release_Ref();
		SubObjects[i] = nullptr;
	}
	SubObjects.Delete_All();
	ProxyList.clear();

	SnapPoints.clear();
}


/***********************************************************************************************
 * CollectionClass::Class_ID -- returns class id for collection render objects                 *
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
int CollectionClass::Class_ID()	const
{
	return RenderObjClass::CLASSID_COLLECTION;
}


/***********************************************************************************************
 * CollectionClass::Get_Num_Polys -- returns the number of polygons in this collection         *
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
int CollectionClass::Get_Num_Polys() const
{
	int pcount = 0;
	for (int i=0; i<SubObjects.Count(); i++) {
		pcount += SubObjects[i]->Get_Num_Polys();
	}
	return pcount;
}


/***********************************************************************************************
 * CollectionClass::Render -- render this collection                                           *
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
void CollectionClass::Render(RenderInfoClass & rinfo)
{
	if (Is_Not_Hidden_At_All() == false) {
		return;
	}

	if (Are_Sub_Object_Transforms_Dirty()) {
		Update_Sub_Object_Transforms();
	}

	for (int i=0; i<SubObjects.Count(); i++) {
		SubObjects[i]->Render(rinfo);
	}
}


/***********************************************************************************************
 * CollectionClass::Set_Transform -- set the transform for this collection                     *
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
void CollectionClass::Set_Transform(const Matrix3D &m)
{
	RenderObjClass::Set_Transform(m);
	Set_Sub_Object_Transforms_Dirty(true);
}


/***********************************************************************************************
 * CollectionClass::Set_Position -- set the position for this collection                       *
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
void CollectionClass::Set_Position(const Vector3 &v)
{
	RenderObjClass::Set_Position(v);
	Set_Sub_Object_Transforms_Dirty(true);
}


/***********************************************************************************************
 * CollectionClass::Get_Num_Sub_Objects -- returns the number of sub objects                   *
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
int CollectionClass::Get_Num_Sub_Objects() const
{
	return SubObjects.Count();
}


/***********************************************************************************************
 * CollectionClass::Get_Sub_Object -- returns a pointer to the desired sub object              *
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
RenderObjClass * CollectionClass::Get_Sub_Object(int index) const
{
	if (SubObjects[index]) {
		SubObjects[index]->Add_Ref();
	}
	return SubObjects[index];
}


/***********************************************************************************************
 * CollectionClass::Add_Sub_Object -- adds another object into this collection                 *
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
int CollectionClass::Add_Sub_Object(RenderObjClass * subobj)
{
	WWASSERT(subobj);
	subobj->Add_Ref();
	subobj->Set_Container(this);
	subobj->Set_Transform(Transform);
	int res = SubObjects.Add(subobj);
	Update_Sub_Object_Bits();
	Update_Obj_Space_Bounding_Volumes();
	if (Is_In_Scene()) {
		subobj->Notify_Added(Scene);
	}
	return res;
}


/***********************************************************************************************
 * CollectionClass::Remove_Sub_Object -- removes a sub object from this collection             *
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
int CollectionClass::Remove_Sub_Object(RenderObjClass * robj)
{
	if (robj == nullptr) return 0;

	int res = 0;

	Matrix3D tm = Get_Transform();

	for (int i=0; i<SubObjects.Count(); i++) {
		if (robj == SubObjects[i]) {

			if (Is_In_Scene()) {
				SubObjects[i]->Notify_Removed(Scene);
			}
			SubObjects[i]->Set_Container(nullptr);
			SubObjects[i]->Set_Transform(tm);
			SubObjects[i]->Release_Ref();
			res = SubObjects.Delete(i);
			break;
		}
	}

	if (res != 0) {
		Update_Sub_Object_Bits();
		Update_Obj_Space_Bounding_Volumes();
	}

	return res;
}


/***********************************************************************************************
 * CollectionClass::Cast_Ray -- passes the ray test to each sub object                         *
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
bool CollectionClass::Cast_Ray(RayCollisionTestClass & raytest)
{
	bool res = false;
	for (int i=0; i<SubObjects.Count(); i++) {
		res |= SubObjects[i]->Cast_Ray(raytest);
	}
	return res;
}


/***********************************************************************************************
 * CollectionClass::Cast_AABox -- passes the axis-aligned box test to each sub object          *
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
bool CollectionClass::Cast_AABox(AABoxCollisionTestClass & boxtest)
{
	bool res = false;
	for (int i=0; i<SubObjects.Count(); i++) {
		res |= SubObjects[i]->Cast_AABox(boxtest);
	}
	return res;
}


/***********************************************************************************************
 * CollectionClass::Cast_OBBox -- passes the oriented box test to each sub object              *
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
bool CollectionClass::Cast_OBBox(OBBoxCollisionTestClass & boxtest)
{
	bool res = false;
	for (int i=0; i<SubObjects.Count(); i++) {
		res |= SubObjects[i]->Cast_OBBox(boxtest);
	}
	return res;
}


/***********************************************************************************************
 * CollectionClass::Intersect_AABox -- test for intersection with an AABox                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/19/00    gth : Created.                                                                 *
 *=============================================================================================*/
bool CollectionClass::Intersect_AABox(AABoxIntersectionTestClass & boxtest)
{
	bool res = false;
	for (int i=0; i<SubObjects.Count(); i++) {
		res |= SubObjects[i]->Intersect_AABox(boxtest);
	}
	return res;
}


/***********************************************************************************************
 * CollectionClass::Intersect_OBBox -- test for intersection with an OBBox                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/19/00    gth : Created.                                                                 *
 *=============================================================================================*/
bool CollectionClass::Intersect_OBBox(OBBoxIntersectionTestClass & boxtest)
{
	bool res = false;
	for (int i=0; i<SubObjects.Count(); i++) {
		res |= SubObjects[i]->Intersect_OBBox(boxtest);
	}
	return res;
}

/***********************************************************************************************
 * CollectionClass::Get_Obj_Space_Bounding_Sphere -- returns the object space bounding sphere. *
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
void CollectionClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	sphere = BoundSphere;
}


/***********************************************************************************************
 * CollectionClass::Get_Obj_Space_Bounding_Box -- returns the object-space bounding box        *
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
void CollectionClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	box = BoundBox;
}


/***********************************************************************************************
 * CollectionClass::Snap_Point_Count -- returns the number of snap points in this collecion    *
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
int CollectionClass::Snap_Point_Count()
{
	if (!SnapPoints.empty()) {
		return static_cast<int>(SnapPoints.size());
	} else {
		return 0;
	}
}


/***********************************************************************************************
 * CollectionClass::Get_Snap_Point -- return the desired snap point                            *
 *                                                                                             *
 * This function will set the passed vector to be equal to the object space coordinates of     *
 * the desired snap point.                                                                     *
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
void CollectionClass::Get_Snap_Point(int index,Vector3 * set)
{
	WWASSERT(set != nullptr);
	if (index >= 0 && static_cast<std::size_t>(index) < SnapPoints.size()) {
		const auto& point = SnapPoints[index];
		set->Set(point.x, point.y, point.z);
	} else {
		set->X = set->Y = set->Z = 0;
	}
}


/***********************************************************************************************
 * CollectionClass::Scale -- scale the objects in this collection                              *
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
void CollectionClass::Scale(float scale)
{
	for (int i=0; i<SubObjects.Count(); i++) {
		SubObjects[i]->Scale(scale);
	}
}


/***********************************************************************************************
 * CollectionClass::Scale -- scale the objects in this collection                              *
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
void CollectionClass::Scale(float scalex, float scaley, float scalez)
{
	for (int i=0; i<SubObjects.Count(); i++) {
		SubObjects[i]->Scale(scalex,scaley,scalez);
	}
}


/***********************************************************************************************
 * CollectionClass::Update_Obj_Space_Bounding_Volumes -- recomputes the object space bounding  *
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
void CollectionClass::Update_Obj_Space_Bounding_Volumes()
{
	int i;
	if (SubObjects.Count() <= 0) {
		BoundSphere = SphereClass();
		BoundBox.Center.Set(0,0,0);
		BoundBox.Extent.Set(0,0,0);
		return;
	}

	Matrix3D tm = Get_Transform();
	Set_Transform(Matrix3D(true));

	// loop through all sub-objects, combining their bounding spheres.
	BoundSphere = SubObjects[0]->Get_Bounding_Sphere();
	for (i=1; i < SubObjects.Count(); i++) {
		BoundSphere.Add_Sphere(SubObjects[i]->Get_Bounding_Sphere());
	}

	// loop through the sub-objects, computing a box in the root coordinate
	// system which bounds all of the meshes.  Note that we've set the
	// root coordinate system to identity for this.
	MinMaxAABoxClass box(Vector3(FLT_MAX,FLT_MAX,FLT_MAX),Vector3(-FLT_MAX,-FLT_MAX,-FLT_MAX));

	for (i=0; i < SubObjects.Count(); i++) {
		box.Add_Box(SubObjects[i]->Get_Bounding_Box());
	}

	BoundBox.Init(box);

   Invalidate_Cached_Bounding_Volumes();

   // Now update the object space bounding volumes of this object's container:
   RenderObjClass *container = Get_Container();
   if (container) container->Update_Obj_Space_Bounding_Volumes();

	Set_Transform(tm);
}


/***********************************************************************************************
 * CollectionClass::Update_Sub_Object_Transforms -- recomputes all sub object transforms       *
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
void CollectionClass::Update_Sub_Object_Transforms()
{
	RenderObjClass::Update_Sub_Object_Transforms();
	for (int i=0; i<SubObjects.Count(); i++) {
		SubObjects[i]->Set_Transform(Transform);
		SubObjects[i]->Update_Sub_Object_Transforms();
	}
	Set_Sub_Object_Transforms_Dirty(false);
}


/***********************************************************************************************
 * CollectionClass::Get_Placeholder -- Returns information about a placeholder object.
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/28/99    PDS : Created.                                                                 *
 *=============================================================================================*/
bool CollectionClass::Get_Proxy (int index, ProxyClass &proxy) const
{
	bool retval = false;

	if (index >= 0 && index < static_cast<int>(ProxyList.size())) {

		//
		// Return the proxy information to the caller
		//
		const auto& source = ProxyList[index];
        const auto& m = source.transform;
        proxy = ProxyClass(source.name.c_str(), Matrix3D(
            m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7], m[8],m[9],m[10],m[11]));
		retval	= true;
	}

	return retval;
}


/***********************************************************************************************
 * CollectionClass::Get_Proxy_Count -- Returns the count of proxy objects in the collection.
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/28/99    PDS : Created.                                                                 *
 *=============================================================================================*/
int CollectionClass::Get_Proxy_Count () const
{
	return static_cast<int>(ProxyList.size());
}


/***********************************************************************************************
 * CollectionLoaderClass::Load -- reads a collection from a w3d file                           *
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
Graphics::ModelFactory<RenderObjClass> * Load_Collection_Factory(ChunkLoadClass & cload)
{
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if(cload.Read(bytes.data(),static_cast<unsigned>(bytes.size()))!=bytes.size())return nullptr;
    Assets::ModelCollectionDesc description;
    std::string error;
    if(!Assets::W3D::W3DRead_Model_Collection(bytes,description,error))return nullptr;
    auto data=std::make_shared<const Assets::ModelCollectionDesc>(std::move(description));
    return new Graphics::ModelFactory<RenderObjClass>(data->name,RenderObjClass::CLASSID_COLLECTION,[data] { return NEW_REF(CollectionClass,(*data)); });
}
