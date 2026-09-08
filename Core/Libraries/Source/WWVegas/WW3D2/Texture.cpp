import Graphics.Resources.Textures.Quality;
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
 *                     $Archive:: /Commando/Code/ww3d2/Texture.cpp                            $*
 *                                                                                             *
 *                  $Org Author:: Steve_t                                                     $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 85                                                          $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   FileListTextureClass::Load_Frame_Surface -- Load source texture                           *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

import Assets.Images.PixelEncoding;
import Graphics.RHI;
import Graphics.Backends.DX11.FrameRuntime;
#include <cstddef>
#include <vector>
#include <string>
#include "Texture.h"
#include "WW3D.h"
#include <WWLib/nstrdup.h>
#include "W3DFile.h"
#include "AssetMgr.h"
import Graphics.Resources.Textures.Load;
#include "WWLib/ffactory.h"
#include "WWDebug/wwprofile.h"
import Graphics.Resources.Textures.Storage;

import Assets.Adapters.W3D.Materials;

static Graphics::TextureImageReader Texture_Archive_Reader(std::string path)
{
    return [path=std::move(path)](std::size_t prefix, std::vector<std::byte>& bytes,
        std::size_t& source_size) {
        if (!_TheFileFactory || path.empty()) return false;
        file_auto_ptr file(_TheFileFactory,path.c_str());
        if (!file.get() || !file->Is_Available() || !file->Open()) return false;
        const int size = file->Size();
        if (size <= 0 || prefix > static_cast<std::size_t>(size)) return false;
        std::vector<std::byte> source(prefix ? prefix : static_cast<std::size_t>(size));
        if (file->Read(source.data(),static_cast<int>(source.size())) != static_cast<int>(source.size())) return false;
        source_size = static_cast<std::size_t>(size);
        bytes = std::move(source);
        return true;
    };
}

static std::shared_ptr<const Graphics::ResourceLoadSource> Make_Texture_Load_Source(TextureBaseClass& texture)
{
    return std::make_shared<const Graphics::ResourceLoadSource>([&texture]() -> std::unique_ptr<Graphics::ResourceLoadJob> {
        if (texture.Is_Initialized()) return {};
        Graphics::TextureLoadRequest request;
        request.device = Graphics::Shared_Frame_Device();
        if (const auto* image = texture.As_TextureClass()) request.encoding = image->Get_Texture_Format();
        else if (const auto* cube = texture.As_CubeTextureClass()) {
            request.dimension = Graphics::RHITextureDimension::Cube;
            request.encoding = cube->Get_Texture_Format();
        } else if (const auto* volume = texture.As_VolumeTextureClass()) {
            request.dimension = Graphics::RHITextureDimension::Volume;
            request.encoding = volume->Get_Texture_Format();
        } else return {};
        request.mips = {static_cast<unsigned>(texture.MipLevelCount),
            static_cast<unsigned>(Graphics::Get_Texture_Quality_Settings().mip_reduction),
            static_cast<unsigned>(Graphics::Get_Texture_Quality_Settings().minimum_dimension),texture.Is_Reducible()};
        request.prefer_16_bits = Graphics::Get_Texture_Quality_Settings().prefer_16_bits;
        request.allow_compression = texture.Is_Compression_Allowed();
        const auto& shift = texture.Get_HSV_Shift();
        request.hsv_shift = {shift.X,shift.Y,shift.Z};
        std::string path(texture.Get_Full_Path().str());
        request.read_tga = Texture_Archive_Reader(path);
        if (path.size() >= 4) {
            path.replace(path.size()-3,3,"dds");
            request.read_dds = Texture_Archive_Reader(std::move(path));
        }
        texture.Add_Ref();
        auto owner = std::shared_ptr<TextureBaseClass>(&texture,[](TextureBaseClass* value) { value->Release_Ref(); });
        return std::make_unique<Graphics::TextureLoadJob>(std::move(request),
            [owner=std::move(owner)](Graphics::TextureResource* resource) noexcept {
                if (resource) owner->Apply_New_Surface(resource,true);
                else owner->Set_Render_Backend_Texture(nullptr);
            });
    });
}

const unsigned DEFAULT_INACTIVATION_TIME=20000;

/*
** Definitions of static members:
*/

static unsigned unused_texture_id;

// This throttles submissions to the background texture loading queue.
static unsigned TexturesAppliedPerFrame;
const unsigned MAX_TEXTURES_APPLIED_PER_FRAME=2;

static unsigned Compute_Texture_Level_Size_Bytes(const Graphics::TextureResource& texture,unsigned level)
{
    const auto& description=texture.Description();
    const unsigned width=std::max(1u,description.width>>level);
    const unsigned height=std::max(1u,description.height>>level);

	switch (texture.Encoding())
	{
	case Assets::PixelEncoding::BC1:
	{
		const unsigned blocks_x = ((width + 3U) / 4U) ? ((width + 3U) / 4U) : 1U;
		const unsigned blocks_y = ((height + 3U) / 4U) ? ((height + 3U) / 4U) : 1U;
		return blocks_x * blocks_y * 8U;
	}
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
	{
		const unsigned blocks_x = ((width + 3U) / 4U) ? ((width + 3U) / 4U) : 1U;
		const unsigned blocks_y = ((height + 3U) / 4U) ? ((height + 3U) / 4U) : 1U;
		return blocks_x * blocks_y * 16U;
	}
	case Assets::PixelEncoding::Unknown:
		if (description.format == Graphics::RHITextureFormat::D16_UNorm)
		{
			return width * height * 2U;
		}
		if (description.format != Graphics::RHITextureFormat::Unknown)
		{
			return width * height * 4U;
		}
		break;
	default:
		break;
	}

	return width * height * Assets::Pixel_Size(texture.Encoding());
}


