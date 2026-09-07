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
 *                     $Archive:: /Commando/Code/ww3d2/VertMaterial.cpp                       $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 8/22/01 11:06a                                              $*
 *                                                                                             *
 *                    $Revision:: 42                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Init -- init code                                                                         *
 *   Shutdown -- shutdown code                                                                 *
 *   Get_Preset -- retrieve presets                                                            *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include <cstddef>
#include <string>
#include <vector>
#include "VertMaterial.h"
#include "WWLib/realcrc.h"
#include "WWDebug/wwdebug.h"
#include "WWLib/chunkio.h"
#include "W3DErr.h"
#include "WWLib/INI.h"
#include "WWLib/XSTRAW.h"
#include "WW3D.h"

import Assets.Adapters.W3D.Materials;


static unsigned int unique=1;

VertexMaterialClass* VertexMaterialClass::Presets[VertexMaterialClass::PRESET_COUNT];

/*
** VertexMaterialClass Implementation
*/
VertexMaterialClass::VertexMaterialClass():
	Material{},
	Flags(0),
	UniqueID(0),
	CRCDirty(true)
{
	int i;

	for (i=0; i<Graphics::Material::TextureSlotCount; i++)
	{
		Mapper[i]=nullptr;
		UVSource[i] = i;
	}
	Set_Ambient(1.0f,1.0f,1.0f);
	Set_Diffuse(1.0f,1.0f,1.0f);

	Set_Opacity(1.0f);
	Set_Shininess(0.0f);
}

VertexMaterialClass::VertexMaterialClass(const VertexMaterialClass & src) :
	Material(src.Material),
	Flags(src.Flags),
	Name(src.Name),
	UniqueID(src.UniqueID),
	CRCDirty(true)
{
	int i;
	for (i=0; i<Graphics::Material::TextureSlotCount; i++)
	{
		Mapper[i]=nullptr;
		if (src.Mapper[i])
		{
			TextureMapperClass *mapper=src.Mapper[i]->Clone();
			Set_Mapper(mapper,i);
			mapper->Release_Ref();
		}

		UVSource[i] = src.UVSource[i];
	}
}

void VertexMaterialClass::Make_Unique()
{
	CRCDirty=true;
	UniqueID=unique;
	unique++;
}

VertexMaterialClass::~VertexMaterialClass()
{
	int i;

	for (i=0; i<Graphics::Material::TextureSlotCount; i++)
	{
		if (Mapper[i])
		{
			REF_PTR_RELEASE(Mapper[i]);
			Mapper[i]=nullptr;
		}
	}
}

VertexMaterialClass & VertexMaterialClass::operator = (const VertexMaterialClass &src)
{

	if (this != &src) {
		Name=src.Name;
		Flags = src.Flags;
		UniqueID=src.UniqueID;
		CRCDirty=src.CRCDirty;
		int stage;
		for (stage=0;stage<Graphics::Material::TextureSlotCount;++stage) {
			if (Mapper[stage] != nullptr) {
				Mapper[stage]->Release_Ref();
				Mapper[stage] = nullptr;
			}
		}
		for (stage=0;stage<Graphics::Material::TextureSlotCount;++stage) {
			if (src.Mapper[stage]) {
				TextureMapperClass *mapper = src.Mapper[stage]->Clone();
				Set_Mapper(mapper,stage);
				mapper->Release_Ref();
			}
			UVSource[stage] = src.UVSource[stage];
		}

		Material = src.Material;
	}
	return *this;
}

