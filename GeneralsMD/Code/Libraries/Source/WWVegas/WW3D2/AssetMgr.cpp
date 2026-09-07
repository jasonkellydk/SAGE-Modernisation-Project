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

/* $Header: /Commando/Code/ww3d2/AssetMgr.cpp 43    11/01/01 1:11a Jani_p $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/AssetMgr.cpp                           $*
 *                                                                                             *
 *                   Org Author:: Greg_h                                                       *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 10:14a                                              $*
 *                                                                                             *
 *                    $Revision:: 46                                                          $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 * 07/01/02 KM Shader library integration
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   WW3DAssetManager::WW3DAssetManager -- Constructor                                         *
 *   WW3DAssetManager::~WW3DAssetManager -- Destructor                                         *
 *   WW3DAssetManager::Free -- free all memory (un-needed?)                                    *
 *   WW3DAssetManager::Free_Assets -- Release all loaded assets                                *
 *   WW3DAssetManager::Load_3D_Assets -- Load 3D assets from a .W3D file                       *
 *   WW3DAssetManager::Load_Prototype -- loads a prototype from a W3D chunk                    *
 *   WW3DAssetManager::Create_Render_Obj -- Create a render object for the user                *
 *   WW3DAssetManager::Render_Obj_Exists -- Check whether a render object with the given name  *
 *   WW3DAssetManager::Create_Render_Obj_Iterator -- Create an iterator which can enumerate al *
 *   WW3DAssetManager::Release_Render_Obj_Iterator -- release a render object iterator         *
 *   WW3DAssetManager::Create_Material_Iterator -- Create a material iterator                  *
 *   WW3DAssetManager::Acquire_Animation -- Returns a pointer to a names HAnim                         *
 *   WW3DAssetManager::Get_Material -- Gets a pointer to a loaded material or creates it       *
 *   WW3DAssetManager::Get_Material -- Gets a pointer to a loaded material or creates the mate *
 *   WW3DAssetManager::Get_Material -- Gets a pointer to a loaded material or creates it       *
 *   WW3DAssetManager::Release_Material -- Release a material                                  *
 *   WW3DAssetManager::Add_Material -- Add a material to the list                              *
 *   WW3DAssetManager::Get_Texture -- get a TextureClass for the specified targa               *
 *   WW3DAssetManager::Release_All_Textures -- release all textures in the system              *
 *   WW3DAssetManager::Register_Model_Decoder -- add a new loader to the system             *
 *   WW3DAssetManager::Model decoder lookup -- find the loader that handles this chunk type   *
 *   WW3DAssetManager::Add_Prototype -- adds the prototype to the hash table                   *
 *   WW3DAssetManager::Find_Prototype -- searches the hash table for the prototype             *
 *   WW3DAssetManager::Open_Texture_File_Cache -- Turn on the texture cache system.            *
 *   WW3DAssetManager::Close_Texture_File_Cache -- Turn off the texture cache system.          *
 *   CachedTextureFileClass::getMipmapData -- get data for texture - check to see if in cache. *
 *   CachedTextureFileClass::getMipmapLevelPartial -- not yet implemented                      *
 *   CachedTextureFileClass::setupDefaultValues -- loads texture in to get default data.       *
 *   WW3DAssetManager::Get_Streaming_Texture -- Gets a streaming texture.                      *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <map>
import Assets.Images.PixelEncoding;
#include "AssetMgr.h"
#include <assert.h>

#include "WWLib/bittype.h"
#include "WWLib/chunkio.h"
#include "WWLib/realcrc.h"

#include "WWDebug/wwdebug.h"

#include <cstddef>
#include <utility>
#include <vector>
#include "Texture.h"
import Assets.Adapters.W3D.Rig;
#include "Collect.h"
#include "WW3D.h"
#include "WWLib/ffactory.h"
#include "HLOD.h"
#include "Mesh.h"
#include "WWLib/wwstring.h"
#include "WWLib/RAWFILE.h"
#include "WWDebug/wwmemlog.h"
#include "Dazzle.h"
import Assets.Adapters.W3D.Retention;
#include "WWDebug/wwprofile.h"
#include "WW3D2/StringUtilities.h"

#include <string>

#include "ShdLib.h"
import Assets.Cache.Animations;

/*
** Static member variable which keeps track of the single instanced asset manager
*/
WW3DAssetManager *		WW3DAssetManager::TheInstance = nullptr;

/*
** Iterator for the Render Objects in the asset manager
*/
class RObjIterator : public RenderObjIterator
{
public:
	virtual bool					Is_Done() override;
	virtual const char *			Current_Item_Name() override;
	virtual int						Current_Item_Class_ID() override;
protected:
	friend class WW3DAssetManager;
};