/*!
 * KM General base constructor for texture classes
 */
TextureBaseClass::TextureBaseClass
(
	unsigned int width,
	unsigned int height,
	enum MipCountType mip_level_count,
	enum PoolType pool,
	bool rendertarget,
	bool reducible
)
:	MipLevelCount(mip_level_count),
	BackendTexture(0),
	Initialized(false),
   Name(""),
	FullPath(""),
	texture_id(unused_texture_id++),
	IsLightmap(false),
	IsProcedural(false),
	IsReducible(reducible),
	RenderTarget(rendertarget),
	ProceduralTextureRecreationEnabled(false),
	IsCompressionAllowed(false),
	InactivationTime(0),
	ExtendedInactivationTime(0),
	LastInactivationSyncTime(0),
	LastAccessed(0),
	Width(width),
	Height(height),
	Pool(pool),
	Dirty(false),
	LoadSource(Make_Texture_Load_Source(*this)),
	HSVShift(0.0f,0.0f,0.0f)
{
}


//**********************************************************************************************
//! Base texture class destructor
/*! KJM
*/
TextureBaseClass::~TextureBaseClass()
{
	RecreationRegistration.Reset();
	LoadSource.reset();

	if (BackendTexture != 0)
	{
		Graphics::Release_Texture_Resource(BackendTexture);
		BackendTexture = 0;
        GraphicsTexture = {};
	}


}

void TextureBaseClass::Register_For_Recreation()
{
    RecreationRegistration=Graphics::Get_Resource_Recreation_Registry().Register(
        [this] { Set_Render_Backend_Texture(nullptr); },
        [this] { if (!Peek_Render_Backend_Texture()) Ensure_Render_Backend_Texture(); });
}

static Graphics::TextureResource* Create_Texture(unsigned width, unsigned height, Assets::PixelEncoding format,
	MipCountType mip_levels, TextureBaseClass::PoolType pool, bool render_target)
{
	Graphics::RHITexture description{width, height, static_cast<unsigned>(mip_levels)};
    if (render_target) description.usage |= static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget);
    return Graphics::TextureResource::Create(Graphics::Shared_Frame_Device(), description, format,
        render_target ? Graphics::RHITextureFormat::D24_UNorm_S8 : Graphics::RHITextureFormat::Unknown);
}

static Graphics::TextureResource* Create_Texture(Graphics::TextureEdit *surface, MipCountType mip_levels)
{
	return surface ? surface->Create_Texture(Graphics::Shared_Frame_Device(), mip_levels) : nullptr;
}

static Graphics::TextureResource* Create_ZTexture(unsigned width, unsigned height, Graphics::RHITextureFormat format,
	MipCountType mip_levels, TextureBaseClass::PoolType pool)
{
	return Graphics::TextureResource::Create(Graphics::Shared_Frame_Device(),
        {width, height, 1, format, static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)}, Assets::PixelEncoding::Unknown);
}

static Graphics::TextureResource* Create_Cube_Texture(unsigned width, unsigned height, Assets::PixelEncoding format,
	MipCountType mip_levels, TextureBaseClass::PoolType pool, bool render_target)
{
	Graphics::RHITexture description{width, height, static_cast<unsigned>(mip_levels)};
    description.dimension = Graphics::RHITextureDimension::Cube;
    description.array_size = 6;
    if (render_target) description.usage |= static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget);
    return Graphics::TextureResource::Create(Graphics::Shared_Frame_Device(), description, format,
        render_target ? Graphics::RHITextureFormat::D24_UNorm_S8 : Graphics::RHITextureFormat::Unknown);
}

static Graphics::TextureResource* Create_Volume_Texture(unsigned width, unsigned height, unsigned depth,
	Assets::PixelEncoding format, MipCountType mip_levels, TextureBaseClass::PoolType pool)
{
	Graphics::RHITexture description{width, height, static_cast<unsigned>(mip_levels)};
    description.dimension = Graphics::RHITextureDimension::Volume;
    description.depth = depth;
    return Graphics::TextureResource::Create(Graphics::Shared_Frame_Device(), description, format);
}




