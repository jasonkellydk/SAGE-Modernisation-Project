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
 *                     $Archive:: /Commando/Code/ww3d2/MeshMdlIO.cpp                          $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 1/18/02 3:09p                                               $*
 *                                                                                             *
 *                    $Revision:: 27                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   MeshModelClass::Load_W3D -- Load a mesh from a W3D file                                   *
 *   MeshModelClass::read_chunks -- read all of the chunks for a mesh                          *
 *   MeshModelClass::read_vertices -- reads the vertex chunk                                   *
 *   MeshModelClass::read_texcoords -- read in the texture coordinates chunk                   *
 *   MeshModelClass::read_vertex_normals -- reads a vertex normal chunk from the w3d file      *
 *   MeshModelClass::read_v3_materials -- Reads in version 3 materials.                        *
 *   MeshModelClass::read_triangles -- read the triangles chunk                                *
 *   MeshModelClass::read_per_tri_materials -- read the material indices for each triangle     *
 *   MeshModelClass::read_user_text -- read in the user text chunk                             *
 *   MeshModelClass::read_vertex_colors -- read in the vertex colors chunk                     *
 *   MeshModelClass::read_vertex_influences -- read in the vertex influences chunk             *
 *   MeshModelClass::read_vertex_shade_indices -- read the shade index chunk                   *
 *   MeshModelClass::read_material_info -- read the material info chunk                        *
 *   MeshModelClass::read_shaders -- read the shaders chunk                                    *
 *   MeshModelClass::read_vertex_materials -- read the vertex materials chunk                  *
 *   MeshModelClass::read_textures -- read the textures chunk                                  *
 *   MeshModelClass::read_material_pass -- read a material pass chunk                          *
 *	  MeshModelClass::read_prelit_material -- read prelit material chunks.							  *
 *   MeshModelClass::read_aabtree -- loads the aabtree chunk                                   *
 *   MeshLoadContextClass::MeshLoadContextClass -- constructor for MeshLoadContextClass        *
 *   MeshLoadContextClass::~MeshLoadContextClass -- destructor                                 *
 *   MeshLoadContextClass::Get_Texcoord_Array -- returns the texture coordinates array         *
 *   MeshLoadContextClass::Add_Shader -- adds a shader to the array                            *
 *   MeshLoadContextClass::Add_Vertex_Materail -- adds a vertex material                       *
 *   MeshLoadContextClass::Add_Texture -- adds a texture                                       *
 *   MeshLoadContextClass::Add_Legacy_Material -- adds a legacy material                       *
 *   MeshLoadContextClass::Peek_Legacy_Shader -- returns a legacy shader                       *
 *   MeshLoadContextClass::Peek_Legacy_Vertex_Material -- returns a pointer to a legacy vmat   *
 *   MeshLoadContextClass::Peek_Legacy_Texture -- returns a pointer to a texture               *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <cstddef>
#include <vector>
#include "MeshMdl.h"
#include "MatInfo.h"
#include "VertMaterial.h"
#include "Shader.h"
#include "Texture.h"
#include "WW3D.h"
#include "WWLib/chunkio.h"
#include "W3DErr.h"
#include "W3DFile.h"
#include "AssetMgr.h"
#include "WWLib/simplevec.h"
#include "WWLib/realcrc.h"
#include "WW3D2/StringUtilities.h"

import Assets.Adapters.W3D.PassBindings;
import Assets.Adapters.W3D.Materials;
import Assets.Math;
import Assets.Adapters.W3D.Geometry;

#ifdef _UNIX
#include "osdep/osdep.h"
#endif

/**
** MeshLoadContextClass
** This class is just used as a temporary scratchpad while a mesh is being
** loaded.  In some cases, a chunk will be encountered before I have a place
** to plug it into the mesh model, etc.  It is also used to convert from the
** old material format to the new one (detecting duplicated materials in the
** process).
**
** This object will hold refs to all of the unique material objects.  These
** objects will later be transferred into the MaterialInfo for the mesh and it
** will own the refs for the mesh.  The load context object is destroyed once
** loading is complete...
*/
class MeshLoadContextClass
{
	W3DMPO_CODE(MeshLoadContextClass)
private:
	MeshLoadContextClass();
	~MeshLoadContextClass();

	W3dTexCoordStruct *		Get_Texcoord_Array();

	int							Add_Shader(ShaderClass shader);
	int							Add_Vertex_Material(VertexMaterialClass * vmat);
	int							Add_Texture(TextureClass* tex);

	ShaderClass					Peek_Shader(int index)											{ return Shaders[index]; }
	VertexMaterialClass *	Peek_Vertex_Material(int index)								{ return VertexMaterials[index]; }
	TextureClass *				Peek_Texture(int index)											{ return Textures[index]; }

	int							Shader_Count()												{ return Shaders.Count(); }
	int							Vertex_Material_Count()									{ return VertexMaterials.Count(); }
	int							Texture_Count()												{ return Textures.Count(); }

	/*
	** Legacy material support.
	*/
	void							Add_Legacy_Material(ShaderClass shader,VertexMaterialClass * vmat,TextureClass * tex);
	ShaderClass					Peek_Legacy_Shader(int legacy_material_index);
	VertexMaterialClass *	Peek_Legacy_Vertex_Material(int legacy_material_index);
	TextureClass *				Peek_Legacy_Texture(int legacy_material_index);

	/*
	** Redundant UV detection support.  The context provides a temporary buffer to
	** load the uv coordinates into.
	*/
	Vector2 *					Get_Temporary_UV_Array(int elementcount);


	/*
	** Currently, the only tool that creates DIG chunks is the lightmap tool.  Since DX9 support
	** for linking the emissive material color to an array seems poor, We're just going to multiply
	** the DIG array into the DCG array (or make it the DCG array).  Now, to properly support the
	** "alternate material set", we have to know whether we've already encountered a DIG chunk so
	** these flags provide that functionality.
	*/
	void							Notify_Loaded_DIG_Chunk(bool onoff = true)				{ LoadedDIG = onoff; }
	bool							Already_Loaded_DIG()										{ return LoadedDIG; }

private:

	struct LegacyMaterialClass
	{
		LegacyMaterialClass() : VertexMaterialIdx(0),ShaderIdx(0),TextureIdx(0)	{ }
		~LegacyMaterialClass()	{ }
		void		Set_Name(const char * name) { Name=name; }

		StringClass Name;
		int		VertexMaterialIdx;
		int		ShaderIdx;
		int		TextureIdx;
	};


	Assets::W3D::W3DMeshHeader Header;
	W3dTexCoordStruct *		TexCoords;
	W3dMaterialInfoStruct	MatInfo;

	uint32						PrelitChunkID;

	int							CurPass;
	int							CurTexStage;

	DynamicVectorClass < LegacyMaterialClass * >		LegacyMaterials;
	DynamicVectorClass < ShaderClass >					Shaders;
	DynamicVectorClass < VertexMaterialClass * >		VertexMaterials;
	DynamicVectorClass < unsigned long >				VertexMaterialCrcs;
	DynamicVectorClass < TextureClass * >				Textures;

