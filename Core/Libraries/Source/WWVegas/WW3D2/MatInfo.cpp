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
 *                 Project Name : MatInfo.h                                                    *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/MatInfo.cpp                            $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 6/15/01 5:50p                                               $*
 *                                                                                             *
 *                    $Revision:: 10                                                          $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "MatInfo.h"
#include "StringUtilities.h"
#include "WWDebug/wwdebug.h"
#include "MeshMdl.h"
#include "Texture.h"

MaterialInfoClass::MaterialInfoClass()
{
}

MaterialInfoClass::MaterialInfoClass(const MaterialInfoClass & src)
{
	for (int mi=0; mi<src.VertexMaterials.Count(); mi++) {
		VertexMaterialClass * vmat;
		vmat = src.VertexMaterials[mi]->Clone();
		VertexMaterials.Add(vmat);
	}

	for (int ti=0; ti<src.Textures.Count(); ti++) {
		TextureClass * tex = src.Textures[ti];
		tex->Add_Ref();
		Textures.Add(tex);
	}
}


MaterialInfoClass::~MaterialInfoClass()
{
	Free();
}


MaterialInfoClass * MaterialInfoClass::Clone() const
{
	return W3DNEW MaterialInfoClass(*this);
}

int MaterialInfoClass::Add_Texture(TextureClass * tex)
{
	WWASSERT(tex != nullptr);
	tex->Add_Ref();
	int index = Textures.Count();
	Textures.Add(tex);
	return index;
}

int MaterialInfoClass::Get_Texture_Index(const char * name)
{
	for (int i=0; i<Textures.Count(); i++) {
		if (WW3DString::Compare_No_Case(name,Textures[i]->Get_Texture_Name()) == 0) {
			return i;
		}
	}
	return -1;
}

TextureClass * MaterialInfoClass::Get_Texture(int index)
{
	WWASSERT(index >= 0);
	WWASSERT(index < Textures.Count());
	Textures[index]->Add_Ref();
	return Textures[index];
}

/*

void MaterialInfoClass::Set_Texture_Reduction_Factor(float trf)
{
	for (int i = 0; i < Textures.Count(); i++) {
		Textures[i]->Set_Reduction_Factor(trf);
	}
}


void MaterialInfoClass::Process_Texture_Reduction()
{
	for (int i = 0; i < Textures.Count(); i++) {
		Textures[i]->Process_Reduction();
	}
}
*/
void MaterialInfoClass::Free()
{
	int i;

	for (i=0; i<VertexMaterials.Count(); i++) {
		REF_PTR_RELEASE(VertexMaterials[i]);
	}
	VertexMaterials.Delete_All();

	for (i=0; i<Textures.Count(); i++) {
		REF_PTR_RELEASE(Textures[i]);
	}
	Textures.Delete_All();
}


MaterialRemapperClass::MaterialRemapperClass(MaterialInfoClass * src,MaterialInfoClass * dest) :
	TextureCount(0),
	TextureRemaps(nullptr),
	VertexMaterialCount(0),
	VertexMaterialRemaps(nullptr),
	LastSrcVmat(nullptr),
	LastDestVmat(nullptr),
	LastSrcTex(nullptr),
	LastDestTex(nullptr)
{
	WWASSERT(src);
	WWASSERT(dest);
	WWASSERT(src->Texture_Count() == dest->Texture_Count());
	WWASSERT(src->Vertex_Material_Count() == dest->Vertex_Material_Count());

	SrcMatInfo = src;
	SrcMatInfo->Add_Ref();
	DestMatInfo = dest;
	DestMatInfo->Add_Ref();

	if (src->Vertex_Material_Count() > 0) {
		VertexMaterialCount = src->Vertex_Material_Count();
		VertexMaterialRemaps = W3DNEWARRAY VmatRemapStruct[VertexMaterialCount];
		for (int i=0; i<src->Vertex_Material_Count(); i++) {
			VertexMaterialRemaps[i].Src = src->Peek_Vertex_Material(i);
			VertexMaterialRemaps[i].Dest = dest->Peek_Vertex_Material(i);
		}
	}

	if (src->Texture_Count() > 0) {
		TextureCount = src->Texture_Count();
		TextureRemaps = W3DNEWARRAY TextureRemapStruct[TextureCount];
		for (int i=0; i<src->Texture_Count(); i++) {
			TextureRemaps[i].Src = src->Peek_Texture(i);
			TextureRemaps[i].Dest = dest->Peek_Texture(i);
		}
	}
}