//**********************************************************************************************
//! Invalidate old unused textures
/*!
*/
void TextureBaseClass::Invalidate_Old_Unused_Textures(unsigned invalidation_time_override)
{
	// Texture eviction is also required when thumbnails are disabled. GeneralsMD loads
	// full-resolution managed textures in that mode, and the DX9 backend relies on this
	// path to recover memory when a new texture cannot be allocated.
	// Zero the texture apply count in this function because this is called every frame...(this wasn't in E&B main branch KJM)
	TexturesAppliedPerFrame=0;

	unsigned synctime=WW3D::Get_Sync_Time();
	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager

	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		TextureClass* tex=ite.Peek_Value();

		// Consider invalidating if texture has been initialized and defines inactivation time
		if (tex->Initialized && tex->InactivationTime)
		{
			unsigned age=synctime-tex->LastAccessed;

			if (invalidation_time_override)
			{
				if (age>invalidation_time_override)
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
			else
			{
				// Not used in the last n milliseconds?
				if (age>(tex->InactivationTime+tex->ExtendedInactivationTime))
				{
					tex->Invalidate();
					tex->LastInactivationSyncTime=synctime;
				}
			}
		}
	}
}





//**********************************************************************************************
//! Invalidate this texture
/*!
*/
void TextureBaseClass::Invalidate()
{
	if (Graphics::Get_Resource_Load_Queue().Pending(LoadSource)) {
		return;
	}

	// Don't invalidate procedural textures
	if (IsProcedural) {
		return;
	}

	if (BackendTexture != 0)
	{
		Graphics::Release_Texture_Resource(BackendTexture);
		BackendTexture = 0;
        GraphicsTexture = {};
	}

	Initialized=false;

	LastAccessed=WW3D::Get_Sync_Time();

}

//**********************************************************************************************
//! Returns the opaque texture resource owned by the backend
/*!
*/
Graphics::TextureResource* TextureBaseClass::Peek_Render_Backend_Texture() const
{
	LastAccessed=WW3D::Get_Sync_Time();
	return BackendTexture;
}

Graphics::RHITextureHandle TextureBaseClass::Peek_Graphics_Texture() const
{
    LastAccessed = WW3D::Get_Sync_Time();
    return GraphicsTexture;
}

//**********************************************************************************************
//! Replace the backend texture resource, taking ownership of the handle.
/*!
*/
void TextureBaseClass::Set_Render_Backend_Texture(Graphics::TextureResource* texture)
{
	LastAccessed=WW3D::Get_Sync_Time();
	if (BackendTexture == texture)
	{
		// A zero handle is the canonical released state.  Keep the engine-side
		// flag cleared even when the backend is asked to release an already
		// released resource.
		if (texture == 0)
		{
			Initialized = false;
		}
		return;
	}
	if (BackendTexture != 0)
	{
		Graphics::Release_Texture_Resource(BackendTexture);
	}
	BackendTexture = texture;
    GraphicsTexture=texture ? texture->Handle() : Graphics::RHITextureHandle{};
	// A zero handle means that the native resource was released.  Keep the
	// engine-side initialization bit in sync with the opaque backend handle so
	// Ensure_Render_Backend_Texture() can recreate file-backed resources after a
	// device reset instead of treating the cleared object as still initialized.
	if (texture == 0)
	{
		Initialized = false;
	}
}

//**********************************************************************************************
// Resource users request residency explicitly before submission.
bool TextureBaseClass::Ensure_Render_Backend_Texture()
{
	LastAccessed = WW3D::Get_Sync_Time();
	// A resident resource does not complete a pending full-image load.
	if (!IsProcedural && !Initialized && !Graphics::Get_Resource_Load_Queue().Pending(LoadSource))
	{
		Init();
	}
	if (BackendTexture != 0)
	{
		return true;
	}

	// A load task owns initialization while it is in flight. Starting another load here would
	// race the loader's state machine and can replace a valid pending resource.
	if (Graphics::Get_Resource_Load_Queue().Pending(LoadSource))
	{
		return false;
	}

	if (IsProcedural)
	{
		if (Recreate_Procedural_Texture())
		{
			Initialized = true;
		}
		return BackendTexture != 0;
	}

	return BackendTexture != 0;
}

bool TextureBaseClass::Recreate_Procedural_Texture()
{
	return false;
}


//**********************************************************************************************
//! Load locked surface
/*!
*/



//**********************************************************************************************
//! Is missing texture
/*!
*/
bool TextureBaseClass::Is_Missing_Texture()
{
	return BackendTexture != nullptr && BackendTexture->Is_Placeholder();
}


//**********************************************************************************************
//! Set texture name
/*!
*/
void TextureBaseClass::Set_Texture_Name(const char * name)
{
	Name=name;
}




//**********************************************************************************************
//! Get reduction mip levels
/*!
*/
// ----------------------------------------------------------------------------
// Setting HSV_Shift value is always relative to the original texture. This function invalidates the
// texture surface and causes the texture to be reloaded. For thumbnailable textures, the hue shifting
// is done in the background loading thread.
// ----------------------------------------------------------------------------
void TextureBaseClass::Set_HSV_Shift(const Vector3 &hsv_shift)
{
	Invalidate();
	HSVShift=hsv_shift;
}

//**********************************************************************************************
//! Get total locked surface size
/*! KM
*/
int TextureBaseClass::_Get_Total_Locked_Surface_Size()
{
	int total_locked_surface_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (!tex->Initialized)
		{
			total_locked_surface_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_locked_surface_size;
}

//**********************************************************************************************
//! Get total texture size
/*! KM
*/
int TextureBaseClass::_Get_Total_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		total_texture_size+=tex->Get_Texture_Memory_Usage();
	}
	return total_texture_size;
}

