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
 *                     $Archive:: /Commando/Code/ww3d2/Texture.h                              $*
 *                                                                                             *
 *                  $Org Author:: Jani_p                                                      $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 46                                                          $*
 *                                                                                             *
 * 05/16/02 KM Base texture class to abstract major texture types, e.g. 3d, z, cube, etc.
 * 06/27/02 KM Texture class abstraction																			*
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "WWLib/always.h"
#include "WWLib/chunkio.h"
import Graphics.Resources.Textures.Edit;
import Assets.Images.PixelEncoding;
import Graphics.RHI;
#include "WWLib/wwstring.h"
#include "WWMath/vector3.h"
import Graphics.Resources.Textures.Sampling;
import Graphics.Resources.Loading.Queue;
import Graphics.Resources.Recreation;

enum MipCountType
{
	MIP_LEVELS_ALL=0,		// generate all mipmap levels down to 1x1 size
	MIP_LEVELS_1,			// no mipmapping at all (just one mip level)
	MIP_LEVELS_2,
	MIP_LEVELS_3,
	MIP_LEVELS_4,
	MIP_LEVELS_5,
	MIP_LEVELS_6,
	MIP_LEVELS_7,
	MIP_LEVELS_8,
	MIP_LEVELS_10,
	MIP_LEVELS_11,
	MIP_LEVELS_12,
	MIP_LEVELS_MAX			// This isn't to be used (use MIP_LEVELS_ALL instead), it is just an enum for creating static tables etc.
};

class TextureClass;
class CubeTextureClass;
class VolumeTextureClass;

class TextureBaseClass : public RefCountClass
{






public:

	enum PoolType
	{
		POOL_DEFAULT=0,
		POOL_MANAGED,
		POOL_SYSTEMMEM
	};

	enum TexAssetType
	{
		TEX_REGULAR,
		TEX_CUBEMAP,
		TEX_VOLUME
	};

	// base constructor for derived classes
	TextureBaseClass
	(
		unsigned width,
		unsigned height,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED,
		bool rendertarget=false,
		bool reducible=true
	);

	virtual ~TextureBaseClass() override;

	virtual TexAssetType Get_Asset_Type() const=0;

	// Names
	void	Set_Texture_Name(const char * name);
	void	Set_Full_Path(const char * path)			{ FullPath = path; }
	const StringClass& Get_Texture_Name() const		{ return Name; }
	const StringClass& Get_Full_Path() const			{ if (FullPath.Is_Empty ()) return Name; return FullPath; }

	unsigned Get_ID() const { return texture_id; }	// Each textrure has a unique id

	// The number of Mip levels in the texture
	unsigned int Get_Mip_Level_Count() const
	{
		return MipLevelCount;
	}

	// Note! Width and Height may be zero and may change if texture uses mipmaps
	int Get_Width() const
	{
		return Width;
	}
	int Get_Height() const
	{
		return Height;
	}

	// Time, after which the texture is invalidated if not used. Set to zero to indicate infinite.
	// Time is in milliseconds.
	void Set_Inactivation_Time(unsigned time) { InactivationTime=time; }
	int Get_Inactivation_Time() const { return InactivationTime; }

	// Texture priority affects texture management and caching.

	// Debug utility functions for returning the texture memory usage
	virtual unsigned Get_Texture_Memory_Usage() const=0;

	bool Is_Initialized() const { return Initialized; }
	bool Is_Lightmap() const { return IsLightmap; }
	bool Is_Procedural() const { return IsProcedural; }
	bool Is_Reducible() const { return IsReducible; } //can texture be reduced in resolution for LOD purposes?

	static int _Get_Total_Locked_Surface_Size();
	static int _Get_Total_Texture_Size();
	static int _Get_Total_Lightmap_Texture_Size();
	static int _Get_Total_Procedural_Texture_Size();
	static int _Get_Total_Locked_Surface_Count();
	static int _Get_Total_Texture_Count();
	static int _Get_Total_Lightmap_Texture_Count();
	static int _Get_Total_Procedural_Texture_Count();

	virtual void Init()=0;
	const std::shared_ptr<const Graphics::ResourceLoadSource>& Loading_Source() const { return LoadSource; }

	// This utility function processes the texture reduction (used during rendering)
	void Invalidate();

	// The active renderer owns the resource represented by this opaque handle.
	Graphics::TextureResource* Peek_Render_Backend_Texture() const;
    // Borrowed graphics identity published by the current loaded resource.
    Graphics::RHITextureHandle Peek_Graphics_Texture() const;
	// Takes ownership of the supplied handle and releases the previous resource.
	void Set_Render_Backend_Texture(Graphics::TextureResource* texture);
	// Ensure that a resource used by a custom render path has been initialized.
	// Call before submitting a resource that may have been evicted or recreated.
	bool Ensure_Render_Backend_Texture();

	PoolType Get_Pool() const { return Pool; }