MaterialRemapperClass::~MaterialRemapperClass()
{
	SrcMatInfo->Release_Ref();
	DestMatInfo->Release_Ref();

	delete[] TextureRemaps;
	delete[] VertexMaterialRemaps;
}

TextureClass * MaterialRemapperClass::Remap_Texture(TextureClass * src)
{
	if (src == nullptr) return src;
	if (src == LastSrcTex) return LastDestTex;
	for (int i=0; i<TextureCount; i++) {
		if (TextureRemaps[i].Src == src) {
			LastSrcTex = src;
			LastDestTex = TextureRemaps[i].Dest;
			return TextureRemaps[i].Dest;
		}
	}
	WWASSERT(0); // uh-oh didn't find the texture, what happened???
	return nullptr;
}

VertexMaterialClass * MaterialRemapperClass::Remap_Vertex_Material(VertexMaterialClass * src)
{
	if (src == nullptr) return src;
	if (src == LastSrcVmat) return LastDestVmat;
	for (int i=0; i<VertexMaterialCount; i++) {
		if (VertexMaterialRemaps[i].Src == src) {
			LastSrcVmat = src;
			LastDestVmat = VertexMaterialRemaps[i].Dest;
			return VertexMaterialRemaps[i].Dest;
		}
	}
	WWASSERT(0); // uh-oh didn't find the material, what happened???
	return nullptr;
}

void MaterialRemapperClass::Remap_Mesh(const MeshMatDescClass * srcmeshmatdesc, MeshMatDescClass * destmeshmatdesc)
{
	/*
	** Remap the vertex materials if there is at least one of them
	*/
	if (SrcMatInfo->Vertex_Material_Count() >= 1) {

		for (int pass = 0;pass < srcmeshmatdesc->Get_Pass_Count(); pass++) {

			if (srcmeshmatdesc->Has_Material_Array(pass)) {

				for (int vert_index = 0; vert_index < srcmeshmatdesc->Get_Vertex_Count(); vert_index++) {
					VertexMaterialClass * src = srcmeshmatdesc->Peek_Material(vert_index, pass);
					destmeshmatdesc->Set_Material(vert_index, Remap_Vertex_Material(src),pass);
				}

			} else {

				VertexMaterialClass * src = srcmeshmatdesc->Peek_Single_Material(pass);
				destmeshmatdesc->Set_Single_Material(Remap_Vertex_Material(src), pass);

			}
		}
	}

	/*
	** Remap the textures if there is at least one of them
	*/
	if (SrcMatInfo->Texture_Count() >= 1) {

		for (int pass = 0;pass < srcmeshmatdesc->Get_Pass_Count(); pass++) {

			for (int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {

				if (srcmeshmatdesc->Has_Texture_Array(pass, stage)) {

					for (int poly_index = 0; poly_index < srcmeshmatdesc->Get_Polygon_Count(); poly_index++) {
						TextureClass * src = srcmeshmatdesc->Peek_Texture(poly_index, pass, stage);
						destmeshmatdesc->Set_Texture(poly_index, Remap_Texture(src), pass, stage);
					}

				} else {

					TextureClass * src = srcmeshmatdesc->Peek_Single_Texture(pass, stage);
					destmeshmatdesc->Set_Single_Texture(Remap_Texture(src), pass, stage);

				}
			}
		}
	}
}