unsigned long VertexMaterialClass::Compute_CRC() const
{
	unsigned long crc = 0;

// don't include the name when determining whether two vertex materials match
//	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(Name.Peek_Buffer()),sizeof(char)*strlen(Name),crc);

	// Keep the authored material key independent of struct padding and graphics
	// storage layout. Unused color alpha channels have always been zero.
	const float values[]{
		Material.diffuse[0],Material.diffuse[1],Material.diffuse[2],Material.opacity,
		Material.ambient[0],Material.ambient[1],Material.ambient[2],0,
		Material.specular[0],Material.specular[1],Material.specular[2],0,
		Material.emissive[0],Material.emissive[1],Material.emissive[2],0,
		Material.shininess};
	const auto diffuse_source=static_cast<ColorSourceType>(Material.diffuse_source);
	const auto ambient_source=static_cast<ColorSourceType>(Material.ambient_source);
	const auto emissive_source=static_cast<ColorSourceType>(Material.emissive_source);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(values),sizeof(values),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&Flags),sizeof(Flags),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&diffuse_source),sizeof(diffuse_source),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&ambient_source),sizeof(ambient_source),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&emissive_source),sizeof(emissive_source),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&UVSource),sizeof(UVSource),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&Material.lighting),sizeof(Material.lighting),crc);
	crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&UniqueID),sizeof(UniqueID),crc);

	int i;
	for (i=0; i<Graphics::Material::TextureSlotCount; i++)
	{
		if (Mapper[i]) crc = CRC_Memory(reinterpret_cast<const unsigned char *>(&(Mapper[i])),sizeof(TextureMapperClass*),crc);
	}

	return crc;
}

// Ambient Get and Sets

void VertexMaterialClass::Get_Ambient(Vector3 * set) const
{
	assert(set);
	*set=Vector3(Material.ambient[0],Material.ambient[1],Material.ambient[2]);
}

void VertexMaterialClass::Set_Ambient(const Vector3 & color)
{
	CRCDirty=true;
	Material.ambient[0]=color.X;
	Material.ambient[1]=color.Y;
	Material.ambient[2]=color.Z;
}

void VertexMaterialClass::Set_Ambient(float r,float g,float b)
{
	CRCDirty=true;
	Material.ambient[0]=r;
	Material.ambient[1]=g;
	Material.ambient[2]=b;
}

// Diffuse Get and Sets

void VertexMaterialClass::Get_Diffuse(Vector3 * set) const
{
	assert(set);
	*set=Vector3(Material.diffuse[0],Material.diffuse[1],Material.diffuse[2]);
}

void VertexMaterialClass::Set_Diffuse(const Vector3 & color)
{
	CRCDirty=true;
	Material.diffuse[0]=color.X;
	Material.diffuse[1]=color.Y;
	Material.diffuse[2]=color.Z;
}

void VertexMaterialClass::Set_Diffuse(float r,float g,float b)
{
	CRCDirty=true;
	Material.diffuse[0]=r;
	Material.diffuse[1]=g;
	Material.diffuse[2]=b;
}

// Specular Get and Sets

void VertexMaterialClass::Get_Specular(Vector3 * set) const
{
	assert(set);
	*set=Vector3(Material.specular[0],Material.specular[1],Material.specular[2]);
}

void VertexMaterialClass::Set_Specular(const Vector3 & color)
{
	CRCDirty=true;
	Material.specular[0]=color.X;
	Material.specular[1]=color.Y;
	Material.specular[2]=color.Z;
}

void VertexMaterialClass::Set_Specular(float r,float g,float b)
{
	CRCDirty=true;
	Material.specular[0]=r;
	Material.specular[1]=g;
	Material.specular[2]=b;
}

// Emissive Get and Sets

void VertexMaterialClass::Get_Emissive(Vector3 * set) const
{
	assert(set);
	*set=Vector3(Material.emissive[0],Material.emissive[1],Material.emissive[2]);
}

void VertexMaterialClass::Set_Emissive(const Vector3 & color)
{
	CRCDirty=true;
	Material.emissive[0]=color.X;
	Material.emissive[1]=color.Y;
	Material.emissive[2]=color.Z;
}

void VertexMaterialClass::Set_Emissive(float r,float g,float b)
{
	CRCDirty=true;
	Material.emissive[0]=r;
	Material.emissive[1]=g;
	Material.emissive[2]=b;
}


float	VertexMaterialClass::Get_Shininess() const
{
	return Material.shininess;
}