	/*
	** Alternate material data.  Any alternate material data will be loaded into
	** this MeshMatDescClass object.  When loading is finished, an alternate MeshMatDescClass
	** will be allocated in the mesh model.  This MeshMatDescClass will be initialized to be
	** identical to the default MeshMatDescClass and then any data contained in this
	** MeshMatDescClass will replace the relevant arrays.
	*/
	MeshMatDescClass											AlternateMatDesc;

	SimpleVecClass<Vector2>									TempUVArray;

	/*
	** Record when we load the DIG chunk
	*/
	bool															LoadedDIG;

	friend class MeshClass;
	friend class MeshModelClass;
};


/***********************************************************************************************
 * MeshModelClass::Load_W3D -- Load a mesh from a W3D file                                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::Load_W3D(ChunkLoadClass & cload)
{
    if(!cload.Open_Chunk())return WW3D_ERROR_LOAD_FAILED;
    if(cload.Cur_Chunk_ID()!=W3D_CHUNK_MESH_HEADER3) {
        cload.Close_Chunk();return WW3D_ERROR_LOAD_FAILED;
    }
    std::vector<std::byte> header_bytes(cload.Cur_Chunk_Length());
    const bool complete=cload.Read(header_bytes.data(),static_cast<unsigned>(header_bytes.size()))==header_bytes.size();
    cload.Close_Chunk();
    Assets::W3D::W3DMeshHeader header;
    if(!complete || !Assets::W3D::W3DRead_Mesh_Header(header_bytes,header))return WW3D_ERROR_LOAD_FAILED;
    auto* context=W3DNEW MeshLoadContextClass;
    const auto destroy=[](MeshLoadContextClass* value) { delete value; };
    std::unique_ptr<MeshLoadContextClass,decltype(destroy)> owner(context,destroy);
    context->Header=std::move(header);
	/*
	** Process the header
	*/
    Reset(context->Header.triangle_count,context->Header.vertex_count,1);
    W3dAttributes=context->Header.attributes;
    SortLevel=context->Header.sort_level;
    const std::string model_name=context->Header.container_name.empty()?context->Header.name:context->Header.container_name+"."+context->Header.name;
    Set_Name(model_name.c_str());

	context->AlternateMatDesc.Set_Vertex_Count(VertexCount);
	context->AlternateMatDesc.Set_Polygon_Count(PolyCount);

	/*
	** Set Bounding Info
	*/
	BoundBoxMin.Set(context->Header.bounds.minimum.x,context->Header.bounds.minimum.y,context->Header.bounds.minimum.z);
	BoundBoxMax.Set(context->Header.bounds.maximum.x,context->Header.bounds.maximum.y,context->Header.bounds.maximum.z);

	BoundSphereCenter.Set(context->Header.sphere_center.x,context->Header.sphere_center.y,context->Header.sphere_center.z);
	BoundSphereRadius = context->Header.sphere_radius;

	/*
	** Flags
	*/
	if (context->Header.version >= W3D_MAKE_VERSION(4,1)) {
		int geometry_type = context->Header.attributes & W3D_MESH_FLAG_GEOMETRY_TYPE_MASK;
		switch (geometry_type)
		{
			case W3D_MESH_FLAG_GEOMETRY_TYPE_NORMAL:
				break;
			case W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ALIGNED:
				Set_Flag(ALIGNED,true);
				break;
			case W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ORIENTED:
				Set_Flag(ORIENTED,true);
				break;
			case W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN:
				Set_Flag(SKIN,true);
				Set_Flag(ALLOW_NPATCHES,true);
				break;
		}
	}

	if (context->Header.attributes & W3D_MESH_FLAG_TWO_SIDED) {
		Set_Flag(TWO_SIDED,true);
	}

	if (context->Header.attributes & W3D_MESH_FLAG_CAST_SHADOW) {
		Set_Flag(CAST_SHADOW,true);
	}

	if (context->Header.attributes & W3D_MESH_FLAG_NPATCHABLE) {
		Set_Flag(ALLOW_NPATCHES,true);
	}

	// Configure the load sequence for prelighting.
	if (context->Header.attributes & W3D_MESH_FLAG_PRELIT_MASK) {

		// Select from the available prelit materials based on current prelit lighting mode.
		// If the model does not have the current prelit mode, select the next highest quality
		// prelit material that is available.
		switch (WW3D::Get_Prelit_Mode()) {

			case WW3D::PRELIT_MODE_LIGHTMAP_MULTI_TEXTURE:
				if (context->Header.attributes & W3D_MESH_FLAG_PRELIT_LIGHTMAP_MULTI_TEXTURE) {
					context->PrelitChunkID = W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_TEXTURE;
					Set_Flag (PRELIT_LIGHTMAP_MULTI_TEXTURE, true);
					break;
				}
				FALLTHROUGH; // Else fall thru...

			case WW3D::PRELIT_MODE_LIGHTMAP_MULTI_PASS:
				if (context->Header.attributes & W3D_MESH_FLAG_PRELIT_LIGHTMAP_MULTI_PASS) {
					context->PrelitChunkID = W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_PASS;
					Set_Flag (PRELIT_LIGHTMAP_MULTI_PASS, true);
					break;
				}
				FALLTHROUGH; // Else fall thru...

			case WW3D::PRELIT_MODE_VERTEX:
				if (context->Header.attributes & W3D_MESH_FLAG_PRELIT_VERTEX) {
					context->PrelitChunkID = W3D_CHUNK_PRELIT_VERTEX;
					Set_Flag (PRELIT_VERTEX, true);
					break;
				}
				FALLTHROUGH; // Else fall thru...

			default:

				// This prelighting option MUST be available if none of the others are available.
				WWASSERT (context->Header.attributes & W3D_MESH_FLAG_PRELIT_UNLIT);
				context->PrelitChunkID = W3D_CHUNK_PRELIT_UNLIT;
				break;
		}

	} else {

		// For backwards compatibility, test for obsolete lightmap flag.
		if (context->Header.attributes & OBSOLETE_W3D_MESH_FLAG_LIGHTMAPPED) {
			Set_Flag (PRELIT_LIGHTMAP_MULTI_PASS, true);
		}

		// Else this mesh has no prelighting.
	}

	read_chunks(cload,context);

	/*
	** If this is a pre-3.0 mesh and it has vertex influences,
	** fixup the bone indices to account for the new root node
	*/
	if ((context->Header.version < W3D_MAKE_VERSION(3,0)) && (Get_Flag(SKIN))) {

		uint16 * links = get_bone_links();
		WWASSERT(links);

		for (int bi = 0; bi < Get_Vertex_Count(); bi++) {
			links[bi] += 1;
		}
	}

	/*
	** If this mesh is collideable and no AABTree was in the file, generate one now
	*/
	if (	(((W3dAttributes & W3D_MESH_FLAG_COLLISION_TYPE_MASK) >> W3D_MESH_FLAG_COLLISION_TYPE_SHIFT) != 0) &&
			(CullTree == nullptr))
	{
		Generate_Culling_Tree();
	}

	/*
	** Transfer the materials into the MatInfo
	*/
	install_materials(context);

	/*
	** Delete the temporary LoadInfo object
	*/
	owner.reset();

	/*
	** Post-process the model: optimize passes, activate fog etc.
	*/
	post_process();

	return WW3D_ERROR_OK;

}


