#include "W3DErr.h"
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

/* $Header: /Commando/Code/ww3d2/Mesh.cpp 69    1/19/02 1:01p Greg_h $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando / G 3D engine                                       *
 *                                                                                             *
 *                    File Name : MESH.cpp                                                     *
 *                                                                                             *
 *                   Programmer : Greg Hjelstrom                                               *
 *                                                                                             *
 *                   Start Date : 06/11/97                                                     *
 *                                                                                             *
 *                  Last Update : June 12, 1997 [GH]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   MeshClass::MeshClass -- Constructor for MeshClass                                         *
 *   MeshClass::MeshClass -- Copy Constructor for MeshClass                                    *
 *	  MeshClass::operator == -- assignment operator for MeshClass                               *
 *   MeshClass::~MeshClass -- destructor                                                       *
 *   MeshClass::Contains -- Determines whether mesh contains a (worldspace) point.             *
 *   MeshClass::Free -- Releases all memory/assets in use by this mesh                         *
 *   MeshClass::Clone -- Creates a clone of this mesh                                          *
 *   MeshClass::Get_Name -- returns the name of the mesh                                       *
 *   MeshClass::Set_Name -- sets the name of this mesh                                         *
 *   MeshClass::Get_W3D_Flags -- access to the W3D flags                                       *
 *   MeshClass::Get_User_Text -- access to the text buffer                                     *
 *   MeshClass::Scale -- Scales the mesh                                                       *
 *   MeshClass::Scale -- Scales the mesh                                                       *
 *   MeshClass::Load -- creates a mesh out of a mesh chunk in a .w3d file                      *
 *   MeshClass::Cast_Ray -- compute a ray intersection with this mesh                          *
 *   MeshClass::Cast_AABox -- cast an AABox against this mesh                                  *
 *   MeshClass::Cast_OBBox -- Cast an obbox against this mesh                                  *
 *   MeshClass::Intersect_AABox -- test for intersection with given AABox                      *
 *   MeshClass::Intersect_OBBox -- test for intersection with the given OBBox                  *
 *   MeshClass::Generate_Culling_Tree -- Generates a hierarchical culling tree for the mesh    *
 *   MeshClass::Direct_Load -- read the w3d file directly into this mesh object                *
 *   MeshClass::read_chunks -- read all of the chunks from the .wtm file                       *
 *	  MeshClass::read_vertices -- reads the vertex chunk                                        *
 *   MeshClass::read_texcoords -- read in the texture coordinates chunk                        *
 *   MeshClass::install_texture_coordinates -- installs the given u-v's in each channel that is*
 *   MeshClass::read_vertex_normals -- reads a surrender normal chunk from the wtm file        *
 *   MeshClass::read_v3_materials -- Reads in version 3 materials.                             *
 *   MeshClass::read_map -- Reads definition of a texture map from the file                    *
 *   MeshClass::read_triangles -- read the triangles chunk                                     *
 *   MeshClass::read_per_tri_materials -- read the material indices for each triangle          *
 *   MeshClass::read_user_text -- read in the user text chunk                                  *
 *   MeshClass::read_vertex_colors -- read in the vertex colors chunk                          *
 *   MeshClass::read_vertex_influences -- read in the vertex influences chunk                  *
 *   MeshClass::Get_Material_Info -- returns a pointer to the material info                    *
 *   MeshClass::Get_Num_Polys -- returns the number of polys (tris) in this mesh               *
 *   MeshClass::Render -- renders this mesh                                                    *
 *   MeshClass::update_skin -- deforms the mesh                                                *
 *   MeshClass::clone_materials -- clone the materials for this mesh                           *
 *   MeshClass::install_materials -- transfers the materials into the mesh                     *
 *   MeshClass::Get_Model -- user access to the mesh model                                     *
 *   MeshClass::Get_Obj_Space_Bounding_Sphere -- returns obj-space bounding sphere             *
 *   MeshClass::Get_Obj_Space_Bounding_Box -- returns the obj-space bounding box               *
 *   MeshClass::Get_Deformed_Vertices -- Gets the deformed vertices for a skin                 *
 *   MeshClass::Get_Deformed_Vertices -- Gets the deformed vertices for a skin                 *
 *   MeshClass::Replace_VertexMaterial -- Replaces existing vertex material with a new one. Wi *
 *   MeshClass::Make_Unique -- Makes mesh unique in the renderer, but still shares system ram  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "Mesh.h"
#include "GraphicsMesh.h"
#include <assert.h>
#include "W3DFile.h"
#include "AssetMgr.h"
#include "W3DErr.h"
#include "WWDebug/wwdebug.h"
import Graphics.Materials.MeshMaterial;
import Graphics.Scene.Props.Material;
import Graphics.Materials.State;
import Graphics.Scene.Models.Hierarchy;
#include "WWMath/tri.h"
#include "WWMath/aaplane.h"
#include "WWLib/chunkio.h"
#include "MeshMdl.h"
#include "MeshGeometry.h"
#include "Camera.h"
#include "Texture.h"
#include "RInfo.h"
#include "ColTest.h"
#include "IntTest.h"
#include <WWDebug/wwprofile.h>


bool MeshClass::Legacy_Meshes_Fogged = true;

/*
** This #define causes the collision code to always recompute the triangle normals rather
** than using the ones in the model.
** TODO: ensure that the models have unit normals and start re-using them again or write
** collision code to handle non-unit normals!
*/
#if (OPTIMIZE_PLANEEQ_RAM)
#define COMPUTE_NORMALS
#endif