// ----------------------------------------------------------------------------


//**********************************************************************************************
//! Get total lightmap texture size
/*!
*/
int TextureBaseClass::_Get_Total_Lightmap_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Lightmap())
		{
			total_texture_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_texture_size;
}


//**********************************************************************************************
//! Get total procedural texture size
/*!
*/
int TextureBaseClass::_Get_Total_Procedural_Texture_Size()
{
	int total_texture_size=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (tex->Is_Procedural())
		{
			total_texture_size+=tex->Get_Texture_Memory_Usage();
		}
	}
	return total_texture_size;
}

//**********************************************************************************************
//! Get total texture count
/*!
*/
int TextureBaseClass::_Get_Total_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		texture_count++;
	}

	return texture_count;
}

// ----------------------------------------------------------------------------


//**********************************************************************************************
//! Get total light map texture count
/*!
*/
int TextureBaseClass::_Get_Total_Lightmap_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		if (ite.Peek_Value()->Is_Lightmap())
		{
			texture_count++;
		}
	}

	return texture_count;
}

//**********************************************************************************************
//! Get total procedural texture count
/*!
*/
int TextureBaseClass::_Get_Total_Procedural_Texture_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		if (ite.Peek_Value()->Is_Procedural())
		{
			texture_count++;
		}
	}

	return texture_count;
}


//**********************************************************************************************
//! Get total locked surface count
/*!
*/
int TextureBaseClass::_Get_Total_Locked_Surface_Count()
{
	int texture_count=0;

	HashTemplateIterator<StringClass,TextureClass*> ite(WW3DAssetManager::Get_Instance()->Texture_Hash());
	// Loop through all the textures in the manager
	for (ite.First ();!ite.Is_Done();ite.Next ())
	{
		// Get the current texture
		TextureBaseClass* tex=ite.Peek_Value();
		if (!tex->Initialized)
		{
			texture_count++;
		}
	}

	return texture_count;
}

/*************************************************************************
**                             TextureClass
*************************************************************************/
TextureClass::TextureClass
(
	unsigned width,
	unsigned height,
	Assets::PixelEncoding format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
:	TextureBaseClass(width, height, mip_level_count, pool, rendertarget,allow_reduction),
	Sampling(Graphics::Make_Texture_Sampling(mip_level_count != MIP_LEVELS_1)),
	TextureFormat(format)
{
	Initialized=false;
	IsProcedural=true;
	IsReducible=false;
	Set_Procedural_Texture_Recreation_Enabled(true);

	switch (format)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	Set_Render_Backend_Texture(Create_Texture(width, height, format, mip_level_count, pool, rendertarget));
	Initialized = Peek_Render_Backend_Texture() != 0;

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
	}
	// DX11 has no managed-resource pool. Every procedural texture owns a
	// native resource that must be released and recreated across a device
	// reset, including procedural textures whose legacy pool was managed.
	if (IsProcedural)
	{
		Register_For_Recreation();
	}
	LastAccessed=WW3D::Get_Sync_Time();
}



// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	Assets::PixelEncoding texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureBaseClass(0, 0, mip_level_count),
	Sampling(Graphics::Make_Texture_Sampling(mip_level_count != MIP_LEVELS_1)),
	TextureFormat(texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds
	IsReducible=allow_reduction;

	switch (TextureFormat)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	case Assets::PixelEncoding::RG8_SNorm:		// Bumpmap
	case Assets::PixelEncoding::RG5_SNorm_L6:	// Bumpmap
	case Assets::PixelEncoding::RG8_SNorm_L8X8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!WW3D::Is_Initted() || !(Graphics::Texture_Storage_Format(TextureFormat) != Graphics::RHITextureFormat::Unknown))
		{
			TextureFormat=Assets::PixelEncoding::Unknown;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Sampling.mipmap = Graphics::SamplingFilter::Disabled;
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Sampling.minification = Graphics::SamplingFilter::Fast;
			Sampling.magnification = Graphics::SamplingFilter::Fast;
			if (mip_level_count!=MIP_LEVELS_1) Sampling.mipmap = Graphics::SamplingFilter::Fast;
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Set_Render_Backend_Texture(0);
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// Prepare the full image before its first draw on the render thread.
	if (Graphics::Get_Resource_Load_Queue().Is_Owner_Thread())
	{
		Init();
	}
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass
(
	Graphics::TextureEdit *surface,
	MipCountType mip_level_count
)
:  TextureBaseClass(0,0,mip_level_count),
	Sampling(Graphics::Make_Texture_Sampling(mip_level_count != MIP_LEVELS_1)),
	TextureFormat(surface->Image().Encoding())
{
	IsProcedural=true;
	Initialized=false;
	IsReducible=false;

	Assets::ImageDescription sd;
	sd=surface->Image().Description();
	Width=sd.width;
	Height=sd.height;
	switch (sd.encoding)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	Set_Render_Backend_Texture(Create_Texture(surface, mip_level_count));
	Initialized = Peek_Render_Backend_Texture() != 0;
	LastAccessed=WW3D::Get_Sync_Time();
}

// ----------------------------------------------------------------------------
TextureClass::TextureClass(Graphics::TextureResource* texture)
:	TextureBaseClass
	(
		0,
		0,
		static_cast<MipCountType>((texture ? texture->Description().mip_count : 0))
	),
	Sampling(Graphics::Make_Texture_Sampling((texture ? texture->Description().mip_count : 0) != 1)),
	TextureFormat(Assets::PixelEncoding::Unknown)
{
	Initialized=false;
	IsProcedural=true;
	IsReducible=false;

	Set_Render_Backend_Texture(texture != 0 ? Graphics::Retain_Texture_Resource(texture) : 0);
	Initialized = Peek_Render_Backend_Texture() != 0;
	if (texture)
	{
        const auto& description=texture->Description();
		Width=static_cast<int>(description.width);
		Height=static_cast<int>(description.height);
		TextureFormat=texture->Encoding();
	}
	switch (TextureFormat)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	default: break;
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Initialise the texture
/*!
*/
bool TextureClass::Recreate_Procedural_Texture()
{
	if (!ProceduralTextureRecreationEnabled || Width <= 0 || Height <= 0 ||
		TextureFormat == Assets::PixelEncoding::Unknown)
	{
		return false;
	}

	Set_Render_Backend_Texture(Create_Texture(
		static_cast<unsigned>(Width),
		static_cast<unsigned>(Height),
		TextureFormat,
		MipLevelCount,
		Get_Pool(),
		RenderTarget));

	return Peek_Render_Backend_Texture() != 0;
}

void TextureClass::Init()
{
	if (IsProcedural)
	{
		if (!Peek_Render_Backend_Texture())
		{
			Initialized = Recreate_Procedural_Texture();
		}
		else
		{
			Initialized = true;
		}

		LastAccessed=WW3D::Get_Sync_Time();
		return;
	}

	// If the texture has already been initialised we should exit now
	if (Initialized) return;

	WWPROFILE("TextureClass::Init");

	// If the texture has recently been inactivated, increase the inactivation time (this texture obviously
	// should not have been inactivated yet).
	if (InactivationTime && LastInactivationSyncTime)
	{
		if ((WW3D::Get_Sync_Time()-LastInactivationSyncTime)<InactivationTime)
		{
			ExtendedInactivationTime=3*InactivationTime;
		}
		LastInactivationSyncTime=0;
	}


	if (!Peek_Render_Backend_Texture())
	{
		Graphics::Get_Resource_Load_Queue().Request(this->Loading_Source(), Graphics::ResourceLoadPriority::Immediate);
	}

	if (!Initialized)
	{
		Graphics::Get_Resource_Load_Queue().Request(this->Loading_Source(), Graphics::ResourceLoadPriority::Background);
	}

	LastAccessed=WW3D::Get_Sync_Time();
}

//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void TextureClass::Apply_New_Surface
(
	Graphics::TextureResource* texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	Set_Render_Backend_Texture(texture);

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_Render_Backend_Texture() != 0);
	if (initialized && texture)
	{
        const auto& description=texture->Description();
		TextureFormat=texture->Encoding();
		Width=static_cast<int>(description.width);
		Height=static_cast<int>(description.height);
	}
}



//**********************************************************************************************
//! Get surface from mip level
/*!
*/
Graphics::TextureEdit *TextureClass::Get_Surface_Level(unsigned int level)
{
	if (!Ensure_Render_Backend_Texture())
	{
		return nullptr;
	}

	Graphics::TextureResource* const texture = Peek_Render_Backend_Texture();
	if (texture == 0)
	{
		return nullptr;
	}

	auto* edit = Graphics::TextureEdit::Readback(*texture, level);
    return edit;
}

//**********************************************************************************************
//! Get surface description for a mip level
/*!
*/
void TextureClass::Get_Level_Description(Assets::ImageDescription& desc, unsigned int level)
{
    desc = {};
    if (!Ensure_Render_Backend_Texture()) return;
    const auto* texture = Peek_Render_Backend_Texture();
    if (!texture || level >= texture->Description().mip_count) return;
    desc = {texture->Encoding(), std::max(1u, texture->Description().width >> level),
        std::max(1u, texture->Description().height >> level)};
}

//**********************************************************************************************
//! Get texture memory usage
/*!
*/
unsigned TextureClass::Get_Texture_Memory_Usage() const
{
    const auto* texture=Peek_Render_Backend_Texture();
    if (!texture) return 0;
    unsigned size=0;
    for (unsigned level=0;level<texture->Description().mip_count;++level)
        size+=Compute_Texture_Level_Size_Bytes(*texture,level);
    return size;
}


// Utility functions
TextureClass* Load_Texture(ChunkLoadClass &cload)
{
    if (!cload.Open_Chunk()) return nullptr;
    if (cload.Cur_Chunk_ID() != W3D_CHUNK_TEXTURE) {
        cload.Close_Chunk();
        return nullptr;
    }
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    const bool read = cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) == bytes.size();
    cload.Close_Chunk();
    Assets::W3D::W3DTextureData decoded;
    if (!read || !Assets::W3D::W3DRead_Texture(bytes,decoded)) return nullptr;
    TextureClass *newtex = nullptr;
    if (decoded.has_info)
    {

        MipCountType mipcount;

        bool no_lod = ((decoded.attributes & W3DTEXTURE_NO_LOD) == W3DTEXTURE_NO_LOD);

        if (no_lod)
        {
            mipcount = MIP_LEVELS_1;
        }
        else
        {
            switch (decoded.attributes & W3DTEXTURE_MIP_LEVELS_MASK) {

                case W3DTEXTURE_MIP_LEVELS_ALL:
                    mipcount = MIP_LEVELS_ALL;
                    break;

                case W3DTEXTURE_MIP_LEVELS_2:
                    mipcount = MIP_LEVELS_2;
                    break;

                case W3DTEXTURE_MIP_LEVELS_3:
                    mipcount = MIP_LEVELS_3;
                    break;

                case W3DTEXTURE_MIP_LEVELS_4:
                    mipcount = MIP_LEVELS_4;
                    break;

                default:
                    WWASSERT (false);
                    mipcount = MIP_LEVELS_ALL;
                    break;
            }
        }

        Assets::PixelEncoding format=Assets::PixelEncoding::Unknown;

        switch (decoded.attributes & W3DTEXTURE_TYPE_MASK)
        {

            case W3DTEXTURE_TYPE_COLORMAP:
                // Do nothing.
                break;

            case W3DTEXTURE_TYPE_BUMPMAP:
            {
                if (WW3D::Is_Initted())
                {
                    // No mipmaps to bumpmap for now
                    mipcount=MIP_LEVELS_1;

                    format=Assets::PixelEncoding::RG8_SNorm;
                }
                break;
            }

            default:
                WWASSERT (false);
                break;
        }

        newtex = WW3DAssetManager::Get_Instance()->Get_Texture (decoded.name.c_str(), mipcount, format);

        if (no_lod)
        {
            newtex->Get_Sampling().mipmap = Graphics::SamplingFilter::Disabled;
        }
        bool u_clamp = ((decoded.attributes & W3DTEXTURE_CLAMP_U) != 0);
        newtex->Get_Sampling().address[0] = u_clamp ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;
        bool v_clamp = ((decoded.attributes & W3DTEXTURE_CLAMP_V) != 0);
        newtex->Get_Sampling().address[1] = v_clamp ? Graphics::RHISamplerAddress::Clamp : Graphics::RHISamplerAddress::Wrap;

    } else
    {
        newtex = WW3DAssetManager::Get_Instance()->Get_Texture(decoded.name.c_str());
    }

    WWASSERT(newtex);
    return newtex;
}