/***********************************************************************************************
 * WW3DAssetManager::WW3DAssetManager -- Constructor                                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *   05/10/1999 SKB : Add TextureCache                                                         *
 *=============================================================================================*/
WW3DAssetManager::WW3DAssetManager() :


	WW3D_Load_On_Demand		(false),
	Activate_Fog_On_Load		(false)
{
	assert(TheInstance == nullptr);
	TheInstance = this;

	// install the default loaders
#ifndef USE_WWSHADE
	Register_Model_Decoder(W3D_CHUNK_MESH,Load_Mesh_Factory);
#endif

	Register_Model_Decoder(W3D_CHUNK_HMODEL,Load_HModel_Factory);
	Register_Model_Decoder(W3D_CHUNK_COLLECTION,Load_Collection_Factory);
	Register_Model_Decoder(W3D_CHUNK_HLOD,Load_HLod_Factory);
	Register_Model_Decoder(W3D_CHUNK_LODMODEL,Load_ModelLevels_Factory);
	Register_Model_Decoder(W3D_CHUNK_AGGREGATE,Load_Aggregate_Factory);
	Register_Model_Decoder(W3D_CHUNK_DAZZLE,Load_Dazzle_Factory);

	SHD_REG_LOADER;

}


/***********************************************************************************************
 * WW3DAssetManager::~WW3DAssetManager -- Destructor                                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
WW3DAssetManager::~WW3DAssetManager()
{
	Free();
	Assets::Get_Animation_Cache().Reset_Missing();

#ifdef WWDEBUG
	if (Report.Reporting_Enabled()) {
		const std::string report = Report.Format_Report();
		RawFileClass raw_log_file("asset_report.txt");
		raw_log_file.Create();
		raw_log_file.Open(RawFileClass::WRITE);
		raw_log_file.Write(report.data(), static_cast<int>(report.size()));
		raw_log_file.Close();
	}
#endif

	TheInstance = nullptr;

}

static void Create_Number_String(StringClass& number, unsigned value)
{
	unsigned miljoonat=value/(1024*1028);
	unsigned tuhannet=(value/1024)%1024;
	unsigned ykkoset=value%1024;
	if (miljoonat) {
		number.Format("%d %3.3d %3.3d",miljoonat,tuhannet,ykkoset);
	}
	else if (tuhannet) {
		number.Format("%d %3.3d",tuhannet,ykkoset);
	}
	else {
		number.Format("%d",ykkoset);
	}
}

static void Log_Textures(bool inited,unsigned& total_count, unsigned& total_mem)
{
	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass * tex=ite.Peek_Value();
		if (tex->Is_Initialized()!=inited) continue;

		Assets::ImageDescription desc;
		tex->Get_Level_Description(desc);
		if (desc.width == 0 || desc.height == 0) continue;

		StringClass tex_format;
		tex_format.Format("Pixel encoding %d", static_cast<int>(desc.encoding));

		unsigned texmem=tex->Get_Texture_Memory_Usage();
		total_mem+=texmem;
		total_count++;
		StringClass number;
		Create_Number_String(number,texmem);

		WWDEBUG_SAY(("%32s	%4d * %4d (%15s), init %d, size: %14s bytes, refs: %d",
			tex->Get_Texture_Name().str(),
			desc.width,
			desc.height,
			tex_format.str(),
			tex->Is_Initialized(),
			number.str(),
			tex->Num_Refs()));

	}
}

void WW3DAssetManager::Log_Texture_Statistics()
{
	unsigned total_initialized_tex_mem=0;
	unsigned total_uninitialized_tex_mem=0;
	unsigned total_initialized_count=0;
	unsigned total_uninitialized_count=0;
	StringClass number;

	WWDEBUG_SAY(("\nInitialized textures ------------------------------------------\n"));
	Log_Textures(true,total_initialized_count,total_initialized_tex_mem);

	Create_Number_String(number,total_initialized_tex_mem);
	WWDEBUG_SAY(("\n%d initialized textures, totalling %14s bytes\n",
		total_initialized_count,
		number.str()));

	WWDEBUG_SAY(("\nUn-initialized textures ---------------------------------------\n"));
	Log_Textures(false,total_uninitialized_count,total_uninitialized_tex_mem);

	Create_Number_String(number,total_uninitialized_tex_mem);
	WWDEBUG_SAY(("\n%d un-initialized textures, totalling, totalling %14s bytes\n",
		total_uninitialized_count,
		number.str()));
/*
	RenderObjIterator * rite=WW3DAssetManager::Get_Instance()->Create_Render_Obj_Iterator();
	if (rite) {
		for (rite->First(); !rite->Is_Done(); rite->Next()) {
//			RenderObjClass * robj=Create_Render_Obj(rite->Current_Item_Name());
//			if (robj) {
//
//				robj->Release_Ref();
//			}
			if (rite->Current_Item_Class_ID()==RenderObjClass::CLASSID_HMODEL) {
				WWDEBUG_SAY(("robj: %s",rite->Current_Item_Name()));
			}
		}

		WW3DAssetManager::Get_Instance()->Release_Render_Obj_Iterator(rite);
	}
*/
}

