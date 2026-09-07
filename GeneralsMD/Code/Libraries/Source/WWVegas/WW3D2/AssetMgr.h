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

/* $Header: /Commando/Code/ww3d2/AssetMgr.h 19    12/17/01 7:55p Jani_p $ */
/***********************************************************************************************
 ***                            Confidential - Westwood Studios                              ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Commando                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/AssetMgr.h                             $*
 *                                                                                             *
 *                       Author:: Greg_h                                                       *
 *                                                                                             *
 *                     $Modtime:: 12/15/01 4:14p                                              $*
 *                                                                                             *
 *                    $Revision:: 19                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
import Graphics.Scene.Models.Factory;

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
import Assets.Cache.Animations;
import Graphics.Scene.Models.FactoryStore;
import Assets.Cache.AssetReport;


#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
import Assets.Images.PixelEncoding;
#include "WWLib/always.h"
#include "WWLib/Vector.h"
import Assets.Cache.Skeletons;
#include "WWLib/SLIST.h"
#include "WW3D2/Texture.h"
#include "WWLib/hashtemplate.h"
#include "WWLib/simplevec.h"

class	ChunkLoadClass;

class FileClass;
class FileFactoryClass;

class RenderObjClass;
class HModelClass;

class TextureIterator;
class TextureFileCache;
class StreamingTextureClass;
struct StreamingTextureConfig;
class TextureClass;


/*
** AssetIterator
**	This object can iterate through the 3D assets which
** currently exist in the Asset Manager.  It tells you the names
** of the assets which the manager can create for you.
*/
class AssetIterator
{

public:

	virtual							~AssetIterator() { };
	virtual void					First() { Index = 0; }
	virtual void					Next()	{ Index ++; }
	virtual bool					Is_Done() = 0;
	virtual const char *			Current_Item_Name() = 0;

protected:

	AssetIterator()			{ Index = 0; }
	int								Index;
};

/*
** RenderObjIterator
** The render obj iterator simply adds a method for determining
** the class id of a render object prototype in the system.
*/
class RenderObjIterator : public AssetIterator
{
public:
	virtual int						Current_Item_Class_ID() = 0;
};


/*

	WW3DAssetManager

	This object is the manager of all of the 3D data.  Load your meshes, animations,
	etc etc using the Load_3D_Assets function.

	WARNING: hierarchy trees should be loaded before the meshes and animations
	which attach to them.

	-------------------------------------------------------------------------------------
	Dec 11, 1997, Asset Manager Brainstorming:

	- WW3DAssetManager will be differentiated from other game data asset managers
	(sounds, strings, etc) because they behave differently and serve different
	purposes

	- WW3D creates "clones" from the blueprints it has of render objects whereas
	Our commando data asset manager will provide the data (file images) for the
	blueprints.  Maybe the CommandoDataManager could deal in MemoryFileClasses.
	Or void * and then the ww3d manager could convert to MemoryFiles...

	- Future caching: In the case that we want to implement a caching system,
	assets must be "released" when not in use.

   - CommandoW3d asset manager asks the game data asset manager for assets by name.
	Game data manager must have a "directory" structure which maps each named
	asset to data on disk.  It then returns an image of the file once it has
	been loaded into ram.

	- Assets must be individual files, named with the asset name used in code/scripting
	We will write a tool which chops w3d files up so that all of the individual assets
	are brought out into their own file and named with the actual w3d name.

   - Data Asset Manager will load the file into ram, give it to us and forget about it
	W3d will release_ref it or delete it and the file image will go away.

	- Each time the 3d asset manager is requested for an asset, it will look through
	the render objects it has, if the asset isn't found, it will ask for the asset
	from the generic data asset manager.  When creating the actual render object,
	the 3d asset manager may find that it needs another asset (such as a hierarchy tree).
	It will then recurse, and ask for that asset.

	- Copy Mode will go away.  It will be internally set based on whether the mesh
	contains vertex animation.  All other render objects will simply "Clone".

	- Commando will derive a Cmdo3DAssetManager which knows about the special chunks
	required for the terrains.

	-------------------------------------------------------------------------------------
	July 28, 1998

	- Exposed the prototype system and added prototype loaders (Graphics::ModelFactory<RenderObjClass> and
	decoder functions). This allows the user to install custom
	loaders for new render object types.

	- Simplified the interface by removing the special purpose creation functions,
	leaving only the Create_Render_Obj function.

	- In certain cases some users need to know what kind of render object was created
	so we added a Class_ID mechanism to RenderObjClass.

	- Class_ID for render objects is not enough.  Need the asset iterator to be able
	to tell you the Class_ID of each asset in the asset manager.  This also means that
	the prototype class needs to be able to tell you the class ID.  Actually this
	code only seems to be used by tools such as SView but is needed anyway...

	-------------------------------------------------------------------------------------
	TheSuperHackers @fix xezon 08/11/2025
	The Asset Manager will now return null when it cannot find or create a valid font
	with the given inputs. This way the user knows that the requested font is unusable.
*/


class WW3DAssetManager
{

public:

	/*
	** Constructor and destructor
	*/
	WW3DAssetManager();
	virtual ~WW3DAssetManager();

	/*
	** Access to the single instance of a WW3DAssetManager.  The user
	** can subclass their own asset manager class but should only
	** create one instance.  (a violation of this will be caught with
	** a run-time assertion)
	**
	** The "official" way to get at the global asset manager is to
	** use a line of code like this:
	**	WW3DAssetManager::Get_Instance();
	*/
	static WW3DAssetManager *		Get_Instance() { return TheInstance; }
	static void							Delete_This() { delete TheInstance; TheInstance=nullptr; }