	bool Is_Missing_Texture();

	// Support for self managed textures
	bool Is_Dirty() { WWASSERT(Pool==POOL_DEFAULT); return Dirty; };
	void Set_Dirty() { WWASSERT(Pool==POOL_DEFAULT); Dirty=true; }
	void Clean() { Dirty=false; };

	void Set_HSV_Shift(const Vector3 &hsv_shift);
	const Vector3& Get_HSV_Shift() { return HSVShift; }

	bool Is_Compression_Allowed() const { return IsCompressionAllowed; }


	// Background texture loader will call this when texture has been loaded
	virtual void Apply_New_Surface(Graphics::TextureResource* texture, bool initialized,
		bool disable_auto_invalidation = false)=0;	// If the parameter is true, the texture will be flagged as initialised

	MipCountType MipLevelCount;

	// Inactivate textures that haven't been used in a while. Pass zero to use textures'
	// own inactive times (default). In urgent need to free up texture memory, try
	// calling with relatively small (just few seconds) time override to free up everything
	// but the currently used textures.
	static void Invalidate_Old_Unused_Textures(unsigned inactive_time_override);

	virtual TextureClass* As_TextureClass() { return nullptr; }
	virtual CubeTextureClass* As_CubeTextureClass() { return nullptr; }
	virtual VolumeTextureClass* As_VolumeTextureClass() { return nullptr; }

protected:

	void Register_For_Recreation();
	virtual bool Recreate_Procedural_Texture();
	void Set_Procedural_Texture_Recreation_Enabled(bool enabled)
	{
		ProceduralTextureRecreationEnabled = enabled;
	}


	bool Initialized;

	// For debug purposes the texture sets this true if it is a lightmap texture
	bool IsLightmap;
	bool IsCompressionAllowed;
	bool IsProcedural;
	bool IsReducible;
	bool RenderTarget;
	bool ProceduralTextureRecreationEnabled;


	unsigned InactivationTime;	// In milliseconds
	unsigned ExtendedInactivationTime;	// This is set by the engine, if needed
	unsigned LastInactivationSyncTime;
	mutable unsigned LastAccessed;

	// If this is non-zero, the texture will have a hue shift done at the next init (this
	// value should only be changed by Set_HSV_Shift() function, which also invalidates the
	// texture).
	Vector3 HSVShift;

	int Width;
	int Height;

private:

	// Opaque texture resource owned by the active render backend.
	Graphics::TextureResource* BackendTexture;
    Graphics::RHITextureHandle GraphicsTexture{};

	// Name
	StringClass Name;
	StringClass	FullPath;

	// Unique id
	unsigned texture_id;

	// Support for self-managed textures

	PoolType Pool;
	bool Dirty;

	std::shared_ptr<const Graphics::ResourceLoadSource> LoadSource;
	Graphics::ResourceRecreationRegistration RecreationRegistration;

};


/*************************************************************************
**                             TextureClass
**
** This is our regular texture class. For legacy reasons it contains some
** information beyond the graphics texture itself, such as texture addressing
** modes.
**
*************************************************************************/
class TextureClass : public TextureBaseClass
{
	W3DMPO_CODE(TextureClass)

public:

	// Create texture with desired height, width and format.
	TextureClass
	(
		unsigned width,
		unsigned height,
		Assets::PixelEncoding format,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED,
		bool rendertarget=false,
		bool allow_reduction=true
	);

	// Create texture from a file. If format is specified the texture is converted to that format.
	// Note that the format must be supported by the current device and that a texture can't exist
	// in the system with the same name in multiple formats.
	TextureClass
	(
		const char *name,
		const char *full_path=nullptr,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		Assets::PixelEncoding texture_format=Assets::PixelEncoding::Unknown,
		bool allow_compression=true,
		bool allow_reduction=true
	);

	// Create texture from a surface.
	TextureClass
	(
		Graphics::TextureEdit *surface,
		MipCountType mip_level_count=MIP_LEVELS_ALL
	);

	TextureClass(Graphics::TextureResource* texture);

	// default constructors for derived classes (cube & vol)
	TextureClass
	(
		unsigned width,
		unsigned height,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED,
		bool rendertarget=false,
		Assets::PixelEncoding format=Assets::PixelEncoding::Unknown,
		bool allow_reduction=true
	)
	: TextureBaseClass(width,height,mip_level_count,pool,rendertarget,allow_reduction), TextureFormat(format),
	  Sampling(Graphics::Make_Texture_Sampling(mip_level_count != MIP_LEVELS_1)) { }

	virtual TexAssetType Get_Asset_Type() const override { return TEX_REGULAR; }

	virtual void Init() override;

	// Background texture loader will call this when texture has been loaded
	virtual void Apply_New_Surface(Graphics::TextureResource* texture, bool initialized,
		bool disable_auto_invalidation = false);	// If the parameter is true, the texture will be flagged as initialised