/***********************************************************************************************
 * MeshClass::MeshClass -- Constructor for MeshClass                                           *
 *                                                                                             *
 *    Initializes an empty mesh class                                                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
MeshClass::MeshClass() :
	Model(nullptr),
	LightEnvironment(nullptr),
	BaseVertexOffset(0),
	NextVisibleSkin(nullptr),
	m_alphaOverride(1.0f),
	m_materialPassAlphaOverride(1.0f),
	m_materialPassEmissiveOverride(1.0f),
	m_muzzleFlashDesignation(Graphics::MuzzleFlashDesignation::None)
{
}


/***********************************************************************************************
 * MeshClass::MeshClass -- Copy Constructor for MeshClass                                      *
 *                                                                                             *
 *    Creates a mesh which is a copy of the given mesh                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *=============================================================================================*/
MeshClass::MeshClass(const MeshClass & that) :
	RenderObjClass(that),
	Model(nullptr),
	LightEnvironment(nullptr),
	BaseVertexOffset(that.BaseVertexOffset),
	NextVisibleSkin(nullptr),
	m_alphaOverride(1.0f),
	m_materialPassAlphaOverride(1.0f),
	m_materialPassEmissiveOverride(1.0f),
	m_muzzleFlashDesignation(that.m_muzzleFlashDesignation)
{
	REF_PTR_SET(Model,that.Model);					// mesh instances share models by default
}


/***********************************************************************************************
 * operator == -- assignment operator for MeshClass                                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
MeshClass & MeshClass::operator = (const MeshClass & that)
{
	if (this != &that) {
		Release_Graphics_Mesh_State(GraphicsMeshes);

		RenderObjClass::operator = (that);

		REF_PTR_SET(Model,that.Model);				// mesh instances share models by default
		BaseVertexOffset = that.BaseVertexOffset;
		m_muzzleFlashDesignation = that.m_muzzleFlashDesignation;

		// just dont copy the light environment
		LightEnvironment = nullptr;
	}
	return * this;
}


/***********************************************************************************************
 * MeshClass::~MeshClass -- destructor                                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
MeshClass::~MeshClass()
{
	Free();
}


/***********************************************************************************************
 * MeshClass::Contains -- Determines whether mesh contains a (worldspace) point.               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS: Assumes mesh is a closed manifold - results are undefined otherwise               *
 *           This function will NOT work for skins                                             *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   8/30/00     NH : Created.                                                                 *
 *=============================================================================================*/
