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
 *                 Project Name : WWPhys                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/matrixMapper.cpp                       $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/26/02 4:04p                                             $*
 *                                                                                             *
 *                    $Revision:: 11                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   MatrixMapperClass::MatrixMapperClass -- Constructor                                       *
 *   MatrixMapperClass::Set_Texture_Transform -- Sets the viewspace-to-texturespace transform  *
 *   MatrixMapperClass::Update_View_To_Pixel_Transform -- recomputes ViewToPixel               *
 *   MatrixMapperClass::Compute_Texture_Coordinate -- compute a single texture coord           *
 *   MatrixMapperClass::Calculate_Texture_Matrix -- Calculate texture matrix                   *
 *   CompositeMatrixMapperClass::CompositeMatrixMapperClass -- Constructor                     *
 *   CompositeMatrixMapperClass::CompositeMatrixMapperClass -- Copy Constructor                *
 *   CompositeMatrixMapperClass::~CompositeMatrixMapperClass -- Destructor                     *
 *   CompositeMatrixMapperClass::~CompositeMatrixMapperClass -- Destructor                     *
 *   CompositeMatrixMapperClass::Calculate_Texture_Matrix -- Calculate texture matrix          *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#include "matrixMapper.h"
#include <cstring>

namespace {
std::array<float,16> Texture_Matrix_Values(const Matrix4x4& matrix)
{
    std::array<float,16> values;
    std::memcpy(values.data(),&matrix,sizeof(matrix));
    return values;
}
static_assert(static_cast<unsigned>(MatrixMapperClass::ORTHO_PROJECTION) == static_cast<unsigned>(Graphics::TextureProjection::Orthographic));
static_assert(static_cast<unsigned>(MatrixMapperClass::PERSPECTIVE_PROJECTION) == static_cast<unsigned>(Graphics::TextureProjection::Perspective));
static_assert(static_cast<unsigned>(MatrixMapperClass::DEPTH_GRADIENT) == static_cast<unsigned>(Graphics::TextureProjection::DepthGradient));
static_assert(static_cast<unsigned>(MatrixMapperClass::NORMAL_GRADIENT) == static_cast<unsigned>(Graphics::TextureProjection::NormalGradient));
}



/***********************************************************************************************
 * MatrixMapperClass::MatrixMapperClass -- Constructor                                         *
 *                                                                                             *
 *    Initializes the member variables to defaults                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/13/99   gth : created                                                                  *
 *=============================================================================================*/
MatrixMapperClass::MatrixMapperClass(int stage) :
	TextureMapperClass(stage),
	Flags(0),
	Type(ORTHO_PROJECTION),
	ViewToTexture(true),
	ViewToPixel(true),
	ViewSpaceProjectionNormal(0.0f, 0.0f, 0.0f),
	GradientUCoord(0.5f)
{
}

/***********************************************************************************************
 * MatrixMapperClass::Set_Texture_Transform -- Sets the viewspace-to-texturespace transform    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   6/20/2001  gth : created                                                                  *
 *=============================================================================================*/
void MatrixMapperClass::Set_Texture_Transform(const Matrix3D & view_to_texture,float texsize)
{
	ViewToTexture = Matrix4x4(view_to_texture);
	Update_View_To_Pixel_Transform(texsize);
}

/***********************************************************************************************
 * MatrixMapperClass::Set_Texture_Transform -- Sets the viewspace-to-texturespace transform    *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/13/99   gth : Created.                                                                 *
 *=============================================================================================*/
void	MatrixMapperClass::Set_Texture_Transform(const Matrix4x4 & view_to_texture,float texsize)
{
	ViewToTexture=view_to_texture;

	Update_View_To_Pixel_Transform(texsize);
}

/***********************************************************************************************
 * MatrixMapperClass::Update_View_To_Pixel_Transform -- recomputes ViewToPixel                 *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/4/00     gth : Created.                                                                 *
 *=============================================================================================*/