/***********************************************************************************************
 * MeshModelClass::read_chunks -- read all of the chunks for a mesh                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_chunks(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	/*
	**	Read in the chunk header
	** If there are no more chunks within the mesh chunk,
	** we are done.
	*/
	while (cload.Open_Chunk()) {

		/*
		** Process the chunk
		*/
		WW3DErrorType error = WW3D_ERROR_OK;

		switch (cload.Cur_Chunk_ID()) {

			case W3D_CHUNK_VERTICES:
					// call up to MeshGeometryClass
					error = read_vertices(cload);
					break;

			case W3D_CHUNK_SURRENDER_NORMALS:
			case W3D_CHUNK_VERTEX_NORMALS:
					// call up to MeshGeometryClass
					error = read_vertex_normals(cload);
					break;

			case W3D_CHUNK_TEXCOORDS:
					error = read_texcoords(cload,context);
					break;

			case O_W3D_CHUNK_MATERIALS:
			case O_W3D_CHUNK_MATERIALS2:
					WWDEBUG_SAY(( "Obsolete material chunk encountered in mesh: %s.%s", context->Header.container_name.c_str(),context->Header.name.c_str()));
					WWASSERT(0);
					break;

			case W3D_CHUNK_MATERIALS3:
					WWDEBUG_SAY(( "Obsolete material chunk encountered in mesh: %s.%s", context->Header.container_name.c_str(),context->Header.name.c_str()));
					error = read_v3_materials(cload,context);
					break;

			case O_W3D_CHUNK_SURRENDER_TRIANGLES:
					WWASSERT_PRINT( 0, "Obsolete Triangle Chunk Encountered!" );
					break;

			case W3D_CHUNK_TRIANGLES:
					// call up to MeshGeometryClass
					error = read_triangles(cload);
					break;

			case W3D_CHUNK_PER_TRI_MATERIALS:
					error = read_per_tri_materials(cload,context);
					break;

			case W3D_CHUNK_MESH_USER_TEXT:
					// call up to MeshGeometryClass
					error = read_user_text(cload);
					break;

			case W3D_CHUNK_VERTEX_COLORS:
					error = read_vertex_colors(cload,context);
					break;

			case W3D_CHUNK_VERTEX_INFLUENCES:
					// call up to MeshGeometryClass
					error = read_vertex_influences(cload);
					break;

			case W3D_CHUNK_VERTEX_SHADE_INDICES:
					// call up to MeshGeometryClass
					error = read_vertex_shade_indices(cload);
					break;

			case W3D_CHUNK_MATERIAL_INFO:
					error = read_material_info(cload,context);
					break;

			case W3D_CHUNK_SHADERS:
					error = read_shaders(cload,context);
					break;

			case W3D_CHUNK_VERTEX_MATERIALS:
					error = read_vertex_materials(cload,context);
					break;

			case W3D_CHUNK_TEXTURES:
					error = read_textures(cload,context);
					break;

			case W3D_CHUNK_MATERIAL_PASS:
					error = read_material_pass(cload,context);
					break;

			case W3D_CHUNK_DEFORM:
					WWDEBUG_SAY(("Obsolete deform chunk encountered in mesh: %s.%s", context->Header.container_name.c_str(),context->Header.name.c_str()));
					break;

			case W3D_CHUNK_DAMAGE:
					WWDEBUG_SAY(("Obsolete damage chunk encountered in mesh: %s.%s", context->Header.container_name.c_str(),context->Header.name.c_str()));
					break;

			case W3D_CHUNK_PRELIT_UNLIT:
			case W3D_CHUNK_PRELIT_VERTEX:
			case W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_PASS:
			case W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_TEXTURE:
					read_prelit_material (cload, context);
					break;

			case W3D_CHUNK_AABTREE:
					// call up to MeshGeometryClass
					read_aabtree(cload);
					break;

			default:
					break;

		}

		cload.Close_Chunk();

		if (error != WW3D_ERROR_OK) {
			return error;
		}
	}

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_texcoords -- read in the texture coordinates chunk                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/6/98     GTH : Created.                                                                 *
 *   2/16/99    GTH : Moved into MeshModel                                                     *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_texcoords(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	W3dTexCoordStruct texcoord;
	Vector2 * uvarray = nullptr;
	int elementcount = cload.Cur_Chunk_Length() / sizeof (W3dTexCoordStruct);

	uvarray = context->Get_Temporary_UV_Array(elementcount);

	if (uvarray != nullptr) {
		/*
		** Read the uv's into the first u-v pass array
		** NOTE: this is an obsolete function.  Texture coordinates are now
		** loaded in the pass chunks
		*/
		for (int i=0; i<VertexCount; i++) {
			if (cload.Read(&(texcoord),sizeof(W3dTexCoordStruct)) != sizeof(W3dTexCoordStruct)) {
				return WW3D_ERROR_LOAD_FAILED;
			}
			uvarray[i].Set(texcoord.U,1.0f - texcoord.V);
		}

		DefMatDesc->Install_UV_Array(context->CurPass,context->CurTexStage,uvarray,elementcount);
	}

	return WW3D_ERROR_OK;
}