	// Get the surface of one of the mipmap levels (defaults to highest-resolution one)
	Graphics::TextureEdit *Get_Surface_Level(unsigned int level = 0);
	void Get_Level_Description( Assets::ImageDescription & desc, unsigned int level = 0 );

	Graphics::TextureSampling& Get_Sampling() { return Sampling; }
	const Graphics::TextureSampling& Get_Sampling() const { return Sampling; }

	Assets::PixelEncoding Get_Texture_Format() const { return TextureFormat; }


	virtual unsigned Get_Texture_Memory_Usage() const override;

	virtual TextureClass* As_TextureClass() override { return this; }

protected:

	virtual bool Recreate_Procedural_Texture();

	Assets::PixelEncoding				TextureFormat;

	// legacy
	Graphics::TextureSampling Sampling;
};

class ZTextureClass : public TextureBaseClass
{
public:
	// Create a z texture with desired height, width and format
	ZTextureClass
	(
		unsigned width,
		unsigned height,
		Graphics::RHITextureFormat zformat,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED
	);

	Graphics::RHITextureFormat Get_Texture_Format() const { return DepthStencilTextureFormat; }

	virtual TexAssetType Get_Asset_Type() const override { return TEX_REGULAR; }

	virtual void Init() override {}

	// Background texture loader will call this when texture has been loaded
	virtual void Apply_New_Surface(Graphics::TextureResource* texture, bool initialized,
		bool disable_auto_invalidation = false);	// If the parameter is true, the texture will be flagged as initialised


	Graphics::TextureEdit *Get_Surface_Level(unsigned int level = 0);
	virtual unsigned Get_Texture_Memory_Usage() const;

protected:
	virtual bool Recreate_Procedural_Texture() override;

private:

	Graphics::RHITextureFormat DepthStencilTextureFormat;
};

class CubeTextureClass : public TextureClass
{
public:
	// Create texture with desired height, width and format.
	CubeTextureClass
	(
		unsigned width,
		unsigned height,
		Assets::PixelEncoding format,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED,
		bool rendertarget=false,
		bool allow_reduction=true
	);

	// Create texture from a file. If format is specified the texture is converted to that format.
	// Note that the format must be supported by the current device and that a texture can't exist
	// in the system with the same name in multiple formats.
	CubeTextureClass
	(
		const char *name,
		const char *full_path=nullptr,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		Assets::PixelEncoding texture_format=Assets::PixelEncoding::Unknown,
		bool allow_compression=true,
		bool allow_reduction=true
	);

	// Create texture from a surface.
	CubeTextureClass
	(
		Graphics::TextureEdit *surface,
		MipCountType mip_level_count=MIP_LEVELS_ALL
	);

	CubeTextureClass(Graphics::TextureResource* texture);

	virtual void Apply_New_Surface(Graphics::TextureResource* texture, bool initialized,
		bool disable_auto_invalidation = false);	// If the parameter is true, the texture will be flagged as initialised

	virtual TexAssetType Get_Asset_Type() const override { return TEX_CUBEMAP; }

	virtual CubeTextureClass* As_CubeTextureClass() override { return this; }

	protected:
	virtual bool Recreate_Procedural_Texture() override;

};

class VolumeTextureClass : public TextureClass
{
public:
	// Create texture with desired height, width and format.
	VolumeTextureClass
	(
		unsigned width,
		unsigned height,
		unsigned depth,
		Assets::PixelEncoding format,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		PoolType pool=POOL_MANAGED,
		bool rendertarget=false,
		bool allow_reduction=true
	);

	// Create texture from a file. If format is specified the texture is converted to that format.
	// Note that the format must be supported by the current device and that a texture can't exist
	// in the system with the same name in multiple formats.
	VolumeTextureClass
	(
		const char *name,
		const char *full_path=nullptr,
		MipCountType mip_level_count=MIP_LEVELS_ALL,
		Assets::PixelEncoding texture_format=Assets::PixelEncoding::Unknown,
		bool allow_compression=true,
		bool allow_reduction=true
	);

	// Create texture from a surface.
	VolumeTextureClass
	(
		Graphics::TextureEdit *surface,
		MipCountType mip_level_count=MIP_LEVELS_ALL
	);

	VolumeTextureClass(Graphics::TextureResource* texture);

	virtual void Apply_New_Surface(Graphics::TextureResource* texture, bool initialized,
		bool disable_auto_invalidation = false);	// If the parameter is true, the texture will be flagged as initialised

	virtual TexAssetType Get_Asset_Type() const override { return TEX_VOLUME; }

	virtual VolumeTextureClass* As_VolumeTextureClass() override { return this; }

protected:

	virtual bool Recreate_Procedural_Texture() override;

	int Depth;
};

// Utility functions for loading and saving texture descriptions from/to W3D files
TextureClass *Load_Texture(ChunkLoadClass & cload);
void Save_Texture(TextureClass * texture, ChunkSaveClass & csave);