void	VertexMaterialClass::Set_Shininess(float shin)
{
	CRCDirty=true;
	Material.shininess=shin;
}

float	VertexMaterialClass::Get_Opacity() const
{
	return Material.opacity;
}

void	VertexMaterialClass::Set_Opacity(float o)
{
	CRCDirty=true;
	Material.opacity=o;
}

void	VertexMaterialClass::Set_Ambient_Color_Source(ColorSourceType src)
{
	CRCDirty=true;
	Material.ambient_source = static_cast<Graphics::PropColorSource>(src);
}

void	VertexMaterialClass::Set_Emissive_Color_Source(ColorSourceType src)
{
	CRCDirty=true;
	Material.emissive_source = static_cast<Graphics::PropColorSource>(src);
}

void	VertexMaterialClass::Set_Diffuse_Color_Source(ColorSourceType src)
{
	CRCDirty=true;
	Material.diffuse_source = static_cast<Graphics::PropColorSource>(src);
}

VertexMaterialClass::ColorSourceType
VertexMaterialClass::Get_Ambient_Color_Source()
{
	return static_cast<ColorSourceType>(Material.ambient_source);
}

VertexMaterialClass::ColorSourceType
VertexMaterialClass::Get_Emissive_Color_Source()
{
	return static_cast<ColorSourceType>(Material.emissive_source);
}

VertexMaterialClass::ColorSourceType
VertexMaterialClass::Get_Diffuse_Color_Source()
{
	return static_cast<ColorSourceType>(Material.diffuse_source);
}

void VertexMaterialClass::Set_UV_Source(int stage,int array_index)
{
	WWASSERT(stage >= 0);
	WWASSERT(stage < Graphics::Material::TextureSlotCount);
	WWASSERT(array_index >= 0);
	WWASSERT(array_index < 8);
	CRCDirty=true;
	UVSource[stage] = array_index;
}

int VertexMaterialClass::Get_UV_Source(int stage)
{
	WWASSERT(stage >= 0);
	WWASSERT(stage < Graphics::Material::TextureSlotCount);
	return UVSource[stage];
}


void VertexMaterialClass::Init_From_Material3(const W3dMaterial3Struct & mat3)
{
	Vector3 tmp0,tmp1,tmp2;

	tmp0.X = static_cast<float>(mat3.DiffuseColor.R) / 255.0f;
	tmp0.Y = static_cast<float>(mat3.DiffuseColor.G) / 255.0f;
	tmp0.Z = static_cast<float>(mat3.DiffuseColor.B) / 255.0f;
	tmp1.X = static_cast<float>(mat3.DiffuseCoefficients.R) / 255.0f;
	tmp1.Y = static_cast<float>(mat3.DiffuseCoefficients.G) / 255.0f;
	tmp1.Z = static_cast<float>(mat3.DiffuseCoefficients.B) / 255.0f;
	tmp2.X = tmp0.X * tmp1.X;
	tmp2.Y = tmp0.Y * tmp1.Y;
	tmp2.Z = tmp0.Z * tmp1.Z;
	Set_Diffuse(tmp2);

	tmp0.X = static_cast<float>(mat3.SpecularColor.R) / 255.0f;
	tmp0.Y = static_cast<float>(mat3.SpecularColor.G) / 255.0f;
	tmp0.Z = static_cast<float>(mat3.SpecularColor.B) / 255.0f;
	tmp1.X = static_cast<float>(mat3.SpecularCoefficients.R) / 255.0f;
	tmp1.Y = static_cast<float>(mat3.SpecularCoefficients.G) / 255.0f;
	tmp1.Z = static_cast<float>(mat3.SpecularCoefficients.B) / 255.0f;
	tmp2.X = tmp0.X * tmp1.X;
	tmp2.Y = tmp0.Y * tmp1.Y;
	tmp2.Z = tmp0.Z * tmp1.Z;
	Set_Specular(tmp2);

	tmp0.X = static_cast<float>(mat3.EmissiveCoefficients.R) / 255.0f;
	tmp0.Y = static_cast<float>(mat3.EmissiveCoefficients.G) / 255.0f;
	tmp0.Z = static_cast<float>(mat3.EmissiveCoefficients.B) / 255.0f;
	Set_Emissive(tmp0);

	tmp0.X = static_cast<float>(mat3.AmbientCoefficients.R) / 255.0f;
	tmp0.Y = static_cast<float>(mat3.AmbientCoefficients.G) / 255.0f;
	tmp0.Z = static_cast<float>(mat3.AmbientCoefficients.B) / 255.0f;
	Set_Ambient(tmp0);

	Set_Shininess(mat3.Shininess);
	Set_Opacity(mat3.Opacity);
}