void MatrixMapperClass::Update_View_To_Pixel_Transform(float tex_size)
{
	/*
	** Create a ViewToPixel matrix which also does all of the offsetting and flipping that has
	** to take place to get the actual texture coordinates.
	**
	** Here is a description of the math:
	** If you transform eye-space points with the ViewToTexture matrix, you then need to compute:
	** s = ((x/w) + 1.0f) * 0.5f * ((texwid-2)/texwid)
	** t = (1.0f - (y/w)) * 0.5f * ((texwid-2)/texwid)
	**
	** Let K = 0.5 * ((texwid-2)/texwid), Then:
	** ((x/w) + 1.0f) * K = (K*x + K*w) / w
	** (1.0f - (y/w)) * K = (-K*y + K*w) / w
	**
	** This leads to the following matrix (The KOOK matrix!)
	** | K  0 0 K |
	** | 0 -K 0 K |
	** | 0  0 1 0 |
	** | 0  0 0 1 |
	**
	** The code below manually "optimally" pre-multiplies this matrix with the ViewToTexture matrix
	**
	** In addition, the z-coordinate is modified so that it goes from 0 to 1 rather than -1 to +1.
	** It can also be flipped if the INVERT_DEPTH_GRADIENT flag is on.
	*/
	float k = 0.5 * (tex_size - 2.0f) / tex_size;

	ViewToPixel[0][0] = k * ViewToTexture[0][0] + k * ViewToTexture[3][0];
	ViewToPixel[0][1] = k * ViewToTexture[0][1] + k * ViewToTexture[3][1];
	ViewToPixel[0][2] = k * ViewToTexture[0][2] + k * ViewToTexture[3][2];
	ViewToPixel[0][3] = k * ViewToTexture[0][3] + k * ViewToTexture[3][3];

	ViewToPixel[1][0] = -k * ViewToTexture[1][0] + k * ViewToTexture[3][0];
	ViewToPixel[1][1] = -k * ViewToTexture[1][1] + k * ViewToTexture[3][1];
	ViewToPixel[1][2] = -k * ViewToTexture[1][2] + k * ViewToTexture[3][2];
	ViewToPixel[1][3] = -k * ViewToTexture[1][3] + k * ViewToTexture[3][3];

	if (Get_Flag(INVERT_DEPTH_GRADIENT)) {
		ViewToPixel[2][0] = -0.5f * ViewToTexture[2][0] + 0.5f * ViewToTexture[3][0];
		ViewToPixel[2][1] = -0.5f * ViewToTexture[2][1] + 0.5f * ViewToTexture[3][1];
		ViewToPixel[2][2] = -0.5f * ViewToTexture[2][2] + 0.5f * ViewToTexture[3][2];
		ViewToPixel[2][3] = -0.5f * ViewToTexture[2][3] + 0.5f * ViewToTexture[3][3];
	} else {
		ViewToPixel[2][0] = 0.5f * ViewToTexture[2][0] + 0.5f * ViewToTexture[3][0];
		ViewToPixel[2][1] = 0.5f * ViewToTexture[2][1] + 0.5f * ViewToTexture[3][1];
		ViewToPixel[2][2] = 0.5f * ViewToTexture[2][2] + 0.5f * ViewToTexture[3][2];
		ViewToPixel[2][3] = 0.5f * ViewToTexture[2][3] + 0.5f * ViewToTexture[3][3];
	}

	ViewToPixel[3] = ViewToTexture[3];

	/*
	** Store the view space negative z vector for lighting effects
	*/
	ViewSpaceProjectionNormal.X = -ViewToTexture[2][0];
	ViewSpaceProjectionNormal.Y = -ViewToTexture[2][1];
	ViewSpaceProjectionNormal.Z = -ViewToTexture[2][2];
	ViewSpaceProjectionNormal.Normalize();

}

/***********************************************************************************************
 * MatrixMapperClass::Compute_Texture_Coordinate -- compute a single texture coord             *
 *                                                                                             *
 * INPUT:                                                                                      *
 * point - 3D point to calculate the texture coordinate for                                    *
 * set_stq - pointer to texture coordinate, s,t will be stored in X,Y.  q will be stored in Z  *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   1/25/00    gth : Created.                                                                 *
 *=============================================================================================*/