	/*
	** Load data from any type of w3d file
	*/
	virtual bool						Load_3D_Assets( const char * filename);
	virtual bool						Load_3D_Assets(FileClass & assetfile);

	/*
	** Get rid of all of the currently loaded assets
	*/
	virtual void						Free_Assets();

	/*
	**	Release any assets that only the asset manager has a reference to.
	*/
	virtual void						Release_Unused_Assets();

	/*
	** Release assets not in the given exclusion list.
	*/
	virtual void						Free_Assets_With_Exclusion_List(const DynamicVectorClass<StringClass> & model_exclusion_list);
	virtual void						Create_Asset_List(DynamicVectorClass<StringClass> & model_exclusion_list);

	/*
	** create me an instance of one of the prototype render objects
	*/
	virtual RenderObjClass *		Create_Render_Obj(const char * name);

	/*
	** query if there is a render object with the specified name
	*/
	virtual bool						Render_Obj_Exists(const char * name);

	/*
	** Iterate through all render objects or through the
	** sub-categories of render objects.  NOTE! the user is responsible
	** for releasing the iterator when finished with it!
	*/
	virtual RenderObjIterator *	Create_Render_Obj_Iterator();
	virtual void						Release_Render_Obj_Iterator(RenderObjIterator *);

	/*
	** Access to HAnims, Used by Animatable3DObj's
	** TODO: make HAnims accessible from the HMODELS (or Animatable3DObj...)
	*/
	virtual Assets::AnimationAssetHandle Acquire_Animation(const char * name);

	/*
	** Access to textures
	*/
//	virtual AssetIterator *			Create_Texture_Iterator();

	HashTemplateClass<StringClass,TextureClass*>& Texture_Hash() { return TextureHash; }

	static void Log_Texture_Statistics();

	virtual TextureClass *			Get_Texture
	(
		const char * filename,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		Assets::PixelEncoding texture_format=Assets::PixelEncoding::Unknown,
		bool allow_compression=true,
		TextureBaseClass::TexAssetType type=TextureBaseClass::TEX_REGULAR,
		bool allow_reduction=true
	);

	virtual void						Release_All_Textures();
	virtual void						Release_Unused_Textures();
	virtual void						Release_Texture(TextureClass *);

	/*
	** Prepared skeleton assets used to initialize model poses
	*/
	Assets::SkeletonAssetHandle Get_Skeleton(const char * name);
	const Assets::ModelRigDesc * Resolve_Skeleton(Assets::SkeletonAssetHandle handle) const { return Skeletons.Resolve(handle); }

	/*
	** Chunk decoder functions remain registered for this manager lifetime.
	** The first registration for each chunk ID takes precedence.
	*/
	using ModelDecoder = Graphics::ModelFactory<RenderObjClass>* (*)(ChunkLoadClass&);
    void Register_Model_Decoder(int chunk_id,ModelDecoder decode);

	// The game installs the always-available NULL factory after constructing
	// the manager. Ownership remains with this manager and the reserved factory
	// is kept across Free_Assets calls. A second installation is rejected.
	bool Install_Reserved_Model_Factory(
		std::unique_ptr<Graphics::ModelFactory<RenderObjClass>> factory);

	/*
	**	The Add_Prototype is public so that we can add prototypes for procedurally
	** generated objects to the asset manager.
	*/
	void									Add_Prototype(Graphics::ModelFactory<RenderObjClass> * newproto);
	void									Remove_Prototype(Graphics::ModelFactory<RenderObjClass> *proto);
	void									Remove_Prototype(const char *name);
	Graphics::ModelFactory<RenderObjClass> *					Find_Prototype(const char * name);

	/*
	** Load on Demand
	*/
	bool	Get_WW3D_Load_On_Demand()			{ return WW3D_Load_On_Demand; }
	void	Set_WW3D_Load_On_Demand( bool on_off )	{ WW3D_Load_On_Demand = on_off; }

	/*
	** Add fog to objects on load
	*/
	bool	Get_Activate_Fog_On_Load()				{ return Activate_Fog_On_Load; }
	void	Set_Activate_Fog_On_Load( bool on_off )	{ Activate_Fog_On_Load = on_off; }

	// Log texture statistics
	void Log_All_Textures();

protected:

	void									Free();

	bool									Load_Prototype(ChunkLoadClass & cload);


	/*
	** Prototype Loaders
	** These objects are responsible for importing certain W3D chunk types and turning
	** them into prototypes.
	*/
	std::unordered_map<int,ModelDecoder> ModelDecoders;

    Graphics::ModelFactoryStore<Graphics::ModelFactory<RenderObjClass>> ModelFactories;
	std::unique_ptr<Graphics::ModelFactory<RenderObjClass>> ReservedModelFactory;

	/*
	** managers of HTrees, HAnims, Textures....
	*/
	Assets::SkeletonCache Skeletons;
	Assets::AssetReport Report;
	void Load_Animation_Chunk(ChunkLoadClass& cload);
	bool Load_Skeleton(ChunkLoadClass & cload);

	/*
	** Should .W3D be loaded if not in memory
	*/
	bool									WW3D_Load_On_Demand;

	/*
	** Should we activate fog on objects while loading them
	*/
	bool									Activate_Fog_On_Load;

	/*
	** Texture hash table for quick texture lookups
	*/
	HashTemplateClass<StringClass, TextureClass *> TextureHash;

	/*
	** The 3d asset manager is a singleton, there should be only
	** one and it is accessible through Get_Instance()
	*/
	static WW3DAssetManager *		TheInstance;

	/*
	** the iterator classes are friends
	*/
	friend class RObjIterator;
	friend class TextureIterator;

};