/***********************************************************************************************
 * WW3DAssetManager::Free -- free all memory (un-needed?)                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Free()
{
	Free_Assets();
}


/***********************************************************************************************
 * WW3DAssetManager::Free_Assets -- Release all loaded assets                                  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *   05/10/1999 SKB : Close down texture cache file.                                           *
 *=============================================================================================*/
void WW3DAssetManager::Free_Assets()
{
	WWPROFILE( "WW3DAssetManager::Free_Assets" );

	ModelFactories.Clear();

	// delete all of the anims and trees
	Assets::Get_Animation_Cache().Clear_Named();
	Skeletons.Clear();

	// release all my references to the materials
	Release_All_Textures();

	// Close down cache if it is open.
	// NONONONOO.... Don't close it as we might want to free the assets and still be able to load textures.
//	Close_Texture_File_Cache();
}


/***********************************************************************************************
 * WW3DAssetManager::Free_Unused_Assets -- Release all assets that are referenced only by      *
 *                                         the asset manager.                                  *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/18/99   EHC : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Release_Unused_Assets()
{
	// release all references to objects that have only one reference on them
	// and remove them from our lists.
	Release_Unused_Textures();
}

/***********************************************************************************************
 * WW3DAssetManager::Free_Assets_With_Exclusion_List -- Release all assets that are not named  *
 *                                                      in the exclusion list.                 *
 *                                                                                             *
 * This function checks if each prototype is named or is a child of something named in the     *
 * given exclusion list.                                                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 * exclusion_list - list of names of render object prototypes to not release.                  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/12/2002 GH  : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Free_Assets_With_Exclusion_List(const DynamicVectorClass<StringClass> & exclusion_names)
{

	// Retention names intentionally preserve spelling and case.
    std::unordered_set<std::string> retained;
    retained.reserve(exclusion_names.Count());
    for(int i=0;i<exclusion_names.Count();++i)retained.emplace(exclusion_names[i].str());

    ModelFactories.Erase_If([&](Graphics::ModelFactory<RenderObjClass>& factory) {
        const auto key=Assets::W3D::W3D_Model_Retention_Key(factory.name.c_str());
        return !key || !retained.contains(std::string(*key));
    });

	// delete all of the anims and trees
	Assets::Get_Animation_Cache().Remove_Unused_If([&](const Assets::AnimationClip& clip) {
        const auto key=Assets::W3D::W3D_Animation_Retention_Key(clip.name.c_str());
        return !key || !retained.contains(std::string(*key));
    });
	Skeletons.Remove_If([&](const Assets::ModelRigDesc& skeleton) {
		return !retained.contains(skeleton.skeleton_name);
	});

	// release references to textures that are not used
	Release_Unused_Textures();

}

/***********************************************************************************************
 * WW3DAssetManager::Create_Asset_List -- Create a list of the W3D files that are loaded       *
 *                                                                                             *
 * This function checks if each prototype is named or is a child of something named in the     *
 * given exclusion list.                                                                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 * model_list - dynamic vector to populate with names of w3d files loaded                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 * As in other places in the code, we are assuming that w3d filenames match the "top-level"    *
 * render object contained within them!                                                        *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/12/2002 GH  : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Create_Asset_List(DynamicVectorClass<StringClass> & model_list)
{
	for (int i=0; i<static_cast<int>(ModelFactories.Size()); i++) 	{
		// ok, we ignore all of the following:
		// - sub objects, these will have a '.' in their name
		// - munged objects, these will have # characters in their name
		Graphics::ModelFactory<RenderObjClass> * proto = ModelFactories.At(i);
		if (proto) {
			const char * name = proto->name.c_str();

			if ((strchr(name,'#') == nullptr) && (strchr(name,'.') == nullptr)) {
				model_list.Add(StringClass(name));
			}
		}
	}

	// Add in the w3d files for all of the animations
	Assets::Get_Animation_Cache().Visit_Named([&](const Assets::AnimationClip& clip) {
        const auto separator=clip.name.find('.');
        if(separator!=std::string::npos)model_list.Add(StringClass(clip.name.c_str()+separator+1));
    });
}


/***********************************************************************************************
 * WW3DAssetManager::Load_3D_Assets -- Load 3D assets from a file                              *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   10/22/98   BMG : Created.                                                                 *
 *=============================================================================================*/