bool MeshClass::Contains(const Vector3 &point)
{
	// Transform point to object space and pass on to model
	Vector3 obj_point;
	Matrix3D::Inverse_Transform_Vector(Transform, point, &obj_point);
	return Model->Contains(obj_point);
}


/***********************************************************************************************
 * MeshClass::Free -- Releases all memory/assets in use by this mesh                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Free()
{
	Release_Graphics_Mesh_State(GraphicsMeshes);
	REF_PTR_RELEASE(Model);
}


/***********************************************************************************************
 * MeshClass::Clone -- Creates a clone of this mesh                                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
RenderObjClass * MeshClass::Clone() const
{
	return NEW_REF( MeshClass, (*this));
}


/***********************************************************************************************
 * MeshClass::Get_Name -- returns the name of the mesh                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/15/98    GTH : Created.                                                                 *
 *=============================================================================================*/
const char * MeshClass::Get_Name() const
{
	return Model->Get_Name();
}


/***********************************************************************************************
 * MeshClass::Set_Name -- sets the name of this mesh                                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/9/99     GTH : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Set_Name(const char * name)
{
	Model->Set_Name(name);
}

/***********************************************************************************************
 * MeshClass::Get_W3D_Flags -- access to the W3D flags                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/15/98    GTH : Created.                                                                 *
 *=============================================================================================*/
uint32 MeshClass::Get_W3D_Flags()
{
	return Model->W3dAttributes;
}


/***********************************************************************************************
 * MeshClass::Get_User_Text -- access to the text buffer                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/15/98    GTH : Created.                                                                 *
 *=============================================================================================*/
const char * MeshClass::Get_User_Text() const
{
	return Model->Get_User_Text();
}


/***********************************************************************************************
 * MeshClass::Get_Material_Info -- returns a pointer to the material info                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/20/98    GTH : Created.                                                                 *
 *=============================================================================================*/
std::shared_ptr<Graphics::ModelMaterials<RefCountPtr<TextureClass>>> MeshClass::Get_Material_Info()
{
	if (Model) {
		if (Model->MatInfo) {
			return Model->MatInfo;
		}
	}
	return nullptr;
}


/***********************************************************************************************
 * MeshClass::Get_Model -- user access to the mesh model                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/4/99     GTH : Created.                                                                 *
 *=============================================================================================*/
MeshModelClass * MeshClass::Get_Model()
{
	if (Model != nullptr) {
		Model->Add_Ref();
	}
	return Model;
}

/***********************************************************************************************
 * MeshClass::Scale -- Scales the mesh                                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *=============================================================================================*/
void MeshClass::Scale(float scale)
{
	if (scale==1.0f) return;

	Vector3 sc;
	sc.X = sc.Y = sc.Z = scale;
	Make_Unique();
	Model->Make_Geometry_Unique();
	Model->Scale(sc);

   Invalidate_Cached_Bounding_Volumes();

   // Now update the object space bounding volumes of this object's container:
   RenderObjClass *container = Get_Container();
   if (container) container->Update_Obj_Space_Bounding_Volumes();
}


/***********************************************************************************************
 * MeshClass::Scale -- Scales the mesh                                                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *   12/8/98    GTH : Modified the box scaling to use the non-uniform parameters               *
 *=============================================================================================*/
void MeshClass::Scale(float scalex, float scaley, float scalez)
{
	// scale the surrender mesh model
	Vector3 sc;
	sc.X = scalex;
	sc.Y = scaley;
	sc.Z = scalez;
	Make_Unique();
	Model->Make_Geometry_Unique();
	Model->Scale(sc);

   Invalidate_Cached_Bounding_Volumes();

   // Now update the object space bounding volumes of this object's container:
   RenderObjClass *container = Get_Container();
   if (container) container->Update_Obj_Space_Bounding_Volumes();
}