WW3DErrorType VertexMaterialClass::Load_W3D(ChunkLoadClass &cload)
{
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if (cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) != bytes.size())
        return WW3D_ERROR_LOAD_FAILED;
    Assets::W3D::W3DVertexMaterialData decoded;
    if (!Assets::W3D::W3DRead_Vertex_Material(bytes, decoded))
        return WW3D_ERROR_LOAD_FAILED;
    const auto &material = decoded.material;
    if (decoded.has_name) Set_Name(material.name.c_str());
    Set_Ambient(Vector3(material.ambient_color.r, material.ambient_color.g, material.ambient_color.b));
    Set_Diffuse(Vector3(material.base_color.r, material.base_color.g, material.base_color.b));
    Set_Specular(Vector3(material.specular_color.r, material.specular_color.g, material.specular_color.b));
    Set_Emissive(Vector3(material.emissive_color.r, material.emissive_color.g, material.emissive_color.b));
    Set_Shininess(material.shininess);
    Set_Opacity(material.opacity);
    if (material.source_attributes & W3DVERTMAT_USE_DEPTH_CUE) Set_Flag(DEPTH_CUE,true);
    if (material.source_attributes & W3DVERTMAT_COPY_SPECULAR_TO_DIFFUSE) Set_Flag(COPY_SPECULAR_TO_DIFFUSE,true);
    Apply_Mappers(material.source_attributes, decoded.mapper_arguments[0].c_str(), decoded.mapper_arguments[1].c_str());
    return WW3D_ERROR_OK;
}