/***********************************************************************************************
 * MeshModelClass::read_v3_materials -- Reads in version 3 materials.                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/2/98     GTH : Created.                                                                 *
 *   2/16/99    GTH : Moved into MeshModelClass                                                *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_v3_materials(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	for (unsigned int mi=0; mi<context->Header.material_count; mi++) {

		/*
		** First, we expect a W3D_CHUNK_MATERIAL3 to wrap the entire material
		*/
		if (!cload.Open_Chunk()) goto Error;
		if (cload.Cur_Chunk_ID() != W3D_CHUNK_MATERIAL3) goto Error;

		/*
		** Inside the MATERIAL3 will be the following:
		**
		** W3D_MATERIAL3_NAME - name of the material
		** W3D_MATERIAL3_INFO - equivalent to 1.40 vertex material parameters
		** W3D_MATERIAL3_DC_MAP - diffuse color mapping
		**   W3D_MAP3_FILENAME - filename of the texture map
		**   W3D_MAP3_INFO - animation, etc information
		** W3D_MATERIAL3_DI_MAP - diffuse illumination map
		** W3D_MATERIAL3_SC_MAP - specular color map
		** W3D_MATERIAL3_SI_MAP - specular illumination map
		*/
		VertexMaterialClass *		vmat = nullptr;
		ShaderClass						shader;
		TextureClass *					tex = nullptr;
		char								name[256];

		/*
		** Read the material name
		*/
		if (!cload.Open_Chunk()) goto Error;
		if (cload.Cur_Chunk_ID() != W3D_CHUNK_MATERIAL3_NAME) goto Error;
		cload.Read(name,cload.Cur_Chunk_Length());
		if (!cload.Close_Chunk()) goto Error;

		/*
		** Read the vertex material parameters
		*/
		if (!cload.Open_Chunk()) goto Error;

			W3dMaterial3Struct mat;
			if (cload.Cur_Chunk_ID() != W3D_CHUNK_MATERIAL3_INFO) goto Error;
			if (cload.Read(&mat,sizeof(W3dMaterial3Struct)) != sizeof(W3dMaterial3Struct)) goto Error;
			vmat = W3DNEW VertexMaterialClass;
			vmat->Init_From_Material3(mat);
			vmat->Set_Name(name);
			shader.Init_From_Material3(mat);

			/*
			** If this shader does alpha blending, the mesh must be sorted.
			*/
			if (shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO) {
				Set_Flag(MeshModelClass::SORT,true);
			}

		if (!cload.Close_Chunk()) goto Error;

		/*
		** Look for the DC map and read it in
		*/
		while (cload.Open_Chunk()) {
			if (cload.Cur_Chunk_ID() == W3D_CHUNK_MATERIAL3_DC_MAP) {

				/*
				** Read in the texture filename
				*/
				char filename[512];
				if (!cload.Open_Chunk()) goto Error;
					if (cload.Cur_Chunk_ID() != W3D_CHUNK_MAP3_FILENAME) goto Error;
					if (cload.Cur_Chunk_Length() >= sizeof(filename)) goto Error;
					cload.Read(filename,cload.Cur_Chunk_Length());
				if (!cload.Close_Chunk()) goto Error;

				/*
				** Read in the auxiliary map info
				*/
				W3dMap3Struct mapinfo;
				if (!cload.Open_Chunk()) goto Error;
					if (cload.Cur_Chunk_ID() != W3D_CHUNK_MAP3_INFO) goto Error;
					if (cload.Read(&mapinfo,sizeof(W3dMap3Struct)) != sizeof(W3dMap3Struct)) goto Error;
				if (!cload.Close_Chunk()) goto Error;

				if ( mapinfo.FrameCount > 1 ) {
					WWDEBUG_SAY(("ERROR: Obsolete Animated Texture detected in model: %s",context->Header.name.c_str()));
				}

				tex = WW3DAssetManager::Get_Instance()->Get_Texture(filename);

				shader.Set_Texturing(ShaderClass::TEXTURING_ENABLE);

			} else if (cload.Cur_Chunk_ID() == W3D_CHUNK_MATERIAL3_SI_MAP) {
				Vector3	diffuse_color;
				vmat->Get_Diffuse( &diffuse_color);
				if ( diffuse_color == Vector3( 0,0,0 ) ) {

					/*
					** Read in the texture filename
					*/
					char filename[512];
					if (!cload.Open_Chunk()) goto Error;
						if (cload.Cur_Chunk_ID() != W3D_CHUNK_MAP3_FILENAME) goto Error;
						if (cload.Cur_Chunk_Length() >= sizeof(filename)) goto Error;
						cload.Read(filename,cload.Cur_Chunk_Length());
					if (!cload.Close_Chunk()) goto Error;

					if (tex) tex->Release_Ref();

					/*
					** Read in the auxiliary map info
					*/
					W3dMap3Struct mapinfo;
					if (!cload.Open_Chunk()) goto Error;
						if (cload.Cur_Chunk_ID() != W3D_CHUNK_MAP3_INFO) goto Error;
						if (cload.Read(&mapinfo,sizeof(W3dMap3Struct)) != sizeof(W3dMap3Struct)) goto Error;
					if (!cload.Close_Chunk()) goto Error;

					if ( mapinfo.FrameCount > 1 ) {
						WWDEBUG_SAY(("ERROR: Obsolete Animated Texture detected in model: %s",context->Header.name.c_str()));
					}

					tex = WW3DAssetManager::Get_Instance()->Get_Texture(filename);

					shader.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
					shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
					shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
					shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
				}
			}

			cload.Close_Chunk();
		}

		// If not texturing, move the diffuse color to ambient (simulating old behavior)
		if ( shader.Get_Texturing() == ShaderClass::TEXTURING_DISABLE ) {
			Vector3 color;
			vmat->Get_Diffuse( &color );
			vmat->Set_Ambient( color );
			vmat->Set_Diffuse( Vector3( 0, 0, 0 ) );
		}

		context->Add_Legacy_Material(shader,vmat,tex);

		vmat->Release_Ref();
		if (tex) tex->Release_Ref();
		vmat = nullptr;
		tex = nullptr;

		/*
		** Close the W3D_CHUNK_MATERIAL3
		*/
		cload.Close_Chunk();
	}

	/*
	** Install the default materials to use in the absence of an array
	*/
	if (context->Vertex_Material_Count() >= 1) {
		Set_Single_Material(context->Peek_Vertex_Material(0),0);
	}

	if (context->Texture_Count() >= 1) {
		Set_Single_Texture(context->Peek_Texture(0),0);
	}

	if (context->Shader_Count() >= 1) {
		Set_Single_Shader(context->Peek_Shader(0),0);
	}

	return WW3D_ERROR_OK;

Error:

	return WW3D_ERROR_LOAD_FAILED;

}