/***********************************************************************************************
 * MeshClass::Get_Deformed_Vertices -- Gets the deformed vertices for a skin                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/4/2001   gth : Created.                                                                 *
 *=============================================================================================*/
void	MeshClass::Get_Deformed_Vertices(Vector3 *dst_vert, Vector3 *dst_norm)
{
	WWASSERT(Model->Get_Flag(MeshGeometryClass::SKIN));
	Model->get_deformed_vertices(dst_vert,dst_norm,Container->Get_Model_Hierarchy());
}


/***********************************************************************************************
 * MeshClass::Get_Deformed_Vertices -- Gets the deformed vertices for a skin                   *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/4/2001   gth : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Get_Deformed_Vertices(Vector3 *dst_vert)
{
	WWASSERT(Model->Get_Flag(MeshGeometryClass::SKIN));
	WWASSERT(Container != nullptr);
	WWASSERT(Container->Get_Model_Hierarchy() != nullptr);

	Model->get_deformed_vertices(dst_vert,Container->Get_Model_Hierarchy());
}

/***********************************************************************************************
 * MeshClass::Get_Num_Polys -- returns the number of polys (tris) in this mesh                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/6/98     GTH : Created.                                                                 *
 *=============================================================================================*/
int MeshClass::Get_Num_Polys() const
{
	if (Model) {
		int num_passes=Model->Get_Pass_Count();
		WWASSERT(num_passes>0);
		int poly_count=Model->Get_Polygon_Count();
		return num_passes*poly_count;
	} else {
		return 0;
	}
}


/***********************************************************************************************
 * MeshClass::Render -- renders this mesh                                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/10/98   GTH : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Render(RenderInfoClass & rinfo)
{
    WWPROFILE("Mesh::Render");
    if (!Is_Not_Hidden_At_All()) return;
    const unsigned sort_level=static_cast<unsigned>(Model->Get_Sort_Level());
    if (Graphics::Get_Scene_Draw_Queue().Is_Enabled() && sort_level!=SORT_LEVEL_NONE) {
        Set_Lighting_Environment(rinfo.light_environment);
        m_alphaOverride=rinfo.alphaOverride;
        m_materialPassAlphaOverride=rinfo.materialPassAlphaOverride;
        m_materialPassEmissiveOverride=rinfo.materialPassEmissiveOverride;
        Graphics::Get_Scene_Draw_Queue().Enqueue<Extract_Ordered_Draw>(sort_level, *this);
        return;
    }
    if (!Model->Get_Flag(MeshGeometryClass::SKIN)
        && CollisionMath::Overlap_Test(rinfo.Camera.Get_Frustum(),Get_Bounding_Box())==CollisionMath::OUTSIDE) return;
    if (sort_level==SORT_LEVEL_NONE) {
        Set_Lighting_Environment(rinfo.light_environment);
        m_alphaOverride=rinfo.alphaOverride;
        m_materialPassAlphaOverride=rinfo.materialPassAlphaOverride;
        m_materialPassEmissiveOverride=rinfo.materialPassEmissiveOverride;
    }
    const bool drawn=Draw_Graphics_Mesh(*this,rinfo,
        {m_alphaOverride,m_materialPassAlphaOverride,m_materialPassEmissiveOverride});
    WWASSERT(drawn);

}


void MeshClass::Replace_Texture(TextureClass* texture,TextureClass* new_texture)
{
	Model->Replace_Texture(texture,new_texture);
}


/***********************************************************************************************
 * MeshClass::Replace_VertexMaterial -- Replaces existing vertex material with a new one. Will *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/2/2001   hy : Created.                                                                  *
 *=============================================================================================*/
void MeshClass::Replace_VertexMaterial(Graphics::MeshMaterial* vmat,const std::shared_ptr<Graphics::MeshMaterial>& new_vmat)
{
	Model->Replace_VertexMaterial(vmat,new_vmat);
}