// Utility function used by Save_Texture
void setup_texture_attributes(TextureClass * tex, W3dTextureInfoStruct * texinfo)
{
	texinfo->Attributes = 0;

	if (tex->Get_Sampling().mipmap == Graphics::SamplingFilter::Disabled) texinfo->Attributes |= W3DTEXTURE_NO_LOD;
	if (tex->Get_Sampling().address[0] == Graphics::RHISamplerAddress::Clamp) texinfo->Attributes |= W3DTEXTURE_CLAMP_U;
	if (tex->Get_Sampling().address[1] == Graphics::RHISamplerAddress::Clamp) texinfo->Attributes |= W3DTEXTURE_CLAMP_V;
}


void Save_Texture(TextureClass * texture,ChunkSaveClass & csave)
{
	const char * filename;
	W3dTextureInfoStruct texinfo;
	memset(&texinfo,0,sizeof(texinfo));

	filename = texture->Get_Full_Path();

	setup_texture_attributes(texture, &texinfo);

	csave.Begin_Chunk(W3D_CHUNK_TEXTURE_NAME);
	csave.Write(filename,strlen(filename)+1);
	csave.End_Chunk();

	if ((texinfo.Attributes != 0) || (texinfo.AnimType != 0) || (texinfo.FrameCount != 0)) {
		csave.Begin_Chunk(W3D_CHUNK_TEXTURE_INFO);
		csave.Write(&texinfo, sizeof(texinfo));
		csave.End_Chunk();
	}
}


/*!
 *	KJM depth stencil texture constructor
 */
ZTextureClass::ZTextureClass
(
	unsigned width,
	unsigned height,
	Graphics::RHITextureFormat zformat,
	MipCountType mip_level_count,
	PoolType pool
)
:	TextureBaseClass(width,height, mip_level_count, pool),
	DepthStencilTextureFormat(zformat)
{
	Set_Render_Backend_Texture(Create_ZTexture(width, height, zformat, mip_level_count, pool));
	Initialized = Peek_Render_Backend_Texture() != 0;

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		Register_For_Recreation();
	}
	Initialized = Peek_Render_Backend_Texture() != 0;
	IsProcedural=true;
	IsReducible=false;
	Set_Procedural_Texture_Recreation_Enabled(true);

	LastAccessed=WW3D::Get_Sync_Time();
}

