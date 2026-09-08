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
 *                     $Archive:: /Commando/Code/ww3d2/MeshMdl.cpp                            $*
 *                                                                                             *
 *                    Org Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/26/02 4:04p                                             $*
 *                                                                                             *
 *                    $Revision:: 48                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "MeshMdl.h"
#include "GraphicsMesh.h"
#include "Texture.h"
#include "WWMath/vp.h"
#include "Camera.h"
#include "WW3D.h"


/*
**
** MeshModelClass Implementation
**
*/


MeshModelClass::MeshModelClass() :
	CurMatDesc(&DefMatDesc),
	MatInfo(nullptr)
{
	Set_Flag(DIRTY_BOUNDS,true);

	MatInfo = std::make_shared<Graphics::ModelMaterials<RefCountPtr<TextureClass>>>();
}

MeshModelClass::MeshModelClass(const MeshModelClass & that) :
	MeshGeometryClass(that),
	DefMatDesc(that.DefMatDesc),
	CurMatDesc(&DefMatDesc),
	MatInfo(nullptr)
{
	if (that.AlternateMatDesc) {
		AlternateMatDesc = std::make_unique<MaterialDescription>(*that.AlternateMatDesc);
	}

	clone_materials(that);
}

MeshModelClass::~MeshModelClass()
{

	Reset(0,0,0);
	MatInfo.reset();

}

MeshModelClass & MeshModelClass::operator = (const MeshModelClass & that)
{
	if (this != &that) {
        Release_Graphics_Mesh_State(GraphicsMeshes);

		MeshGeometryClass::operator = (that);

		DefMatDesc = that.DefMatDesc;
		CurMatDesc = &DefMatDesc;

		AlternateMatDesc.reset();
		if (that.AlternateMatDesc) {
			AlternateMatDesc = std::make_unique<MaterialDescription>(*that.AlternateMatDesc);
		}

		clone_materials(that);
	}
	return * this;
}

void MeshModelClass::Reset(int polycount,int vertcount,int passcount)
{
    Release_Graphics_Mesh_State(GraphicsMeshes);
	Reset_Geometry(polycount,vertcount);

	// Release everything we have and reset to initial state


	MatInfo->Reset();
	DefMatDesc.Reset(polycount,vertcount,passcount);

	AlternateMatDesc.reset();

	CurMatDesc = &DefMatDesc;
}


void MeshModelClass::Replace_Texture(TextureClass* texture,TextureClass* new_texture)
{
	WWASSERT(texture);
	WWASSERT(new_texture);
	for (int stage=0;stage<MaterialDescription::MAX_TEX_STAGES;++stage) {
		for (int pass=0;pass<Get_Pass_Count();++pass) {
			if (Has_Texture_Array(pass,stage)) {
				for (int i=0;i<Get_Polygon_Count();++i) {
					if (Peek_Texture(i,pass,stage)==texture) {
						Set_Texture(i,new_texture,pass,stage);
					}
				}
			}
			else {
				if (Peek_Single_Texture(pass,stage)==texture) {
					Set_Single_Texture(new_texture,pass,stage);
				}
			}
		}
	}
}

void MeshModelClass::Replace_VertexMaterial(Graphics::MeshMaterial* vmat,const std::shared_ptr<Graphics::MeshMaterial>& new_vmat)
{
	WWASSERT(vmat);
	WWASSERT(new_vmat);

	for (int pass=0;pass<Get_Pass_Count();++pass) {
		if (Has_Material_Array(pass)) {
			for (int i=0;i<Get_Vertex_Count();++i) {
				if (Peek_Material(i,pass)==vmat) {
					Set_Material(i,new_vmat,pass);
				}
			}
		}
		else {
			if (Peek_Single_Material(pass)==vmat) {
				Set_Single_Material(new_vmat,pass);
			}
		}
	}
}


void MeshModelClass::Make_Geometry_Unique()
{
    GeometryRevision.Invalidate();
	WWASSERT(Vertex);

	ShareBufferClass<Vector3> * unique_verts = NEW_REF(ShareBufferClass<Vector3>,(*Vertex));
	REF_PTR_SET(Vertex,unique_verts);
	REF_PTR_RELEASE(unique_verts);

	ShareBufferClass<Vector3> * norms = NEW_REF(ShareBufferClass<Vector3>,(*VertexNorm));
	REF_PTR_SET(VertexNorm,norms);
	REF_PTR_RELEASE(norms);

#if (!OPTIMIZE_PLANEEQ_RAM)
	ShareBufferClass<Vector4> * peq = NEW_REF(ShareBufferClass<Vector4>,(*PlaneEq, "MeshModelClass::PlaneEq"));
	REF_PTR_SET(PlaneEq,peq);
	REF_PTR_RELEASE(peq);
#endif
}

void MeshModelClass::Make_UV_Array_Unique(int pass,int stage)
{
	CurMatDesc->Make_UV_Array_Unique(pass,stage);
}

void MeshModelClass::Make_Color_Array_Unique(int array_index)
{
	CurMatDesc->Make_Color_Array_Unique(array_index);
}

void MeshModelClass::Enable_Alternate_Material_Description(bool onoff)
{
	if ((onoff == true) && AlternateMatDesc) {
		if (CurMatDesc != AlternateMatDesc.get()) {
			CurMatDesc = AlternateMatDesc.get();

			if (Get_Flag(SORT) && WW3D::Is_Munge_Sort_On_Load_Enabled())
				compute_static_sort_levels();

			if (WW3D::Is_Overbright_Modify_On_Load_Enabled())
				modify_for_overbright();

			// TODO: Invalidate just this meshes DX9 data!!!
		}
	} else {
		if (CurMatDesc != &DefMatDesc) {
			CurMatDesc = &DefMatDesc;

			if (Get_Flag(SORT) && WW3D::Is_Munge_Sort_On_Load_Enabled())
				compute_static_sort_levels();

			if (WW3D::Is_Overbright_Modify_On_Load_Enabled())
				modify_for_overbright();

		}
	}
}

bool MeshModelClass::Is_Alternate_Material_Description_Enabled()
{
	return AlternateMatDesc && CurMatDesc == AlternateMatDesc.get();
}
bool MeshModelClass::Needs_Vertex_Normals()
{
	if (Get_Flag(MeshModelClass::PRELIT_MASK) == 0) {
		return true;
	}
	return CurMatDesc->Do_Mappers_Need_Normals();
}