bool WW3DAssetManager::Load_3D_Assets( const char * filename )
{
	bool result = false;

	FileClass * file = _TheFileFactory->Get_File( filename );
	if ( file ) {
		if ( file->Is_Available() ) {
			result = WW3DAssetManager::Load_3D_Assets( *file );
		} else {
			WWDEBUG_SAY(("Missing asset '%s'.", filename));
		}
		_TheFileFactory->Return_File( file );
	}

	return result;
}


/***********************************************************************************************
 * WW3DAssetManager::Load_3D_Assets -- Load 3D assets from a .W3D file                         *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
bool WW3DAssetManager::Load_3D_Assets(FileClass & w3dfile)
{
	WWPROFILE( "WW3DAssetManager::Load_3D_Assets" );
	if (!w3dfile.Open()) {
		return false;
	}

	ChunkLoadClass cload(&w3dfile);

	while (cload.Open_Chunk()) {

		switch (cload.Cur_Chunk_ID()) {

			case W3D_CHUNK_HIERARCHY:
				Load_Skeleton(cload);
				break;

			case W3D_CHUNK_ANIMATION:
			case W3D_CHUNK_COMPRESSED_ANIMATION:
			case W3D_CHUNK_MORPH_ANIMATION:
				Load_Animation_Chunk(cload);
				break;

			default:
				Load_Prototype(cload);
				break;
		}

		cload.Close_Chunk();
	}

	w3dfile.Close();

	return true;
}


/***********************************************************************************************
 * WW3DAssetManager::Load_Prototype -- loads a prototype from a W3D chunk                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/29/98    GTH : Created.                                                                 *
 *   2/19/99    EHC : Now has the Add_Prototype call responsible for adding the prototype to   *
 *                    the Prototypes list object.                                              *
 *=============================================================================================*/
bool WW3DAssetManager::Load_Prototype(ChunkLoadClass & cload)
{
	WWPROFILE( "WW3DAssetManager::Load_Prototype" );
	WWMEMLOG(MEM_GEOMETRY);

	/*
	** Get the chunk id
	*/
	int chunk_id = cload.Cur_Chunk_ID();

	/*
	** Find a loader that handles that type of chunk
	*/
	const auto loader = ModelDecoders.find(chunk_id);
	Graphics::ModelFactory<RenderObjClass> * newproto = nullptr;

	if (loader != ModelDecoders.end()) {

		/*
		** Ask it to create a prototype from the contents of the
		** chunk.
		*/
		newproto = loader->second(cload);

	} else {

		/*
		** Warn user about an unknown chunk type
		*/
		WWDEBUG_SAY(("Unknown chunk type encountered!  Chunk Id = %d",chunk_id));
		return false;
	}

	/*
	** Now, see if the prototype that we loaded has a duplicate
	** name with any of our currently loaded prototypes (can't have that!)
	*/
	if (newproto != nullptr) {

		if (!Render_Obj_Exists(newproto->name.c_str())) {

			/*
			** Add the new, unique prototype to our list
			*/
			Add_Prototype(newproto);

		} else {

			/*
			** Warn the user about a name collision with this prototype
			** and dump it
			*/
			WWDEBUG_SAY(("Render Object Name Collision: %s",newproto->name.c_str()));
			delete newproto;
			newproto = nullptr;
			return false;
		}

	} else {

		/*
		** Warn user that a prototype was not generated from this
		** chunk type
		*/
		WWDEBUG_SAY(("Could not generate prototype!  Chunk  = %d",chunk_id));
		return false;
	}

	return true;
}