bool ZTextureClass::Recreate_Procedural_Texture()
{
	if (!ProceduralTextureRecreationEnabled || Width <= 0 || Height <= 0 ||
		DepthStencilTextureFormat == Graphics::RHITextureFormat::Unknown)
	{
		return false;
	}

	Set_Render_Backend_Texture(Create_ZTexture(
		static_cast<unsigned>(Width),
		static_cast<unsigned>(Height),
		DepthStencilTextureFormat,
		MipLevelCount,
		Get_Pool()));

	return Peek_Render_Backend_Texture() != 0;
}



//**********************************************************************************************
//! Apply new surface to texture
/*! KM
*/
void ZTextureClass::Apply_New_Surface
(
	Graphics::TextureResource* texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	Set_Render_Backend_Texture(texture);

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_Render_Backend_Texture() != 0);
	if (initialized && texture)
	{
        const auto& description=texture->Description();
		DepthStencilTextureFormat=description.format;
		Width=static_cast<int>(description.width);
		Height=static_cast<int>(description.height);
	}
}

//**********************************************************************************************
//! Get surface from mip level
/*!
*/
Graphics::TextureEdit *ZTextureClass::Get_Surface_Level(unsigned int level)
{
	if (!Ensure_Render_Backend_Texture())
	{
		return nullptr;
	}

	Graphics::TextureResource* const texture = Peek_Render_Backend_Texture();
	if (texture == 0)
	{
		return nullptr;
	}

	auto* edit = Graphics::TextureEdit::Readback(*texture, level);
    return edit;
}

//**********************************************************************************************
//! Get texture memory usage
/*!
*/
unsigned ZTextureClass::Get_Texture_Memory_Usage() const
{
    const auto* texture=Peek_Render_Backend_Texture();
    if (!texture) return 0;
    unsigned size=0;
    for (unsigned level=0;level<texture->Description().mip_count;++level)
        size+=Compute_Texture_Level_Size_Bytes(*texture,level);
    return size;
}



/*************************************************************************
**                             CubeTextureClass
*************************************************************************/
CubeTextureClass::CubeTextureClass
(
	unsigned width,
	unsigned height,
	Assets::PixelEncoding format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, mip_level_count, pool, rendertarget, format, allow_reduction)
{
	IsProcedural=true;
	IsReducible=false;
	Set_Procedural_Texture_Recreation_Enabled(true);

	switch (format)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	Set_Render_Backend_Texture(Create_Cube_Texture(width, height, format, mip_level_count,
		pool, rendertarget));
	Initialized = Peek_Render_Backend_Texture() != 0;

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		Register_For_Recreation();
	}
	LastAccessed=WW3D::Get_Sync_Time();
}


bool CubeTextureClass::Recreate_Procedural_Texture()
{
	if (!ProceduralTextureRecreationEnabled || Width <= 0 || Height <= 0 ||
		TextureFormat == Assets::PixelEncoding::Unknown)
	{
		return false;
	}

	Set_Render_Backend_Texture(Create_Cube_Texture(
		static_cast<unsigned>(Width),
		static_cast<unsigned>(Height),
		TextureFormat,
		MipLevelCount,
		Get_Pool(),
		RenderTarget));

	return Peek_Render_Backend_Texture() != 0;
}



// ----------------------------------------------------------------------------
CubeTextureClass::CubeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	Assets::PixelEncoding texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	case Assets::PixelEncoding::RG8_SNorm:		// Bumpmap
	case Assets::PixelEncoding::RG5_SNorm_L6:	// Bumpmap
	case Assets::PixelEncoding::RG8_SNorm_L8X8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!WW3D::Is_Initted() || !(Graphics::Texture_Storage_Format(TextureFormat) != Graphics::RHITextureFormat::Unknown))
		{
			TextureFormat=Assets::PixelEncoding::Unknown;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Sampling.mipmap = Graphics::SamplingFilter::Disabled;
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Sampling.minification = Graphics::SamplingFilter::Fast;
			Sampling.magnification = Graphics::SamplingFilter::Fast;
			if (mip_level_count!=MIP_LEVELS_1) Sampling.mipmap = Graphics::SamplingFilter::Fast;
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Set_Render_Backend_Texture(0);
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// Prepare the full image before its first draw on the render thread.
	if (Graphics::Get_Resource_Load_Queue().Is_Owner_Thread())
	{
		Init();
	}
}

//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void CubeTextureClass::Apply_New_Surface
(
	Graphics::TextureResource* texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	Set_Render_Backend_Texture(texture);

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_Render_Backend_Texture() != 0);
	if (initialized && texture)
	{
        const auto& description=texture->Description();
		TextureFormat=texture->Encoding();
		Width=static_cast<int>(description.width);
		Height=static_cast<int>(description.height);
	}
}