void MatrixMapperClass::Compute_Texture_Coordinate(const Vector3 & point,Vector3 * set_stq)
{
	set_stq->X = ViewToPixel[0][0]*point.X + ViewToPixel[0][1]*point.Y + ViewToPixel[0][2]*point.Z + ViewToPixel[0][3];
	set_stq->Y = ViewToPixel[1][0]*point.X + ViewToPixel[1][1]*point.Y + ViewToPixel[1][2]*point.Z + ViewToPixel[1][3];
	set_stq->Z = ViewToPixel[3][0]*point.X + ViewToPixel[3][1]*point.Y + ViewToPixel[3][2]*point.Z + ViewToPixel[3][3];
}


/***********************************************************************************************
 * MatrixMapperClass::Calculate_Texture_Matrix -- Calculate texture matrix                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/06/01     NH : Created comment block.                                                  *
 *=============================================================================================*/
void MatrixMapperClass::Calculate_Texture_Matrix(Matrix4x4 &tex_matrix)
{
    const auto matrix = Graphics::Make_Projection_Texture_Transform(Texture_Matrix_Values(ViewToPixel),
        static_cast<Graphics::TextureProjection>(Type),GradientUCoord,
        {ViewSpaceProjectionNormal.X,ViewSpaceProjectionNormal.Y,ViewSpaceProjectionNormal.Z});
    std::memcpy(&tex_matrix,matrix.data(),sizeof(tex_matrix));
}

/***********************************************************************************************
 * CompositeMatrixMapperClass::CompositeMatrixMapperClass -- Constructor                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/05/01     NH : Created.                                                                *
 *=============================================================================================*/
CompositeMatrixMapperClass::CompositeMatrixMapperClass(TextureMapperClass *internal_mapper, unsigned int stage) :
	MatrixMapperClass(stage),
	InternalMapper(internal_mapper)
{
	if (InternalMapper) {
		InternalMapper->Add_Ref();
	}
}

/***********************************************************************************************
 * CompositeMatrixMapperClass::CompositeMatrixMapperClass -- Copy Constructor                  *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/05/01     NH : Created.                                                                *
 *=============================================================================================*/
CompositeMatrixMapperClass::CompositeMatrixMapperClass(const CompositeMatrixMapperClass & src) :
	MatrixMapperClass(src),
	InternalMapper(src.InternalMapper ? src.InternalMapper->Clone() : nullptr)
{
	if (InternalMapper) {
		InternalMapper->Add_Ref();
	}
}

/***********************************************************************************************
 * CompositeMatrixMapperClass::~CompositeMatrixMapperClass -- Destructor                       *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/05/01     NH : Created.                                                                *
 *=============================================================================================*/
CompositeMatrixMapperClass::~CompositeMatrixMapperClass()
{
	if (InternalMapper) {
		InternalMapper->Release_Ref();
		InternalMapper = nullptr;
	}
}


/***********************************************************************************************
 * MatrixMapperClass::Calculate_Texture_Matrix -- Calculate texture matrix                     *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   11/06/01     NH : Created comment block.                                                  *
 *=============================================================================================*/
void CompositeMatrixMapperClass::Calculate_Texture_Matrix(Matrix4x4 &tex_matrix)
{
    auto projection = Texture_Matrix_Values(ViewToPixel);
    if (InternalMapper) {
        Matrix4x4 input;
        InternalMapper->Calculate_Texture_Matrix(input);
        auto local = Texture_Matrix_Values(input);
        // Authored 2D mapper matrices place translation in column three.
        // Expand that homogeneous coordinate before composing the projection.
        for (unsigned row=0;row<4;++row) {
            local[row*4+3] += local[row*4+2];
            local[row*4+2] = 0;
        }
        projection = Graphics::Compose_Texture_Projection(projection,local);
    }
    const auto matrix = Graphics::Make_Projection_Texture_Transform(projection,
        static_cast<Graphics::TextureProjection>(Type),GradientUCoord,
        {ViewSpaceProjectionNormal.X,ViewSpaceProjectionNormal.Y,ViewSpaceProjectionNormal.Z});
    std::memcpy(&tex_matrix,matrix.data(),sizeof(tex_matrix));
}