/***********************************************************************************************
 * MeshClass::Make_Unique -- Makes mesh unique in the renderer, but still shares system ram ge *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/2/2001   hy : Created.                                                                  *
 *=============================================================================================*/
void MeshClass::Make_Unique(bool force_meshmdl_clone)
{
	// Usually we will not clone the mesh model if it is already unique - force_meshmdl_clone will
	// force it to be cloned in any case. This is used in some special situations, for example if we
	// want to change this mesh and it may have already been rendered, we need to clone the mesh
	// model regardless of whether there is another mesh using it.
	if (Model->Num_Refs()==1 && !force_meshmdl_clone) return;

	MeshModelClass *newmesh=NEW_REF(MeshModelClass,(*Model));
	REF_PTR_SET(Model,newmesh);
	REF_PTR_RELEASE(newmesh);
}

/***********************************************************************************************
 * MeshClass::Load -- creates a mesh out of a mesh chunk in a .w3d file                        *
 *                                                                                             *
 * INPUT:                                                                                      *
 * 																														  *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   06/12/1997 GH  : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshClass::Load_W3D(ChunkLoadClass & cload)
{
	Vector3 boxmin,boxmax;

	/*
	** Make sure this mesh is "empty"
	*/
	Free();

	/*
	** Create empty MaterialInfo and Model
	*/
	Model = NEW_REF(MeshModelClass,());
	if (Model == nullptr) {
		WWDEBUG_SAY(("MeshClass::Load - Failed to allocate model"));
		return WW3D_ERROR_LOAD_FAILED;
	}

	/*
	** Create and read in the model...
	*/
	if (Model->Load_W3D(cload) != WW3D_ERROR_OK) {
		Free();
		return WW3D_ERROR_LOAD_FAILED;
	}

	/*
	** Pull interesting stuff out of the w3d attributes bits
	*/
	int col_bits = (Model->W3dAttributes & W3D_MESH_FLAG_COLLISION_TYPE_MASK) >> W3D_MESH_FLAG_COLLISION_TYPE_SHIFT;
	Set_Collision_Type( col_bits << 1 );
	Set_Hidden(Model->W3dAttributes & W3D_MESH_FLAG_HIDDEN);

	/*
	** Indicate whether this mesh is translucent.  The mesh is considered translucent
	** if sorting has been enabled (alpha blending on pass 0) or if pass0 contains alpha-test.
	** This flag is mainly being used by visibility preprocessing code in Renegade.
	*/
	int is_translucent = Model->Get_Flag(MeshModelClass::SORT);
	int is_alpha = 0;	//keep track of alpha pieces that require sorting (including static sort lists).
	int is_additive = 0;

	if (Model->Has_Shader_Array(0)) {
		for (int i=0; i<Model->Get_Polygon_Count(); i++) {
			Graphics::MaterialState shader = Model->Get_Shader(i,0);
			is_translucent |= (shader.Get_Alpha_Test() == Graphics::MaterialState::ALPHATEST_ENABLE);
			is_alpha |= (shader.Get_Dst_Blend_Func() != Graphics::MaterialState::DSTBLEND_ZERO ||
									shader.Get_Src_Blend_Func() != Graphics::MaterialState::SRCBLEND_ONE) && (shader.Get_Alpha_Test() != Graphics::MaterialState::ALPHATEST_ENABLE);
			is_additive |= (shader.Get_Dst_Blend_Func() == Graphics::MaterialState::DSTBLEND_ONE &&
									shader.Get_Src_Blend_Func() == Graphics::MaterialState::SRCBLEND_ONE);
		}
	} else {
		Graphics::MaterialState shader = Model->Get_Single_Shader(0);
		is_translucent |= (shader.Get_Alpha_Test() == Graphics::MaterialState::ALPHATEST_ENABLE);
		is_alpha |= (shader.Get_Dst_Blend_Func() != Graphics::MaterialState::DSTBLEND_ZERO ||
									shader.Get_Src_Blend_Func() != Graphics::MaterialState::SRCBLEND_ONE) && (shader.Get_Alpha_Test() != Graphics::MaterialState::ALPHATEST_ENABLE);
		is_additive |= (shader.Get_Dst_Blend_Func() == Graphics::MaterialState::DSTBLEND_ONE &&
									shader.Get_Src_Blend_Func() == Graphics::MaterialState::SRCBLEND_ONE);
	}
	Set_Translucent(is_translucent);
	Set_Alpha(is_alpha);
	Set_Additive(is_additive);

	return WW3D_ERROR_OK;

}