/***********************************************************************************************
 * WW3DAssetManager::Create_Render_Obj -- Create a render object for the user                  *
 *                                                                                             *
 *    This function will create any type of render object.  I.e. if you pass in the name       *
 *    of an HModel, it will create an hmodel for you.                                          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
RenderObjClass * WW3DAssetManager::Create_Render_Obj(const char * name)
{
	WWPROFILE( "WW3DAssetManager::Create_Render_Obj" );
	WWMEMLOG(MEM_GEOMETRY);

	// Try to find a prototype
	Graphics::ModelFactory<RenderObjClass> * proto = Find_Prototype(name);

	if (WW3D_Load_On_Demand && proto == nullptr) {	// If we didn't find one, try to load on demand
		Report.Record_Load_On_Demand(Assets::AssetReportCategory::Model, name);

		std::string filename;
		const char *mesh_name = ::strchr (name, '.');
		if (mesh_name != nullptr) {
			filename.assign(name, static_cast<std::size_t>(mesh_name - name));
			filename += ".w3d";
		} else {
			filename = name;
			filename += ".w3d";
		}

		// If we can't find it, try the parent directory
		if ( Load_3D_Assets( filename.c_str() ) == false ) {
			StringClass	new_filename(StringClass("..\\"),true);
			new_filename += filename.c_str();
			Load_3D_Assets( new_filename );
		}

		proto = Find_Prototype(name);		// try again
	}

	if (proto == nullptr) {
		static int warning_count = 0;
		// Note - objects named "#..." are scaled cached objects, so don't warn...
		if (name[0] != '#') {
			if (++warning_count <= 20) {
				WWDEBUG_SAY(("WARNING: Failed to create Render Object: %s",name));
			}
			Report.Record_Missing(Assets::AssetReportCategory::Model, name);
		}
		return nullptr;		// Failed to find a prototype
	}

	return proto->Instantiate();
}


/***********************************************************************************************
 * WW3DAssetManager::Render_Obj_Exists -- Check whether a render object with the given name ex *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
bool WW3DAssetManager::Render_Obj_Exists(const char * name)
{
	if (Find_Prototype(name) == nullptr) return false;
	else return true;
}


/***********************************************************************************************
 * WW3DAssetManager::Create_Render_Obj_Iterator -- Create an iterator which can enumerate all  *
 *                                                                                             *
 *    The iterator returned can enumerate all of the loaded render objects for you.            *
 *    The user is responsible for releasing the iterator!                                      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *    User must release the iterator back to the asset manager or there will be a memory leak  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
RenderObjIterator * WW3DAssetManager::Create_Render_Obj_Iterator()
{
	return W3DNEW RObjIterator();
}


/***********************************************************************************************
 * WW3DAssetManager::Release_Render_Obj_Iterator -- release a render object iterator           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   9/28/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Release_Render_Obj_Iterator(RenderObjIterator * it)
{
	WWASSERT(it != nullptr);
	delete it;
}

/***********************************************************************************************
 * WW3DAssetManager::Acquire_Animation -- Returns a pointer to a names HAnim                           *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *    The implementation has changed since its inception, the asset manager no longer owns the *
 *	   hanim's so they need to be released by the caller.													  *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
Assets::AnimationAssetHandle WW3DAssetManager::Acquire_Animation(const char * name)
{
	WWPROFILE( "WW3DAssetManager::Acquire_Animation" );

	// Try to find the hanim
	Assets::AnimationAssetHandle anim = Assets::Get_Animation_Cache().Acquire(name);

	if (WW3D_Load_On_Demand && anim == nullptr) {	// If we didn't find it, try to load on demand

		if ( !Assets::Get_Animation_Cache().Is_Missing( name ) ) {	// if this is NOT a known missing anim

			Report.Record_Load_On_Demand(Assets::AssetReportCategory::Animation, name);

			std::string filename;
			const char *animname = strchr( name, '.');
			if (animname != nullptr) {
				filename = animname + 1;
				filename += ".w3d";
			} else {
				WWDEBUG_SAY(( "Animation %s has no . in the name", name ));
				WWASSERT( 0 );
				return nullptr;
			}

			// If we can't find it, try the parent directory
			if ( Load_3D_Assets( filename.c_str() ) == false ) {
				StringClass	new_filename = StringClass("..\\") + filename.c_str();
				Load_3D_Assets( new_filename );
			}

			anim = Assets::Get_Animation_Cache().Acquire(name);		// Try again
			if (anim == nullptr) {
				Assets::Get_Animation_Cache().Register_Missing( name );		// This is now a KNOWN missing anim
				Report.Record_Missing(Assets::AssetReportCategory::Animation, name);
			}
		}
	}

	return anim;
}


/***********************************************************************************************
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   12/21/97   GTH : Created.                                                                 *
 *=============================================================================================*/
bool WW3DAssetManager::Load_Skeleton(ChunkLoadClass & cload)
{
	const auto length = cload.Cur_Chunk_Length();
	std::vector<std::byte> bytes(length);
	Assets::ModelRigDesc skeleton;
	std::string error;
	if (cload.Read(bytes.data(), length) != length ||
		!Assets::W3D::W3DRead_Hierarchy(bytes, skeleton, error)) {
		return false;
	}
	return Skeletons.Publish(std::move(skeleton), error).Is_Valid();
}