/*************************************************************************
**                             VolumeTextureClass
*************************************************************************/
VolumeTextureClass::VolumeTextureClass
(
	unsigned width,
	unsigned height,
	unsigned depth,
	Assets::PixelEncoding format,
	MipCountType mip_level_count,
	PoolType pool,
	bool rendertarget,
	bool allow_reduction
)
: TextureClass(width, height, mip_level_count, pool, rendertarget, format, allow_reduction),
  Depth(depth)
{
	IsProcedural=true;
	IsReducible=false;
	Set_Procedural_Texture_Recreation_Enabled(true);

	switch (format)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	default : break;
	}

	Set_Render_Backend_Texture(Create_Volume_Texture(width, height, depth, format,
		mip_level_count, pool));
	Initialized = Peek_Render_Backend_Texture() != 0;

	if (pool==POOL_DEFAULT)
	{
		Set_Dirty();
		Register_For_Recreation();
	}
	LastAccessed=WW3D::Get_Sync_Time();
}


bool VolumeTextureClass::Recreate_Procedural_Texture()
{
	if (!ProceduralTextureRecreationEnabled || Width <= 0 || Height <= 0 ||
		Depth <= 0 || TextureFormat == Assets::PixelEncoding::Unknown)
	{
		return false;
	}

	Set_Render_Backend_Texture(Create_Volume_Texture(
		static_cast<unsigned>(Width),
		static_cast<unsigned>(Height),
		static_cast<unsigned>(Depth),
		TextureFormat,
		MipLevelCount,
		Get_Pool()));

	return Peek_Render_Backend_Texture() != 0;
}



// ----------------------------------------------------------------------------
VolumeTextureClass::VolumeTextureClass
(
	const char *name,
	const char *full_path,
	MipCountType mip_level_count,
	Assets::PixelEncoding texture_format,
	bool allow_compression,
	bool allow_reduction
)
:	TextureClass(0,0,mip_level_count, POOL_MANAGED, false, texture_format),
	Depth(0)
{
	IsCompressionAllowed=allow_compression;
	InactivationTime=DEFAULT_INACTIVATION_TIME;		// Default inactivation time 30 seconds

	switch (TextureFormat)
	{
	case Assets::PixelEncoding::BC1:
	case Assets::PixelEncoding::BC2Premultiplied:
	case Assets::PixelEncoding::BC2:
	case Assets::PixelEncoding::BC3Premultiplied:
	case Assets::PixelEncoding::BC3:
		IsCompressionAllowed=true;
		break;
	case Assets::PixelEncoding::RG8_SNorm:		// Bumpmap
	case Assets::PixelEncoding::RG5_SNorm_L6:	// Bumpmap
	case Assets::PixelEncoding::RG8_SNorm_L8X8:	// Bumpmap
		// If requesting bumpmap format that isn't available we'll just return the surface in whatever color
		// format the texture file is in. (This is illegal case, the format support should always be queried
		// before creating a bump texture!)
		if (!WW3D::Is_Initted() || !(Graphics::Texture_Storage_Format(TextureFormat) != Graphics::RHITextureFormat::Unknown))
		{
			TextureFormat=Assets::PixelEncoding::Unknown;
		}
		// If bump format is valid, make sure compression is not allowed so that we don't even attempt to load
		// from a compressed file (quality isn't good enough for bump map). Also disable mipmapping.
		else
		{
			IsCompressionAllowed=false;
			MipLevelCount=MIP_LEVELS_1;
			Sampling.mipmap = Graphics::SamplingFilter::Disabled;
		}
		break;
	default:	break;
	}

	WWASSERT_PRINT(name && name[0], "TextureClass CTor: null or empty texture name");
	int len=strlen(name);
	for (int i=0;i<len;++i)
	{
		if (name[i]=='+')
		{
			IsLightmap=true;

			// Set bilinear filtering for lightmaps (they are very stretched and
			// low detail so we don't care for anisotropic or trilinear filtering...)
			Sampling.minification = Graphics::SamplingFilter::Fast;
			Sampling.magnification = Graphics::SamplingFilter::Fast;
			if (mip_level_count!=MIP_LEVELS_1) Sampling.mipmap = Graphics::SamplingFilter::Fast;
			break;
		}
	}
	Set_Texture_Name(name);
	Set_Full_Path(full_path);
	WWASSERT(name[0]!='\0');
	if (!WW3D::Is_Texturing_Enabled())
	{
		Initialized=true;
		Set_Render_Backend_Texture(0);
	}

	LastAccessed=WW3D::Get_Sync_Time();

	// Prepare the full image before its first draw on the render thread.
	if (Graphics::Get_Resource_Load_Queue().Is_Owner_Thread())
	{
		Init();
	}
}

//**********************************************************************************************
//! Apply new surface to texture
/*!
*/
void VolumeTextureClass::Apply_New_Surface
(
	Graphics::TextureResource* texture,
	bool initialized,
	bool disable_auto_invalidation
)
{
	Set_Render_Backend_Texture(texture);

	if (initialized) Initialized=true;
	if (disable_auto_invalidation) InactivationTime = 0;

	WWASSERT(Peek_Render_Backend_Texture() != 0);
	if (initialized && texture)
	{
        const auto& description=texture->Description();
		TextureFormat=texture->Encoding();
		Width=static_cast<int>(description.width);
		Height=static_cast<int>(description.height);
		Depth=static_cast<int>(description.depth);
	}
}