/***********************************************************************************************
 * MeshClass::Cast_Ray -- compute a ray intersection with this mesh                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/17/98    GTH : Created.                                                                 *
 *=============================================================================================*/
bool MeshClass::Cast_Ray(RayCollisionTestClass & raytest)
{
	if ((Get_Collision_Type() & raytest.CollisionType) == 0) return false;
	//Modified for 'Generals' so we could select trees but filter out headlight beams, etc. -MW
	if (raytest.CheckTranslucent && Is_Alpha()!=0)
		return false;
	if (Is_Hidden() && !raytest.CheckHidden) return false;
	if (Is_Animation_Hidden()) return false;
	if (raytest.Result->StartBad) return false;

	Matrix3D world_to_obj;
	Matrix3D world=Get_Transform();

	// if aligned or oriented rotate the mesh so that it's aligned to the ray
	if (Model->Get_Flag(MeshModelClass::ALIGNED)) {
			Vector3 mesh_position;
			world.Get_Translation(&mesh_position);
			world.Obj_Look_At(mesh_position,mesh_position - raytest.Ray.Get_Dir(),0.0f);
	} else if (Model->Get_Flag(MeshModelClass::ORIENTED)) {
			Vector3 mesh_position;
			world.Get_Translation(&mesh_position);
			world.Obj_Look_At(mesh_position,raytest.Ray.Get_P0(),0.0f);
	}

	world.Get_Inverse(world_to_obj);
	RayCollisionTestClass objray(raytest,world_to_obj);

	WWASSERT(Model);

	bool hit = Model->Cast_Ray(objray);

	// transform result back into original coordinate system
	if (hit) {
		raytest.CollidedRenderObj = this;
		Matrix3D::Rotate_Vector(world,raytest.Result->Normal, &(raytest.Result->Normal));
		if (raytest.Result->ComputeContactPoint) {
			Matrix3D::Transform_Vector(world,raytest.Result->ContactPoint, &(raytest.Result->ContactPoint));
		}
	}

	return hit;
}


/***********************************************************************************************
 * MeshClass::Cast_AABox -- cast an AABox against this mesh                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/17/98    GTH : Created.                                                                 *
 *=============================================================================================*/
bool MeshClass::Cast_AABox(AABoxCollisionTestClass & boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0) return false;
	if (boxtest.Result->StartBad) return false;

	WWASSERT(Model);

	// This function analyses the transform to call optimized functions in certain cases
	bool hit = Model->Cast_World_Space_AABox(boxtest, Get_Transform());

	if (hit) {
		boxtest.CollidedRenderObj = this;
	}

	return hit;
}


/***********************************************************************************************
 * Cast_OBBox -- Cast an obbox against this mesh                                               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/17/98    GTH : Created.                                                                 *
 *=============================================================================================*/