Assets::SkeletonAssetHandle WW3DAssetManager::Get_Skeleton(const char * name)
{
	WWPROFILE( "WW3DAssetManager::Get_Skeleton" );

	// Resolve a prepared skeleton, loading its source when requested.
	auto skeleton = Skeletons.Find(name);

	if (WW3D_Load_On_Demand && !skeleton.Is_Valid()) {	// If we didn't find it, try to load on demand

		Report.Record_Load_On_Demand(Assets::AssetReportCategory::Skeleton, name);

		std::string filename = name;
		filename += ".w3d";

		// If we can't find it, try the parent directory
		if ( Load_3D_Assets( filename.c_str() ) == false ) {
			StringClass	new_filename("..\\",true);
			new_filename += filename.c_str();
			Load_3D_Assets( new_filename );
		}

		skeleton = Skeletons.Find(name);	// Try again

		if (!skeleton.Is_Valid()) {
			Report.Record_Missing(Assets::AssetReportCategory::Skeleton, name);
		}
	}

	return skeleton;
}

/***********************************************************************************************
 * WW3DAssetManager::Get_Texture -- get a TextureClass from the specified file                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/31/2001  NH : Created.                                                                  *
 *=============================================================================================*/
TextureClass * WW3DAssetManager::Get_Texture
(
	const char * filename,
	MipCountType mip_level_count,
	Assets::PixelEncoding texture_format,
	bool allow_compression,
	TextureBaseClass::TexAssetType type,
	bool allow_reduction
)
{
	WWPROFILE( "WW3DAssetManager::Get_Texture 1" );

	/*
	** We cannot currently mip-map bumpmaps
	*/
	if (texture_format==Assets::PixelEncoding::RG8_SNorm)
	{
		mip_level_count=MIP_LEVELS_1;
	}

	/*
	** Bail if the user isn't really asking for anything
	*/
	if ((filename == nullptr) || (strlen(filename) == 0))
	{
		return nullptr;
	}

	StringClass lower_case_name(filename,true);
	WW3DString::To_Lower(lower_case_name.Peek_Buffer());

	/*
	** See if the texture has already been loaded.
	*/
	TextureClass* tex = TextureHash.Get(lower_case_name);
	if (tex && (tex->Is_Initialized() == true) && (texture_format!=Assets::PixelEncoding::Unknown))
	{
		WWASSERT_PRINT(tex->Get_Texture_Format()==texture_format,("Texture %s has already been loaded with different format",filename));
	}

	/*
	** Didn't have it so we have to create a new texture
	*/
	if (!tex)
	{
		if (type==TextureBaseClass::TEX_REGULAR)
		{
			tex = NEW_REF (TextureClass, (lower_case_name, nullptr, mip_level_count, texture_format, allow_compression, allow_reduction));
		}
		else if (type==TextureBaseClass::TEX_CUBEMAP)
		{
			tex = NEW_REF (CubeTextureClass, (lower_case_name, nullptr, mip_level_count, texture_format, allow_compression, allow_reduction));
		}
		else if (type==TextureBaseClass::TEX_VOLUME)
		{
			tex = NEW_REF (VolumeTextureClass, (lower_case_name, nullptr, mip_level_count, texture_format, allow_compression, allow_reduction));
		}
		else
		{
			WWASSERT_PRINT(false, ("Unhandled case"));
			return nullptr;
		}

		TextureHash.Insert(tex->Get_Texture_Name(),tex);
	}

	tex->Add_Ref();
	return tex;
}


/***********************************************************************************************
 * WW3DAssetManager::Release_All_Textures -- release all textures in the system                *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/10/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Release_All_Textures()
{
	/*
	** for each texture in the list, get it and release ref it
	*/

	HashTemplateIterator<StringClass,TextureClass*> ite(TextureHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass * tex=ite.Peek_Value();
//		WWASSERT(tex->Num_Refs()==1);	// If asset manager is releasing the texture,
														// nobody should be referencing to it anymore!
		tex->Release_Ref();
	}
	TextureHash.Remove_All();
}


/***********************************************************************************************
 * WW3DAssetManager::Release_Unused_Textures -- release all textures with refcount == 1        *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/18/99    EHC : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Release_Unused_Textures()
{
	/*
	** for each texture in the list, get it, check it's refcount, and and release ref it if the
	** refcount is one.
	*/

	unsigned count=0;
	TextureClass* temp_textures[256];

	HashTemplateIterator<StringClass,TextureClass*> ite(TextureHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass* tex=ite.Peek_Value();
		if (tex->Num_Refs() == 1) {
			temp_textures[count++]=tex;
			if (count==256) {
				for (unsigned i=0;i<256;++i) {
					TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
					temp_textures[i]->Release_Ref();
				}
				count=0;
				ite.First();	// iterator doesn't support modifying the hash table while iterating, so start from the
									// beginning.
			}
		}
	}
	for (unsigned i=0;i<count;++i) {
		TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
		temp_textures[i]->Release_Ref();
	}
}