/***********************************************************************************************
 * MeshModelClass::read_per_tri_materials -- read the material indices for each triangle       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/7/98     GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_per_tri_materials(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	if (context->Header.material_count == 1) return WW3D_ERROR_OK;

	TriIndex * polys = get_polys();

	bool multi_mtl = (context->Vertex_Material_Count() > 1);
	bool multi_tex = (context->Texture_Count() > 1);
	bool multi_shad = (context->Shader_Count() > 1);

	if (!multi_mtl) {
		Set_Single_Material(context->Peek_Legacy_Vertex_Material(0));
	}
	if (!multi_tex) {
		Set_Single_Texture(context->Peek_Legacy_Texture(0));
	}
	if (!multi_shad) {
		Set_Single_Shader(context->Peek_Legacy_Shader(0));
	}

	/*
	** Read in each polygon's material id and assign pointer to the
	** shader, texture, and vertex material as needed.
	*/
	for (int i=0; i<Get_Polygon_Count(); i++) {

		// read in the mat id for this poly
		uint16 matid;

		if (cload.Read(&matid,sizeof(uint16)) != sizeof(uint16)) {
			return WW3D_ERROR_LOAD_FAILED;
		}

		if (multi_shad) {
			Set_Shader(i,context->Peek_Legacy_Shader(matid));
		}
		if (multi_tex) {
			Set_Texture(i,context->Peek_Legacy_Texture(matid));
		}
		if (multi_mtl) {
			Set_Material(polys[i].I,context->Peek_Legacy_Vertex_Material(matid));
			Set_Material(polys[i].J,context->Peek_Legacy_Vertex_Material(matid));
			Set_Material(polys[i].K,context->Peek_Legacy_Vertex_Material(matid));
		}
	}

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_vertex_colors -- read in the vertex colors chunk                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/28/1997 GH  : Created.                                                                 *
 *   2/16/99    GTH : Moved into MeshModelClass                                                *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_vertex_colors(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	/*
	** The W3D file format supports arbitrary vertex color arrays for each pass; however since
	** our conversion to hardware T&L, we only support two unique color arrays.  So here is
	** what is happening in this function:
	**
	** 1 - If this is the first diffuse color array we've encountered, load the values
	** 2 - Always set the DCG source for this pass to the array (vertex materials will be fixed later)
	**
	** A side effect is that if two DCG chunks are encountered, only the first is used...
	*/
	if (CurMatDesc->Has_Color_Array(0) == false) {
		W3dRGBStruct color;
		unsigned * dcg = Get_Color_Array(0,true);
		assert(dcg != nullptr);

		for (int i=0; i<Get_Vertex_Count(); i++) {

			if (cload.Read(&color,sizeof(W3dRGBStruct)) != sizeof(W3dRGBStruct)) {
				return WW3D_ERROR_LOAD_FAILED;
			}

			Vector4 col;
			col.Set((float)color.R / 255.0f,(float)color.G / 255.0f,(float)color.B / 255.0f, 1.0f);
			dcg[i]=Assets::Color_To_ARGB({col.X,col.Y,col.Z,col.W});
		}
	}
	CurMatDesc->Set_DCG_Source(context->CurPass,VertexMaterialClass::COLOR1);

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_material_info -- read the material info chunk                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_material_info(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	if (cload.Read(&(context->MatInfo),sizeof(W3dMaterialInfoStruct)) != sizeof(W3dMaterialInfoStruct)) {
		return WW3D_ERROR_LOAD_FAILED;
	}
	Set_Pass_Count(context->MatInfo.PassCount);
	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_shaders -- read the shaders chunk                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_shaders(ChunkLoadClass &cload, MeshLoadContextClass *context)
{
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if (cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) != bytes.size())
        return WW3D_ERROR_LOAD_FAILED;
    if (bytes.size() % 16 != 0 || bytes.size() / 16 != context->MatInfo.ShaderCount)
        return WW3D_ERROR_LOAD_FAILED;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
        ShaderClass shader;
        if (!shader.Load_W3D_Record(std::span<const std::byte>(bytes).subspan(offset, 16)))
            return WW3D_ERROR_LOAD_FAILED;
        context->Add_Shader(shader);
    }
    return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_vertex_materials -- read the vertex materials chunk                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_vertex_materials(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	while (cload.Open_Chunk()) {
		WWASSERT(cload.Cur_Chunk_ID() == W3D_CHUNK_VERTEX_MATERIAL);
		VertexMaterialClass * vmat = NEW_REF(VertexMaterialClass,());
		WW3DErrorType error = vmat->Load_W3D(cload);
		if (error != WW3D_ERROR_OK) {
			return error;
		}
		context->Add_Vertex_Material(vmat);
		vmat->Release_Ref();

		cload.Close_Chunk();
	}
	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_textures -- read the textures chunk                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *   3/05/99	 PDS : Broke the guts of this function into a util function in Texture.cpp		  *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_textures(ChunkLoadClass & cload,MeshLoadContextClass * context)
{
	// Keep reading textures until there are no more...
	for (TextureClass *newtex = ::Load_Texture (cload);
		  newtex != nullptr;
		  newtex = ::Load_Texture (cload)) {

		// Add this texture to our context and release our local hold on it
		context->Add_Texture(newtex);
		newtex->Release_Ref();
	}

	return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_material_pass -- read a material pass chunk                            *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/16/99    GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_material_pass(ChunkLoadClass &cload, MeshLoadContextClass *context)
{
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if (cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) != bytes.size())
        return WW3D_ERROR_LOAD_FAILED;
    Assets::W3D::W3DPassBindings bindings;
    if (!Assets::W3D::W3DRead_Pass_Bindings(bytes, Get_Vertex_Count(), Get_Polygon_Count(), bindings))
        return WW3D_ERROR_LOAD_FAILED;

    const int pass = context->CurPass;
    if (pass < 0 || pass >= MeshMatDescClass::MAX_PASSES
        || bindings.stages.size() > MeshMatDescClass::MAX_TEX_STAGES)
        return WW3D_ERROR_LOAD_FAILED;
    // Validate references before changing either material set.
    for (const auto id : bindings.vertex_material_ids)
        if (id >= static_cast<unsigned>(context->Vertex_Material_Count())) return WW3D_ERROR_LOAD_FAILED;
    for (const auto id : bindings.shader_ids)
        if (id >= static_cast<unsigned>(context->Shader_Count())) return WW3D_ERROR_LOAD_FAILED;
    for (const auto &stage : bindings.stages)
        for (const auto id : stage.texture_ids)
            if (id != 0xffffffffu && id >= static_cast<unsigned>(context->Texture_Count()))
                return WW3D_ERROR_LOAD_FAILED;

    if (!bindings.vertex_material_ids.empty()) {
        auto *materials = DefMatDesc->Has_Material_Data(pass) ? &context->AlternateMatDesc : DefMatDesc;
        const auto &ids = bindings.vertex_material_ids;
        if (ids.size() == 1) materials->Set_Single_Material(context->Peek_Vertex_Material(ids[0]), pass);
        else for (std::size_t vertex = 0; vertex < ids.size(); ++vertex)
            materials->Set_Material(static_cast<int>(vertex), context->Peek_Vertex_Material(ids[vertex]), pass);
    }
    if (!bindings.shader_ids.empty()) {
        auto *materials = DefMatDesc->Has_Shader_Data(pass) ? &context->AlternateMatDesc : DefMatDesc;
        const auto &ids = bindings.shader_ids;
        for (std::size_t face = 0; face < ids.size(); ++face) {
            const auto shader = context->Peek_Shader(ids[face]);
            if (ids.size() == 1) materials->Set_Single_Shader(shader, pass);
            else materials->Set_Shader(static_cast<int>(face), shader, pass);
            if (pass == 0 && shader.Get_Dst_Blend_Func() != ShaderClass::DSTBLEND_ZERO
                && shader.Get_Alpha_Test() == ShaderClass::ALPHATEST_DISABLE && SortLevel == SORT_LEVEL_NONE)
                Set_Flag(SORT, true);
        }
    }

    // Prelit illumination and authored alpha may share the same color array.
    // Applying their decoded arrays in file order preserves alternate sets and
    // the existing multiplication/alpha replacement semantics.
    for (const auto source : bindings.color_order) {
        using Assets::W3D::W3DPassColorSource;
        if (source == W3DPassColorSource::Diffuse) {
            auto *materials = DefMatDesc->Get_DCG_Source(pass) != VertexMaterialClass::MATERIAL
                ? &context->AlternateMatDesc : DefMatDesc;
            const bool replace_rgb = !materials->Has_Color_Array(0);
            if (replace_rgb || context->PrelitChunkID == W3D_CHUNK_PRELIT_VERTEX) {
                auto *colors = materials->Get_Color_Array(0);
                for (std::size_t vertex = 0; vertex < bindings.diffuse_colors.size(); ++vertex) {
                    const auto &input = bindings.diffuse_colors[vertex];
                    auto value = replace_rgb ? Assets::Color4f{input.r, input.g, input.b, input.a}
                        : Assets::Color_From_ARGB(colors[vertex]);
                    value.a = input.a;
                    colors[vertex] = Assets::Color_To_ARGB(value);
                }
            }
            materials->Set_DCG_Source(pass, VertexMaterialClass::COLOR1);
        } else if (source == W3DPassColorSource::Illumination) {
            auto *materials = context->Already_Loaded_DIG() ? &context->AlternateMatDesc : DefMatDesc;
            context->Notify_Loaded_DIG_Chunk(true);
            const bool multiply = materials->Has_Color_Array(0);
            auto *colors = materials->Get_Color_Array(0);
            for (std::size_t vertex = 0; vertex < bindings.diffuse_illumination.size(); ++vertex) {
                const auto &input = bindings.diffuse_illumination[vertex];
                auto value = multiply ? Assets::Color_From_ARGB(colors[vertex]) : Assets::Color4f{};
                value.r *= input.r;
                value.g *= input.g;
                value.b *= input.b;
                colors[vertex] = Assets::Color_To_ARGB(value);
            }
            materials->Set_DCG_Source(pass, VertexMaterialClass::COLOR1);
        }
        // Specular arrays remain in the decoded asset. The existing material
        // description has no per-pass specular array binding.
    }

    for (std::size_t stage_index = 0; stage_index < bindings.stages.size(); ++stage_index) {
        const int stage = static_cast<int>(stage_index);
        const auto &input = bindings.stages[stage_index];
        if (!input.texture_ids.empty()) {
            auto *materials = DefMatDesc->Has_Texture_Data(pass,stage) ? &context->AlternateMatDesc : DefMatDesc;
            if (input.texture_ids.size() == 1) {
                const auto id = input.texture_ids[0];
                materials->Set_Single_Texture(id == 0xffffffffu ? nullptr : context->Peek_Texture(id), pass, stage);
            } else for (std::size_t face = 0; face < input.texture_ids.size(); ++face) {
                const auto id = input.texture_ids[face];
                if (id != 0xffffffffu)
                    materials->Set_Texture(static_cast<int>(face), context->Peek_Texture(id), pass, stage);
            }
        }
        if (!input.texcoords.empty()) {
            auto *materials = DefMatDesc->Has_UV(pass,stage) ? &context->AlternateMatDesc : DefMatDesc;
            const int count = static_cast<int>(input.texcoords.size());
            auto *uvs = context->Get_Temporary_UV_Array(count);
            for (int vertex = 0; vertex < count; ++vertex)
                uvs[vertex].Set(input.texcoords[vertex].x, 1.0f-input.texcoords[vertex].y);
            materials->Install_UV_Array(pass, stage, uvs, count);
        }
        // Indexed corner UVs are retained and validated by the decoder. The
        // current mesh description consumes vertex-indexed UV arrays only.
    }
    context->CurTexStage = static_cast<int>(bindings.stages.size());
    ++context->CurPass;
    return WW3D_ERROR_OK;
}


/***********************************************************************************************
 * MeshModelClass::read_prelit_material -- read prelit material chunks.								  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/02/99    IML : Created.                                                                *
 *=============================================================================================*/
WW3DErrorType MeshModelClass::read_prelit_material (ChunkLoadClass &cload, MeshLoadContextClass *context)
{
	// If this chunk ID matches the selected prelit chunk ID then load it, otherwise skip it.
	if (cload.Cur_Chunk_ID() == context->PrelitChunkID) {

		// While there are chunks in the prelit material chunk wrapper...
		while (cload.Open_Chunk()) {

			WW3DErrorType error = WW3D_ERROR_OK;

			switch (cload.Cur_Chunk_ID()) {

				case W3D_CHUNK_MATERIAL_INFO:
					error = read_material_info (cload,context);
					break;

				case W3D_CHUNK_VERTEX_MATERIALS:
					error = read_vertex_materials (cload,context);
					break;

				case W3D_CHUNK_SHADERS:
					error = read_shaders (cload,context);
					break;

				case W3D_CHUNK_TEXTURES:
					error = read_textures (cload,context);
					break;

				case W3D_CHUNK_MATERIAL_PASS:
					error = read_material_pass (cload,context);
					break;

				default:

					// Unknown chunk.
					break;
			}
			cload.Close_Chunk();
			if (error != WW3D_ERROR_OK) return (error);
		}
	}

	return (WW3D_ERROR_OK);
}


/***********************************************************************************************
 * MeshModelClass::post_process -- post loading, perform any processing on this model.			  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   05/02/00   IML : Created.                                                                 *
 *   7/13/2001  hy : Added static sort postprocessing                                          *
 *=============================================================================================*/
void MeshModelClass::post_process()
{
#if 0
	// we want to allow this now due to usage of the static sort
	// Ensure no sorting, multipass meshes (for they are abomination...)
	if (DefMatDesc->Get_Pass_Count() > 1 && Get_Flag(SORT)) {
		WWDEBUG_SAY(( "Turning SORT off for multipass mesh %s",Get_Name() ));
		Set_Flag(SORT, false);
	}
#endif

	// skinned meshes should not have cull trees
	if (Get_Flag(MeshGeometryClass::SKIN)) {
		if (CullTree) {
			CullTree.reset();
		}
	}

	// turn off backface culling if the mesh is supposed to be two-sided
	if (Get_Flag(MeshGeometryClass::TWO_SIDED)) {

		DefMatDesc->Disable_Backface_Culling();
		if (AlternateMatDesc != nullptr) {
			AlternateMatDesc->Disable_Backface_Culling();
		}

	}

	// fog activation.
	if (WW3DAssetManager::Get_Instance()->Get_Activate_Fog_On_Load()) {
		post_process_fog();
	}

	// if the mesh is sorting, pick an appropriate static sort level
	// if default isn't set
	if (Get_Flag(SORT) && SortLevel==SORT_LEVEL_NONE && WW3D::Is_Munge_Sort_On_Load_Enabled()) {
		compute_static_sort_levels();
	}

	// If we need to, modify the mesh model to support overbrightening (change all
	// GRADIENT_MODULATE to GRADIENT_MODULATE2X)
	if (WW3D::Is_Overbright_Modify_On_Load_Enabled()) {
		modify_for_overbright();
	}
}

void MeshModelClass::post_process_fog()
{
	// If two pass...
	if (DefMatDesc->Get_Pass_Count() == 2) {

		// If single shader on both passes...
		if (!DefMatDesc->ShaderArray[0] && !DefMatDesc->ShaderArray[1]) {

			ShaderClass &shader0 = DefMatDesc->Shader [0];
			ShaderClass &shader1 = DefMatDesc->Shader [1];

			// Analyze the mesh to determine if it is the emissive map effect and if it is, fix it up appropriately.
			bool emissive_map_effect = DefMatDesc->PassCount == 2 &&
												shader0.Get_Texturing() == ShaderClass::TEXTURING_DISABLE &&
												shader0.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE &&
												shader0.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO &&
												shader0.Get_Primary_Gradient() == ShaderClass::GRADIENT_MODULATE &&
												shader0.Get_Secondary_Gradient() == ShaderClass::SECONDARY_GRADIENT_DISABLE &&
												shader1.Get_Texturing() == ShaderClass::TEXTURING_ENABLE &&
												shader1.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_SRC_ALPHA &&
												shader1.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_SRC_COLOR;

			if (emissive_map_effect) {

				// Change the shader/texture setting into an equivalent one which will enable setting fog
				// correctly: Note that we are setting up pass 0 to have a texture now.
				shader0.Set_Texturing(ShaderClass::TEXTURING_ENABLE);
				shader1.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
				shader0.Set_Fog_Func(ShaderClass::FOG_ENABLE);
				shader1.Set_Fog_Func(ShaderClass::FOG_SCALE_FRAGMENT);

				// Copy pass 1 texture/texture array to pass 0.
				REF_PTR_SET (DefMatDesc->Texture[0][0], DefMatDesc->Texture [1][0]);
				if (DefMatDesc->TextureArray [1][0].Is_Allocated()) {
					if (!DefMatDesc->TextureArray [0][0].Is_Allocated()) {
						DefMatDesc->TextureArray [0][0] = DefMatDesc->TextureArray [1][0].Clone();
					}
				}

				// Make pass 0 point to the same UV array as pass 1. If pass 1 has a vertex material
				// array, we only take the first one for determining UV source. The UV source is
				// used to set the UV source of all the vertex materials in pass 0.
				int uv_source = 0;
				if (DefMatDesc->MaterialArray[1].Is_Allocated()) {
					if (const auto *owner = DefMatDesc->MaterialArray[1].Peek(0); owner && owner->Peek()) {
						uv_source = owner->Peek()->Get_UV_Source(0);
					}
				} else {
					DefMatDesc->Material[1]->Get_UV_Source(0);
				}
				if (DefMatDesc->MaterialArray[0].Is_Allocated()) {
					for (int i = 0; i < VertexCount; i++) {
						if (const auto *owner = DefMatDesc->MaterialArray[0].Peek(i); owner && owner->Peek()) {
							owner->Peek()->Set_UV_Source(0, uv_source);
						}
					}
				} else {
					DefMatDesc->Material[0]->Set_UV_Source(0, uv_source);
				}

				return;
			}

			// Analyze the mesh to determine if it is the shiny mask effect and if it is, fix it up appropriately.
			bool shiny_mask_effect = shader0.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE &&
											 shader0.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO &&
											 shader1.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE &&
											(shader1.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_SRC_ALPHA ||
											 shader1.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);

			if (shiny_mask_effect) {
				shader0.Set_Fog_Func(ShaderClass::FOG_SCALE_FRAGMENT);
				shader1.Set_Fog_Func(ShaderClass::FOG_ENABLE);
				return;
			}
		}
	}

	// Mesh is not one of the special two-pass combinations. Apply a per-pass generic fix-up.
	for (int pass = 0; pass < DefMatDesc->PassCount; pass++) {
		DefMatDesc->Shader [pass].Enable_Fog (Get_Name());
		if (DefMatDesc->ShaderArray [pass]) {
			for (int tri = 0; tri < DefMatDesc->ShaderArray [pass]->Get_Count(); tri++) {
				DefMatDesc->ShaderArray [pass]->Get_Element (tri).Enable_Fog (Get_Name());
			}
		}
	}
}

unsigned int MeshModelClass::get_sort_flags(int pass) const
{
	unsigned int flags = 0;
	ShaderClass::StaticSortCategoryType scat;
	if (Has_Shader_Array(pass)) {
		for (int tri = 0; tri < CurMatDesc->ShaderArray[pass]->Get_Count(); tri++) {
			scat = CurMatDesc->ShaderArray[pass]->Get_Element(tri).Get_SS_Category();
			flags |= (1 << scat);
		}
	} else {
		scat = Get_Single_Shader(pass).Get_SS_Category();
		flags |= (1 << scat);
	}
	return flags;
}

unsigned int MeshModelClass::get_sort_flags() const
{
	unsigned int flags = 0;
	for (int pass = 0; pass < Get_Pass_Count(); pass++) {
		flags |= get_sort_flags(pass);
	}
	return flags;
}

void MeshModelClass::compute_static_sort_levels()
{
	enum StaticSortCategoryBitFieldType
	{
		SSCAT_OPAQUE_BF		= (1 << ShaderClass::SSCAT_OPAQUE),
		SSCAT_ALPHA_TEST_BF	= (1 << ShaderClass::SSCAT_ALPHA_TEST),
		SSCAT_ADDITIVE_BF		= (1 << ShaderClass::SSCAT_ADDITIVE),
		SSCAT_SCREEN_BF		= (1 << ShaderClass::SSCAT_SCREEN),
		SSCAT_OTHER_BF			= (1 << ShaderClass::SSCAT_OTHER)
	};

	if (get_sort_flags(0) == SSCAT_OPAQUE_BF) {
		SortLevel = SORT_LEVEL_NONE;
		return;
	}

	switch (get_sort_flags())
	{
	case (SSCAT_OPAQUE_BF | SSCAT_ALPHA_TEST_BF):
		SortLevel = SORT_LEVEL_NONE;
		break;

	case SSCAT_ADDITIVE_BF:
		SortLevel = SORT_LEVEL_BIN3;
		break;

	case SSCAT_SCREEN_BF:
		SortLevel = SORT_LEVEL_BIN2;
		break;

	default:
		SortLevel = SORT_LEVEL_BIN1;
		break;
	};
}

void MeshModelClass::modify_for_overbright()
{
	// Iterate over all passes
	int pass_cnt = Get_Pass_Count();
	for (int pass_idx = 0; pass_idx < pass_cnt; pass_idx++) {

		// First do single shader
		ShaderClass shader = Get_Single_Shader(pass_idx);
		if (shader.Get_Primary_Gradient() == ShaderClass::GRADIENT_MODULATE) {
			shader.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE2X);
			Set_Single_Shader(shader, pass_idx);
		}

		// Now do all other shaders
		if (Has_Shader_Array(pass_idx)) {
			int p_cnt = Get_Polygon_Count();
			for (int p_idx = 0; p_idx < p_cnt; p_idx++) {
				shader = Get_Shader(p_idx, pass_idx);
				if (shader.Get_Primary_Gradient() == ShaderClass::GRADIENT_MODULATE) {
					shader.Set_Primary_Gradient(ShaderClass::GRADIENT_MODULATE2X);
					Set_Shader(p_idx, shader, pass_idx);
				}
			}
		}
	}

}

void MeshModelClass::install_materials(MeshLoadContextClass * context)
{
	int i;

	/*
	** If alternate material chunks were loaded, initialize the AlternateMatDesc
	*/
	install_alternate_material_desc(context);

	/*
	** Finish configuring the vertex materials and color arrays.
	*/
	bool lighting_enabled=true;
	// vertex-lit models need the lighting turned off!
	if (Get_Flag(MeshGeometryClass::PRELIT_VERTEX)) {
		lighting_enabled=false;
	}
	DefMatDesc->Post_Load_Process (lighting_enabled,this);
	if (AlternateMatDesc != nullptr) {
		AlternateMatDesc->Post_Load_Process (lighting_enabled,this);
	}

	/*
	** transfer the refs to our textures into the MatInfo
	*/
	for (i=0; i<context->Texture_Count(); i++) {
		MatInfo->Add_Texture(context->Peek_Texture(i));
	}

	/*
	** transfer the refs to our vertex materials into the MatInfo
	*/
	for (i=0; i<context->Vertex_Material_Count(); i++) {
		MatInfo->Add_Vertex_Material(context->Peek_Vertex_Material(i));
	}
}


void MeshModelClass::clone_materials(const MeshModelClass & srcmesh)
{
	/*
	** Copy the material info and the materials within
	*/
	REF_PTR_RELEASE(MatInfo);
	MatInfo = NEW_REF( MaterialInfoClass,(*(srcmesh.MatInfo)));

	/*
	** remap!
	*/
	MaterialRemapperClass remapper(srcmesh.MatInfo, MatInfo);
	remapper.Remap_Mesh(srcmesh.CurMatDesc, CurMatDesc);
}


void MeshModelClass::install_alternate_material_desc(MeshLoadContextClass * context)
{
	if (context->AlternateMatDesc.Is_Empty() == false) {
		WWASSERT(AlternateMatDesc == nullptr);
		AlternateMatDesc = W3DNEW MeshMatDescClass;
		AlternateMatDesc->Init_Alternate(*DefMatDesc,context->AlternateMatDesc);
	}
}

/***********************************************************************************************
 * MeshLoadContextClass::MeshLoadContextClass -- constructor for MeshLoadContextClass          *
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
MeshLoadContextClass::MeshLoadContextClass()
{
	memset(&MatInfo,0,sizeof(MatInfo));
	PrelitChunkID = 0xffffffff;
	CurPass = 0;
	CurTexStage = 0;
	TexCoords = nullptr;
	LoadedDIG = false;
}


/***********************************************************************************************
 * MeshLoadContextClass::~MeshLoadContextClass -- destructor                                   *
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
MeshLoadContextClass::~MeshLoadContextClass()
{
	int i;

	delete TexCoords;
	TexCoords = nullptr;

	for (i=0; i<Textures.Count(); i++) {
		Textures[i]->Release_Ref();
	}
	for (i=0; i<VertexMaterials.Count(); i++) {
		VertexMaterials[i]->Release_Ref();
	}
	for (i=0; i<LegacyMaterials.Count(); i++) {
		delete LegacyMaterials[i];
	}
}


/***********************************************************************************************
 * MeshLoadContextClass::Get_Texcoord_Array -- returns the texture coordinates array           *
 *                                                                                             *
 * This function mainly exists to support the obsolete version 3.0 w3d files                   *
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
W3dTexCoordStruct * MeshLoadContextClass::Get_Texcoord_Array()
{
	if (TexCoords == nullptr) {
		TexCoords = W3DNEWARRAY W3dTexCoordStruct[Header.vertex_count];
	}
	return TexCoords;
}


/***********************************************************************************************
 * MeshLoadContextClass::Add_Shader -- adds a shader to the array                              *
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
int MeshLoadContextClass::Add_Shader(ShaderClass shader)
{
	int index = Shaders.Count();
	Shaders.Add(shader);
	return index;
}


/***********************************************************************************************
 * MeshLoadContextClass::Add_Vertex_Materail -- adds a vertex material                         *
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
int MeshLoadContextClass::Add_Vertex_Material(VertexMaterialClass * vmat)
{
	WWASSERT(vmat != nullptr);
	vmat->Add_Ref();
	int index = VertexMaterials.Count();
	VertexMaterials.Add(vmat);
	return index;
}


/***********************************************************************************************
 * MeshLoadContextClass::Add_Texture -- adds a texture                                         *
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
int MeshLoadContextClass::Add_Texture(TextureClass * tex)
{
	WWASSERT(tex != nullptr);
	tex->Add_Ref();
	int index = Textures.Count();
	Textures.Add(tex);
	return index;
}


/***********************************************************************************************
 * MeshLoadContextClass::Add_Legacy_Material -- adds a legacy material                         *
 *                                                                                             *
 * This function will check to see if the parameters of any of the parts of the material are   *
 * duplicated.  Only unique shaders/vertexmaterials will be actually added.                    *
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
void MeshLoadContextClass::Add_Legacy_Material(ShaderClass shader,VertexMaterialClass * vmat,TextureClass * tex)
{
	// create a new legacy material
	LegacyMaterialClass * mat = W3DNEW LegacyMaterialClass;

	// add the shader if it is unique
	int si=0;
	for (; si<Shaders.Count(); si++) {
		if (Shaders[si] == shader) break;
	}
	if (si == Shaders.Count()) {
		mat->ShaderIdx = Add_Shader(shader);
	} else {
		mat->ShaderIdx = si;
	}

	// add the vertex material if it is unique
	if (vmat == nullptr) {
		mat->VertexMaterialIdx = -1;
	} else {
		unsigned long crc = vmat->Get_CRC();
		int vi=0;
		for (; vi<VertexMaterialCrcs.Count(); vi++) {
			if (VertexMaterialCrcs[vi] == crc) break;
		}
		if (vi == VertexMaterials.Count()) {
			mat->VertexMaterialIdx = Add_Vertex_Material(vmat);
			VertexMaterialCrcs.Add(crc);
			WWASSERT(VertexMaterialCrcs.Count() == VertexMaterials.Count());
		} else {
			mat->VertexMaterialIdx = vi;
		}
	}

	// add the texture if it is unique
	if (tex == nullptr) {
		mat->TextureIdx = -1;
	} else {
		int ti=0;
		for (; ti<Textures.Count(); ti++) {
			if (Textures[ti] == tex) break;
			if (WW3DString::Compare_No_Case(Textures[ti]->Get_Texture_Name(),tex->Get_Texture_Name()) == 0) break;
		}
		if (ti == Textures.Count()) {
			mat->TextureIdx = Add_Texture(tex);
		} else {
			mat->TextureIdx = ti;
		}
	}

	LegacyMaterials.Add(mat);
}


/***********************************************************************************************
 * MeshLoadContextClass::Peek_Legacy_Shader -- returns a legacy shader								  *
 *                                                                                             *
 * This function does the re-indexing to go from the legacy shader index to the actual shader  *
 * index and then returns that shader                                                          *
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
ShaderClass MeshLoadContextClass::Peek_Legacy_Shader(int legacy_material_index)
{
	WWASSERT(legacy_material_index >= 0);
	WWASSERT(legacy_material_index < LegacyMaterials.Count());
	int si = LegacyMaterials[legacy_material_index]->ShaderIdx;
	return Peek_Shader(si);
}


/***********************************************************************************************
 * MeshLoadContextClass::Peek_Legacy_Vertex_Material -- returns a pointer to a legacy vertex ma*
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
VertexMaterialClass * MeshLoadContextClass::Peek_Legacy_Vertex_Material(int legacy_material_index)
{
	WWASSERT(legacy_material_index >= 0);
	WWASSERT(legacy_material_index < LegacyMaterials.Count());
	int vi = LegacyMaterials[legacy_material_index]->VertexMaterialIdx;
	if (vi != -1) {
		return Peek_Vertex_Material(vi);
	} else {
		return nullptr;
	}
}


/***********************************************************************************************
 * MeshLoadContextClass::Peek_Legacy_Texture -- returns a pointer to a texture                 *
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
TextureClass * MeshLoadContextClass::Peek_Legacy_Texture(int legacy_material_index)
{
	WWASSERT(legacy_material_index >= 0);
	WWASSERT(legacy_material_index < LegacyMaterials.Count());
	int ti = LegacyMaterials[legacy_material_index]->TextureIdx;
	if (ti != -1) {
		return Peek_Texture(ti);
	} else {
		return nullptr;
	}
}


Vector2 * MeshLoadContextClass::Get_Temporary_UV_Array(int elementcount)
{
	TempUVArray.Uninitialised_Grow(elementcount);
	return &(TempUVArray[0]);
}