bool MeshClass::Cast_OBBox(OBBoxCollisionTestClass & boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0) return false;
	if (boxtest.Result->StartBad) return false;

	/*
	** transform into the local coordinate system of the mesh.
	*/
	const Matrix3D & tm = Get_Transform();
	Matrix3D world_to_obj;
	tm.Get_Orthogonal_Inverse(world_to_obj);
	OBBoxCollisionTestClass localtest(boxtest,world_to_obj);

	WWASSERT(Model);

	bool hit = Model->Cast_OBBox(localtest);

	/*
	** If we hit, transform the result of the test back to the original coordinate system.
	*/
	if (hit) {
		boxtest.CollidedRenderObj = this;
		Matrix3D::Rotate_Vector(tm,boxtest.Result->Normal, &(boxtest.Result->Normal));
		if (boxtest.Result->ComputeContactPoint) {
			Matrix3D::Transform_Vector(tm,boxtest.Result->ContactPoint, &(boxtest.Result->ContactPoint));
		}
	}

	return hit;
}


/***********************************************************************************************
 * MeshClass::Intersect_AABox -- test for intersection with given AABox                        *
 *                                                                                             *
 * The AAbox given is assumed to be in world space.  Since meshes aren't generally in world    *
 * space, the test must be transformed into our local coordinate system (which turns it into   *
 * an OBBox...)                                                                                *
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
bool MeshClass::Intersect_AABox(AABoxIntersectionTestClass & boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0) return false;

	Matrix3D inv_tm;
	Get_Transform().Get_Orthogonal_Inverse(inv_tm);
	OBBoxIntersectionTestClass local_test(boxtest,inv_tm);
	WWASSERT(Model);
	return Model->Intersect_OBBox(local_test);
}


/***********************************************************************************************
 * MeshClass::Intersect_OBBox -- test for intersection with the given OBBox                    *
 *                                                                                             *
 * The given OBBox is assumed to be in world space so we need to transform it into the mesh's  *
 * local coordinate system.                                                                    *
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
bool MeshClass::Intersect_OBBox(OBBoxIntersectionTestClass & boxtest)
{
	if ((Get_Collision_Type() & boxtest.CollisionType) == 0) return false;

	Matrix3D inv_tm;
	Get_Transform().Get_Orthogonal_Inverse(inv_tm);
	OBBoxIntersectionTestClass local_test(boxtest,inv_tm);
	WWASSERT(Model);
	return Model->Intersect_OBBox(local_test);
}


/***********************************************************************************************
 * MeshClass::Get_Obj_Space_Bounding_Sphere -- returns obj-space bounding sphere               *
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
void MeshClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	if (Model) {
		Model->Get_Bounding_Sphere(&sphere);
	} else {
		sphere.Center.Set(0,0,0);
		sphere.Radius = 1.0f;
	}
}


/***********************************************************************************************
 * MeshClass::Get_Obj_Space_Bounding_Box -- returns the obj-space bounding box                 *
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
void MeshClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	if (Model) {
		Model->Get_Bounding_Box(&box);
	} else {
		box.Init(Vector3(0,0,0),Vector3(1,1,1));
	}
}


/***********************************************************************************************
 * MeshClass::Generate_Culling_Tree -- Generates a hierarchical culling tree for the mesh      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/18/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Generate_Culling_Tree()
{
	Model->Generate_Culling_Tree();
}


/***********************************************************************************************
 * MeshClass::Add_Dependencies_To_List -- Add dependent files to the list.                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/18/99    PDS : Created.                                                                 *
 *=============================================================================================*/
void MeshClass::Add_Dependencies_To_List
(
	DynamicVectorClass<StringClass> &file_list,
	bool										textures_only
)
{
	//
	// Get a pointer to this mesh's material information object
	//
	auto material = Get_Material_Info ();
	if (material != nullptr) {

		//
		// Loop through all the textures and add their filenames to our list
		//
		for (int index = 0; index < static_cast<int>(material->textures.size()); index ++) {

			//
			//	Add this texture's filename to the list
			//
			TextureClass *texture = material->textures[index].Peek();
			if (texture != nullptr) {
				file_list.Add (texture->Get_Full_Path ());
			}
		}

		//
		// Release our hold on the material information object
		//
		material.reset();
	}

	RenderObjClass::Add_Dependencies_To_List (file_list, textures_only);
}