/***********************************************************************************************
 * WW3DAssetManager::Release_Texture -- release a specific texture from the asset manager      *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   2/18/99    EHC : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Release_Texture(TextureClass *tex)
{
	/*
	** Try to find the texture in the list, if found release it and remove it from the list.
	*/

	TextureHash.Remove(tex->Get_Texture_Name());
	tex->Release_Ref();
}

void WW3DAssetManager::Log_All_Textures()
{
	Log_Texture_Statistics();

	HashTemplateIterator<StringClass,TextureClass*> ite(TextureHash);

	// Log lightmaps -----------------------------------

	WWDEBUG_SAY((
		"Lightmap textures: %d\n\n"
		"size     name\n"
		"--------------------------------------"
		,
		TextureClass::_Get_Total_Lightmap_Texture_Count()));

	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass* t=ite.Peek_Value();
		if (!t->Is_Lightmap()) continue;

		StringClass tmp(true);
		unsigned bytes=t->Get_Texture_Memory_Usage();
		if (!t->Is_Initialized()) {
			tmp+="*";
		}
		else {
			tmp+=" ";
		}
		WWDEBUG_SAY(("%4.4dkb %s%s",bytes/1024,tmp.str(),t->Get_Texture_Name().str()));
	}

	// Log procedural textures -------------------------------

	WWDEBUG_SAY((
		"Procedural textures: %d\n\n"
		"size     name\n"
		"--------------------------------------"
		,
		TextureClass::_Get_Total_Procedural_Texture_Count()));

	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass* t=ite.Peek_Value();
		if (!t->Is_Procedural()) continue;

		StringClass tmp(true);
		unsigned bytes=t->Get_Texture_Memory_Usage();
		if (!t->Is_Initialized()) {
			tmp+="*";
		}
		else {
			tmp+=" ";
		}
		WWDEBUG_SAY(("%4.4dkb %s%s",bytes/1024,tmp.str(),t->Get_Texture_Name().str()));
	}

	// Log "ordinary" textures -------------------------------

	WWDEBUG_SAY((
		"Ordinary textures: %d\n\n"
		"size     name\n"
		"--------------------------------------"
		,
		TextureClass::_Get_Total_Texture_Count()-TextureClass::_Get_Total_Lightmap_Texture_Count()-TextureClass::_Get_Total_Procedural_Texture_Count()));

	for (ite.First();!ite.Is_Done();ite.Next()) {
		TextureClass* t=ite.Peek_Value();
		if (t->Is_Procedural()) continue;
		if (t->Is_Lightmap()) continue;

		StringClass tmp(true);
		unsigned bytes=t->Get_Texture_Memory_Usage();
		if (!t->Is_Initialized()) {
			tmp+="*";
		}
		else {
			tmp+=" ";
		}
		WWDEBUG_SAY(("%4.4dkb %s%s",bytes/1024,tmp.str(),t->Get_Texture_Name().str()));
	}

}



/***********************************************************************************************
 * WW3DAssetManager::Register_Model_Decoder -- add a new loader to the system               *
 *                                                                                             *
 *    The library will automatically install loaders for the "built-in" render object          *
 *    types.  This function exists so that the user can design App-specific render objects,    *
 *    define a chunk format for them, and have the asset manager load them in like everything  *
 *    else.                                                                                    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *    loader - pointer to a global or static instance of your loader type.                     *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/28/98    GTH : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Register_Model_Decoder(int chunk_id,ModelDecoder decode)
{
    WWASSERT(decode);
    if(decode)ModelDecoders.try_emplace(chunk_id,decode);
}

bool WW3DAssetManager::Install_Reserved_Model_Factory(
	std::unique_ptr<Graphics::ModelFactory<RenderObjClass>> factory)
{
	if (!factory || ReservedModelFactory)
		return false;
	ReservedModelFactory = std::move(factory);
	return true;
}


/***********************************************************************************************
 * WW3DAssetManager::Model decoder lookup -- find the loader that handles this chunk type     *
 *                                                                                             *
 * INPUT:                                                                                      *
 * chunk_id - chunk type that the loader needs to handle                                       *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 * pointer to the appropriate loader or null if one wasn't found                               *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/28/98    GTH : Created.                                                                 *
 *=============================================================================================*/



/***********************************************************************************************
 * WW3DAssetManager::Add_Prototype -- adds the prototype to the hash table                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/29/98    GTH : Created.                                                                 *
 *   12/8/98    GTH : Renamed to simply Add_Prototype                                          *
 *   2/19/99    EHC : Now adds the prototype to the prototype list                             *
 *=============================================================================================*/
void WW3DAssetManager::Add_Prototype(Graphics::ModelFactory<RenderObjClass> * newproto)
{
    WWASSERT(newproto != nullptr);
    if(newproto)ModelFactories.Insert(newproto->name.c_str(),decltype(ModelFactories)::Owner(newproto));
}


