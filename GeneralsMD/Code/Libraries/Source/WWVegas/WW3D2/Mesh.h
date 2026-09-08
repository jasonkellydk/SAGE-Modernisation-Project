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

/* $Header: /Commando/Code/ww3d2/Mesh.h 16    11/07/01 5:50p Jani_p $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando / G 3D engine                                       *
 *                                                                                             *
 *                    File Name : MESH.h                                                       *
 *                                                                                             *
 *                   Programmer : Greg Hjelstrom                                               *
 *                                                                                             *
 *                   Start Date : 06/11/97                                                     *
 *                                                                                             *
 *                  Last Update : June 11, 1997 [GH]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
import Graphics.Scene.Models.Factory;
import Graphics.Scene.MuzzleFlash;


#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "WWLib/always.h"
#include "WW3D2/RendObj.h"
#include "WWLib/bittype.h"
#include "WW3D2/W3DErr.h"
import Graphics.Scene.Lighting.Local;
import Graphics.Scene.Models.Materials;

class HModelClass;
class AuxMeshDataClass;
class MeshLoadInfoClass;
class W3DMeshClass;
class ChunkLoadClass;
class ChunkSaveClass;
class RenderInfoClass;
class MeshModelClass;
class GraphicsMeshState;
struct W3dTexCoordStruct;
class TextureClass;
import Graphics.Materials.MeshMaterial;

/**
** MeshClass -- Render3DObject for rendering meshes.
*/
class MeshClass : public RenderObjClass
{
	W3DMPO_CODE(MeshClass)
public:

	MeshClass();
	MeshClass(const MeshClass & src);
	MeshClass & operator = (const MeshClass &);
	virtual ~MeshClass() override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface
	/////////////////////////////////////////////////////////////////////////////
	virtual RenderObjClass *	Clone() const override;
	virtual int						Class_ID() const override { return CLASSID_MESH; }
	virtual const char *			Get_Name() const override;
	virtual void					Set_Name(const char * name) override;
	virtual int						Get_Num_Polys() const override;
	virtual void					Render(RenderInfoClass & rinfo) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Collision Detection
	/////////////////////////////////////////////////////////////////////////////
	virtual bool					Cast_Ray(RayCollisionTestClass & raytest) override;
	virtual bool					Cast_AABox(AABoxCollisionTestClass & boxtest) override;
	virtual bool					Cast_OBBox(OBBoxCollisionTestClass & boxtest) override;
	virtual bool					Intersect_AABox(AABoxIntersectionTestClass & boxtest) override;
	virtual bool					Intersect_OBBox(OBBoxIntersectionTestClass & boxtest) override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Bounding Volumes
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const override;
   virtual void					Get_Obj_Space_Bounding_Box(AABoxClass & box) const override;

	/////////////////////////////////////////////////////////////////////////////
	// Render Object Interface - Attributes, Options, Properties, etc
	/////////////////////////////////////////////////////////////////////////////
	virtual void					Scale(float scale) override;
	virtual void					Scale(float scalex, float scaley, float scalez) override;
	virtual std::shared_ptr<Graphics::ModelMaterials<RefCountPtr<TextureClass>>> Get_Material_Info() override;

   virtual int						Get_Sort_Level() const override;
   virtual void					Set_Sort_Level(int level) override;


	/////////////////////////////////////////////////////////////////////////////
	// MeshClass Interface
	/////////////////////////////////////////////////////////////////////////////
	WW3DErrorType					Load_W3D(ChunkLoadClass & cload);
	void								Generate_Culling_Tree();
	MeshModelClass *				Get_Model();
	MeshModelClass *				Peek_Model();
	uint32							Get_W3D_Flags();
	const char *					Get_User_Text() const;
	int								Get_Draw_Call_Count() const;

	bool								Contains(const Vector3 &point);

	void								Get_Deformed_Vertices(Vector3 *dst_vert, Vector3 *dst_norm);
	void								Get_Deformed_Vertices(Vector3 *dst_vert);

	void								Set_Lighting_Environment(Graphics::LocalLighting * light_env) { if (light_env) {m_localLightEnv=*light_env;LightEnvironment = &m_localLightEnv;} else {LightEnvironment = nullptr;} }
	Graphics::LocalLighting *		Get_Lighting_Environment() { return LightEnvironment; }
	void Set_Muzzle_Flash_Designation(Graphics::MuzzleFlashDesignation designation) { m_muzzleFlashDesignation=designation; }
	Graphics::MuzzleFlashDesignation Get_Muzzle_Flash_Designation() const { return m_muzzleFlashDesignation; }
	float	Get_Alpha_Override() { return m_alphaOverride;}
	GraphicsMeshState*& Graphics_Mesh_State() noexcept { return GraphicsMeshes; }

	void								Set_Next_Visible_Skin(MeshClass * next_visible) { NextVisibleSkin = next_visible; }
	MeshClass *						Peek_Next_Visible_Skin() { return NextVisibleSkin; }

	void								Set_Base_Vertex_Offset(int base) { BaseVertexOffset = base; }
	int								Get_Base_Vertex_Offset() { return BaseVertexOffset; }

	// Do old .w3d mesh files get fog turned on or off?
	static bool						Legacy_Meshes_Fogged;

	void								Replace_Texture(TextureClass* texture,TextureClass* new_texture);
	void								Replace_VertexMaterial(Graphics::MeshMaterial* vmat,const std::shared_ptr<Graphics::MeshMaterial>& new_vmat);

	void								Make_Unique(bool force_meshmdl_clone = false);


protected:

	virtual void					Add_Dependencies_To_List (DynamicVectorClass<StringClass> &file_list, bool textures_only = false) override;

	virtual void					Update_Cached_Bounding_Volumes() const override;

	void								Free();

	void								install_materials(MeshLoadInfoClass * loadinfo);
	void								clone_materials(const MeshClass & srcmesh);

	MeshModelClass *				Model;
	GraphicsMeshState* GraphicsMeshes = nullptr;

	Graphics::LocalLighting *		LightEnvironment;		// cached pointer to the light environment for this mesh
	Graphics::LocalLighting     m_localLightEnv;	//added for 'Generals'
	float					m_alphaOverride;	//added for 'Generals' to allow variable alpha on meshes.
	float					m_materialPassEmissiveOverride;	//added for 'Generals' to allow variable emissive on additional passes.
	float					m_materialPassAlphaOverride;	//added for 'Generals' to allow variable alpha on additional render passes.
	Graphics::MuzzleFlashDesignation m_muzzleFlashDesignation;
	int								BaseVertexOffset;		// offset to our first vertex in whatever vb this mesh is in.
	MeshClass *						NextVisibleSkin;		// linked list of visible skins


};

inline MeshModelClass * MeshClass::Peek_Model()
{
	return Model;
}


// This utility function recurses throughout the subobjects of a renderobject,
// and for each MeshClass it finds it sets the given MeshModel flag on its
// model. This is useful for stuff like making a RenderObjects' polys sort.
//void Set_MeshModel_Flag(RenderObjClass *robj, MeshModelClass::FlagsType flag, int onoff);
void Set_MeshModel_Flag(RenderObjClass *robj, int flag, int onoff);

Graphics::ModelFactory<RenderObjClass>* Load_Mesh_Factory(ChunkLoadClass& cload);