/***********************************************************************************************
 * MeshClass::Update_Cached_Bounding_Volumes -- default collision sphere.                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   5/14/2001    NH : Created.                                                                *
 *=============================================================================================*/
void MeshClass::Update_Cached_Bounding_Volumes() const
{
	Get_Obj_Space_Bounding_Sphere(CachedBoundingSphere);

#ifdef ALLOW_TEMPORARIES
	CachedBoundingSphere.Center = Get_Transform() * CachedBoundingSphere.Center;
#else
	Get_Transform().mulVector3(CachedBoundingSphere.Center);
#endif

	// If we are camera-aligned or -oriented, we don't know which way we are facing at this point,
	// so the box we return needs to contain the sphere. Otherwise do the normal computation.
	if (Model->Get_Flag(MeshModelClass::ALIGNED) || Model->Get_Flag(MeshModelClass::ORIENTED)) {
		CachedBoundingBox.Center = CachedBoundingSphere.Center;
		CachedBoundingBox.Extent.Set(CachedBoundingSphere.Radius, CachedBoundingSphere.Radius, CachedBoundingSphere.Radius);
	} else {
		Get_Obj_Space_Bounding_Box(CachedBoundingBox);
		CachedBoundingBox.Transform(Get_Transform());
	}

	Validate_Cached_Bounding_Volumes();
}


// This utility function recurses throughout the subobjects of a renderobject, and for each
// MeshClass it finds it sets the given MeshModel flag on its model. This is useful for stuff
// like making a RenderObjects' polys sort.
void Set_MeshModel_Flag(RenderObjClass *robj, int flag, int onoff)
{
	if (robj->Class_ID() == RenderObjClass::CLASSID_MESH) {
		// Set flag on model (the assumption is that meshes don't have subobjects)
		MeshClass *mesh = (MeshClass *)robj;
		MeshModelClass *model = mesh->Get_Model();
		model->Set_Flag((MeshModelClass::FlagsType)flag, onoff != 0);
		model->Release_Ref();
	} else {
		// Recurse to subobjects (if any)
		int num_obj = robj->Get_Num_Sub_Objects();
		RenderObjClass *sub_obj;
		for (int i = 0; i < num_obj; i++) {
			sub_obj = robj->Get_Sub_Object(i);
			if (sub_obj) {
				Set_MeshModel_Flag(sub_obj, flag, onoff);
				sub_obj->Release_Ref();
			}
		}
	}
}

int MeshClass::Get_Sort_Level() const
{
	if (Model) {
		return (Model->Get_Sort_Level());
	}
	return(SORT_LEVEL_NONE);
}

void MeshClass::Set_Sort_Level(int level)
{
	if (Model) {
		Model->Set_Sort_Level(level);
	}
}

int MeshClass::Get_Draw_Call_Count() const
{
	if (Model != nullptr) {
		// Report the material texture count for the model.
		if (Model->MatInfo && !Model->MatInfo->textures.empty()) {
			return static_cast<int>(Model->MatInfo->textures.size());
		}

		// Otherwise, return 1
		return 1;

	} else {
		return 0;
	}
}

Graphics::ModelFactory<RenderObjClass>* Load_Mesh_Factory(ChunkLoadClass& cload)
{
    MeshClass* mesh=NEW_REF(MeshClass,());
    if(!mesh)return nullptr;
    const std::shared_ptr<RenderObjClass> source(mesh,[](RenderObjClass* object) { object->Release_Ref(); });
    if(mesh->Load_W3D(cload)!=WW3D_ERROR_OK)return nullptr;
    return new Graphics::ModelFactory<RenderObjClass>(mesh->Get_Name(),mesh->Class_ID(),[source] {
        return static_cast<RenderObjClass*>(SET_REF_OWNER(source->Clone()));
    });
}