/***********************************************************************************************
 * WW3DAssetManager::Remove_Prototype -- Removes all references to the protype.					  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/4/99	 PDS : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Remove_Prototype(Graphics::ModelFactory<RenderObjClass> *proto)
{
	if (proto == nullptr || proto == ReservedModelFactory.get())
		return;
    // This overload transfers ownership back to its caller.
    ModelFactories.Release(proto).release();
}


/***********************************************************************************************
 * WW3DAssetManager::Remove_Prototype -- Removes all references to the protype.					  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/4/99	 PDS : Created.                                                                 *
 *=============================================================================================*/
void WW3DAssetManager::Remove_Prototype(const char *name)
{
	WWASSERT(name != nullptr);
	if (name != nullptr) {
		if (ReservedModelFactory != nullptr &&
			WW3DString::Compare_No_Case(name, ReservedModelFactory->name.c_str()) == 0)
			return;

		// Lookup the prototype by name
		Graphics::ModelFactory<RenderObjClass> *proto = Find_Prototype (name);
		if (proto != nullptr) {

			// Remove the prototype from our lists, and free its memory
			Remove_Prototype (proto);
			delete proto;
		}
	}
}


/***********************************************************************************************
 * WW3DAssetManager::Find_Prototype -- searches the hash table for the prototype               *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   7/29/98    GTH : Created.                                                                 *
 *   12/8/98    GTH : Renamed to simply Find_Prototype                                         *
 *=============================================================================================*/
Graphics::ModelFactory<RenderObjClass> * WW3DAssetManager::Find_Prototype(const char * name)
{
    if(name && ReservedModelFactory != nullptr &&
		WW3DString::Compare_No_Case(name, ReservedModelFactory->name.c_str()) == 0)
		return ReservedModelFactory.get();
    return name?ModelFactories.Find(name):nullptr;
}

/*
** Iterator Implementations.
** =====================================================================
** If the user derives a custom asset manager, you will have
** to implement iterators which can walk through your datastructures.
*/

bool RObjIterator::Is_Done()
{
	return !(Index < static_cast<int>(WW3DAssetManager::Get_Instance()->ModelFactories.Size()));
}

const char * RObjIterator::Current_Item_Name()
{
	if (Index < static_cast<int>(WW3DAssetManager::Get_Instance()->ModelFactories.Size())) {
		return WW3DAssetManager::Get_Instance()->ModelFactories.At(Index)->name.c_str();
	} else {
		return nullptr;
	}
}

int RObjIterator::Current_Item_Class_ID()
{
	if (Index < static_cast<int>(WW3DAssetManager::Get_Instance()->ModelFactories.Size())) {
		return WW3DAssetManager::Get_Instance()->ModelFactories.At(Index)->class_id;
	} else {
		return -1;
	}
}

void WW3DAssetManager::Load_Animation_Chunk(ChunkLoadClass& cload)
{
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if(cload.Read(bytes.data(),static_cast<unsigned>(bytes.size()))!=bytes.size())return;
    std::string error;
    auto& cache=Assets::Get_Animation_Cache();
    if(cload.Cur_Chunk_ID()!=W3D_CHUNK_MORPH_ANIMATION) {
        Assets::ModelAnimationDesc description;
        const bool keyed=cload.Cur_Chunk_ID()==W3D_CHUNK_COMPRESSED_ANIMATION;
        if(!Assets::W3D::W3DRead_Animation(bytes,keyed,description,error))return;
        const auto* skeleton=Resolve_Skeleton(Get_Skeleton(description.skeleton_name.c_str()));
        if(!skeleton)return;
        cache.Publish(std::move(description),static_cast<unsigned>(skeleton->bones.size()),
            keyed?Assets::AnimationSampling::Keyed:Assets::AnimationSampling::Consecutive,error);
        return;
    }
    Assets::ModelPoseAnimationDesc description;
    if(!Assets::W3D::W3DRead_Pose_Animation(bytes,description,error))return;
    const auto* skeleton=Resolve_Skeleton(Get_Skeleton(description.skeleton_name.c_str()));
    if(!skeleton)return;
    const auto bones=static_cast<unsigned>(skeleton->bones.size());
    struct Sources {
        std::vector<Assets::AnimationAssetHandle> handles;
        ~Sources() { for(auto handle:handles)Assets::Get_Animation_Cache().Release(handle); }
    } sources;
    sources.handles.reserve(description.channels.size());
    for(const auto& channel:description.channels) {
        const auto handle=Acquire_Animation(channel.animation_name.c_str());
        if(!handle)return;
        sources.handles.push_back(handle);
    }
    cache.Publish_Pose(std::move(description),bones,sources.handles,error);
}