void VertexMaterialClass::Apply_Mappers(unsigned attributes, const char *arguments0, const char *arguments1)
{
    const auto load_arguments = [](const char *arguments, INIClass &ini) {
        if (!arguments || !*arguments) return;
        std::string section = "[Args]\n";
        section += arguments;
        BufferStraw source(section.data(), static_cast<int>(section.size()+1));
        ini.Load(source);
    };
    INIClass mapping0_arg_ini, mapping1_arg_ini;
    load_arguments(arguments0, mapping0_arg_ini);
    load_arguments(arguments1, mapping1_arg_ini);

	// Set up the vertex mapper.  If it is one of the simple
	// ones, set the pointer to one of the global instances.
	int mapping = attributes & W3DVERTMAT_STAGE0_MAPPING_MASK;

	switch(mapping) {

		case W3DVERTMAT_STAGE0_MAPPING_UV:
			break;

		case W3DVERTMAT_STAGE0_MAPPING_ENVIRONMENT:
			{
				EnvironmentMapperClass *mapper = NEW_REF(EnvironmentMapperClass,(0));
				Set_Mapper(mapper);
				mapper->Release_Ref();
			}
			break;
		case W3DVERTMAT_STAGE0_MAPPING_CHEAP_ENVIRONMENT:
			{
				ClassicEnvironmentMapperClass *mapper = NEW_REF(ClassicEnvironmentMapperClass,(0));
				Set_Mapper(mapper);
				mapper->Release_Ref();
			}
			break;
		case W3DVERTMAT_STAGE0_MAPPING_LINEAR_OFFSET:
			{
				LinearOffsetTextureMapperClass *mapper =
					NEW_REF(LinearOffsetTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_SCREEN:
			{
				ScreenMapperClass *mapper =
					NEW_REF(ScreenMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_SCALE:
			{
				ScaleTextureMapperClass *mapper =
					NEW_REF(ScaleTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_GRID:
			{
				GridTextureMapperClass *mapper =
					NEW_REF(GridTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_ROTATE:
			{
				RotateTextureMapperClass *mapper =
					NEW_REF(RotateTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_SINE_LINEAR_OFFSET:
			{
				SineLinearOffsetTextureMapperClass *mapper =
					NEW_REF(SineLinearOffsetTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_STEP_LINEAR_OFFSET:
			{
				StepLinearOffsetTextureMapperClass *mapper =
					NEW_REF(StepLinearOffsetTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_ZIGZAG_LINEAR_OFFSET:
			{
				ZigZagLinearOffsetTextureMapperClass *mapper =
					NEW_REF(ZigZagLinearOffsetTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_WS_CLASSIC_ENV:
			{
				WSClassicEnvironmentMapperClass *mapper = NEW_REF(WSClassicEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_WS_ENVIRONMENT:
			{
				WSEnvironmentMapperClass *mapper = NEW_REF(WSEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_GRID_CLASSIC_ENV:
			{
				GridClassicEnvironmentMapperClass *mapper =
					NEW_REF(GridClassicEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_GRID_ENVIRONMENT:
			{
				GridEnvironmentMapperClass *mapper =
					NEW_REF(GridEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_RANDOM:
			{
				RandomTextureMapperClass *mapper =
					NEW_REF(RandomTextureMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_EDGE:
		{
			EdgeMapperClass *mapper =
				NEW_REF(EdgeMapperClass,(mapping0_arg_ini, "Args", 0));
			Set_Mapper(mapper,0);
			mapper->Release_Ref();
		}
		break;

		case W3DVERTMAT_STAGE0_MAPPING_BUMPENV:
		{
			BumpEnvTextureMapperClass *mapper =
				NEW_REF(BumpEnvTextureMapperClass,(mapping0_arg_ini, "Args", 0));
			Set_Mapper(mapper,0);
			mapper->Release_Ref();
		}
		break;

		case W3DVERTMAT_STAGE0_MAPPING_GRID_WS_CLASSIC_ENV:
			{
				GridWSClassicEnvironmentMapperClass *mapper =
					NEW_REF(GridWSClassicEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE0_MAPPING_GRID_WS_ENVIRONMENT:
			{
				GridWSEnvironmentMapperClass *mapper =
					NEW_REF(GridWSEnvironmentMapperClass,(mapping0_arg_ini, "Args", 0));
				Set_Mapper(mapper,0);
				mapper->Release_Ref();
			}
			break;

		default:
			break;
	}

	// Same setup for stage 1's mapper.
	mapping = attributes & W3DVERTMAT_STAGE1_MAPPING_MASK;
	switch(mapping) {

		case W3DVERTMAT_STAGE1_MAPPING_UV:
			break;

		case W3DVERTMAT_STAGE1_MAPPING_ENVIRONMENT:
		{
			EnvironmentMapperClass *mapper = W3DNEW EnvironmentMapperClass(1);
			Set_Mapper(mapper, 1);
			mapper->Release_Ref();
		}
		break;
		case W3DVERTMAT_STAGE1_MAPPING_CHEAP_ENVIRONMENT:
		{
			ClassicEnvironmentMapperClass *mapper = W3DNEW ClassicEnvironmentMapperClass(1);
			Set_Mapper(mapper, 1);
			mapper->Release_Ref();
		}
		break;

		case W3DVERTMAT_STAGE1_MAPPING_LINEAR_OFFSET:
		{
			LinearOffsetTextureMapperClass *mapper =
				W3DNEW LinearOffsetTextureMapperClass(mapping1_arg_ini, "Args", 1);
			Set_Mapper(mapper, 1);
			mapper->Release_Ref();
		}
		break;

		case W3DVERTMAT_STAGE1_MAPPING_SCREEN:
		{
			ScreenMapperClass *mapper =
				W3DNEW ScreenMapperClass(mapping1_arg_ini, "Args", 1);
			Set_Mapper(mapper, 1);
			mapper->Release_Ref();
		}
		break;

		case W3DVERTMAT_STAGE1_MAPPING_SCALE:
			{
				ScaleTextureMapperClass *mapper =
					NEW_REF(ScaleTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_GRID:
			{
				GridTextureMapperClass *mapper =
					NEW_REF(GridTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_ROTATE:
			{
				RotateTextureMapperClass *mapper =
					NEW_REF(RotateTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_SINE_LINEAR_OFFSET:
			{
				SineLinearOffsetTextureMapperClass *mapper =
					NEW_REF(SineLinearOffsetTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_STEP_LINEAR_OFFSET:
			{
				StepLinearOffsetTextureMapperClass *mapper =
					NEW_REF(StepLinearOffsetTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_ZIGZAG_LINEAR_OFFSET:
			{
				ZigZagLinearOffsetTextureMapperClass *mapper =
					NEW_REF(ZigZagLinearOffsetTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_WS_CLASSIC_ENV:
			{
				WSClassicEnvironmentMapperClass *mapper = NEW_REF(WSClassicEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_WS_ENVIRONMENT:
			{
				WSEnvironmentMapperClass *mapper = NEW_REF(WSEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_GRID_CLASSIC_ENV:
			{
				GridClassicEnvironmentMapperClass *mapper =
					NEW_REF(GridClassicEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_GRID_ENVIRONMENT:
			{
				GridEnvironmentMapperClass *mapper =
					NEW_REF(GridEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_RANDOM:
			{
				RandomTextureMapperClass *mapper =
					NEW_REF(RandomTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_EDGE:
			{
				EdgeMapperClass *mapper =
					NEW_REF(EdgeMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_BUMPENV:
			{
				BumpEnvTextureMapperClass *mapper =
					NEW_REF(BumpEnvTextureMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

	case W3DVERTMAT_STAGE1_MAPPING_GRID_WS_CLASSIC_ENV:
			{
				GridWSClassicEnvironmentMapperClass *mapper =
					NEW_REF(GridWSClassicEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		case W3DVERTMAT_STAGE1_MAPPING_GRID_WS_ENVIRONMENT:
			{
				GridWSEnvironmentMapperClass *mapper =
					NEW_REF(GridWSEnvironmentMapperClass,(mapping1_arg_ini, "Args", 1));
				Set_Mapper(mapper,1);
				mapper->Release_Ref();
			}
			break;

		default:
			break;
	}
}


WW3DErrorType VertexMaterialClass::Save_W3D(ChunkSaveClass & csave)
{
	WWASSERT(0);
	return WW3D_ERROR_OK;
}




/***********************************************************************************************
 * Init -- init code                                                                           *
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
 *   2/14/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void VertexMaterialClass::Init()
{
	int i;
	for (i=0; i<PRESET_COUNT;i++)
		Presets[i]=NEW_REF(VertexMaterialClass,());

	// Set up presets
	Presets[PRELIT_DIFFUSE]->Set_Diffuse_Color_Source(VertexMaterialClass::COLOR1);
	Presets[PRELIT_DIFFUSE]->Set_Lighting(false);
	Presets[PRELIT_NODIFFUSE]->Set_Lighting(false);
}


/***********************************************************************************************
 * Shutdown -- shutdown code                                                                   *
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
 *   2/14/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void VertexMaterialClass::Shutdown()
{
	int i;
	for (i=0; i<PRESET_COUNT;i++)
		REF_PTR_RELEASE(Presets[i]);
}


/***********************************************************************************************
 * Get_Preset -- retrieve presets                                                              *
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
 *   2/14/2001  hy : Created.                                                                  *
 *=============================================================================================*/
VertexMaterialClass * VertexMaterialClass::Get_Preset(PresetType type)
{
	WWASSERT(type<PRESET_COUNT);
	Presets[type]->Add_Ref();
	return Presets[type];
}
