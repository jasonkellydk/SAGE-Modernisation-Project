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
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/SegLineRenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/26/02 4:04p                                             $*
 *                                                                                             *
 *                    $Revision:: 5                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

import Graphics.Materials.State;
#include <array>
#include "WW3D2/GraphicsMaterial.h"
#include <algorithm>
import Graphics.Scene.Views.CameraMatrices;
import Assets.Math;
import Graphics.Scene.Beams.RibbonSubdivision;
import Graphics.Scene.Beams.RibbonEdges;
import Graphics.Scene.Beams.RibbonIntersections;
import Graphics.Scene.Beams.RibbonGeometry;
#include "WW3D2/Camera.h"
#include "SegLineRenderer.h"
#include "WW3D.h"
#include "W3DFile.h"
#include "RInfo.h"
#include "WWMath/vp.h"
#include "WWMath/Vector3i.h"
#include "WWLib/RANDOM.h"
#include "WWMath/v3_rnd.h"
#include "MeshGeometry.h"


/* We have chunking logic which handles N segments at a time. To simplify the subdivision logic,
** we will ensure that N is a power of two and that N >= 2^MAX_SEGLINE_SUBDIV_LEVELS, so that the
** subdivision logic can be inside the chunking loop.
*/

#if MAX_SEGLINE_SUBDIV_LEVELS > 7
#define SEGLINE_CHUNK_SIZE (1 << MAX_SEGLINE_SUBDIV_LEVELS)
#else
#define SEGLINE_CHUNK_SIZE (128)
#endif


#define MAX_SEGLINE_POINT_BUFFER_SIZE (1 + SEGLINE_CHUNK_SIZE)
// This macro depends on the assumption that each line segment is two polys.





SegLineRendererClass::SegLineRendererClass() :
		Texture(nullptr),
		Shader(Graphics::MaterialState::AdditiveSprite()),
		Width(0.0f),
		Color(Vector3(1,1,1)),
		Opacity(1.0f),
		SubdivisionLevel(0),
		NoiseAmplitude(0.0f),
		MergeAbortFactor(1.5f),
		TextureTileFactor(1.0f),
		TextureCoordinates(WW3D::Get_Logic_Time_Milliseconds()),
		Bits(DEFAULT_BITS)
{
	// EMPTY
}

SegLineRendererClass::SegLineRendererClass(const SegLineRendererClass & that) :
		Texture(nullptr),
		Shader(Graphics::MaterialState::AdditiveSprite()),
		Width(0.0f),
		Color(Vector3(1,1,1)),
		Opacity(1.0f),
		SubdivisionLevel(0),
		NoiseAmplitude(0.0f),
		MergeAbortFactor(1.5f),
		TextureTileFactor(1.0f),
		TextureCoordinates(that.TextureCoordinates),
		Bits(DEFAULT_BITS)
{
	*this = that;
}

SegLineRendererClass & SegLineRendererClass::operator = (const SegLineRendererClass & that)
{
	if (this != &that) {
		REF_PTR_SET(Texture,that.Texture);
		Shader = that.Shader;
		Width = that.Width;
		Color = that.Color;
		Opacity = that.Opacity;
		SubdivisionLevel = that.SubdivisionLevel;
		NoiseAmplitude = that.NoiseAmplitude;
		MergeAbortFactor = that.MergeAbortFactor;
		TextureTileFactor = that.TextureTileFactor;
		TextureCoordinates = that.TextureCoordinates;
		Bits = that.Bits;
	}
	return *this;
}

SegLineRendererClass::~SegLineRendererClass()
{
	REF_PTR_RELEASE(Texture);
}

void SegLineRendererClass::Init(const W3dEmitterLinePropertiesStruct & props)
{
	// translate the flags
	Set_Merge_Intersections(props.Flags & W3D_ELINE_MERGE_INTERSECTIONS);
	Set_Freeze_Random(props.Flags & W3D_ELINE_FREEZE_RANDOM);
	Set_Disable_Sorting(props.Flags & W3D_ELINE_DISABLE_SORTING);
	Set_End_Caps(props.Flags & W3D_ELINE_END_CAPS);

	int texture_mode = ((props.Flags & W3D_ELINE_TEXTURE_MAP_MODE_MASK) >> W3D_ELINE_TEXTURE_MAP_MODE_OFFSET);
	switch (texture_mode)
	{
	case W3D_ELINE_UNIFORM_WIDTH_TEXTURE_MAP:
		Set_Texture_Mapping_Mode(UNIFORM_WIDTH_TEXTURE_MAP);
		break;
	case W3D_ELINE_UNIFORM_LENGTH_TEXTURE_MAP:
		Set_Texture_Mapping_Mode(UNIFORM_LENGTH_TEXTURE_MAP);
		break;
	case W3D_ELINE_TILED_TEXTURE_MAP:
		Set_Texture_Mapping_Mode(TILED_TEXTURE_MAP);
		break;
	};

	// install all other settings
	Set_Current_Subdivision_Level(props.SubdivisionLevel);
	Set_Noise_Amplitude(props.NoiseAmplitude);
	Set_Merge_Abort_Factor(props.MergeAbortFactor);
	Set_Texture_Tile_Factor(props.TextureTileFactor);
	Set_UV_Offset_Rate(Vector2(props.UPerSec,props.VPerSec));
}


void SegLineRendererClass::Set_Texture(TextureClass *texture)
{
	REF_PTR_SET(Texture,texture);
}

TextureClass * SegLineRendererClass::Get_Texture() const
{
	if (Texture != nullptr) {
		Texture->Add_Ref();
	}
	return Texture;
}

void SegLineRendererClass::Set_Current_UV_Offset(const Vector2 & offset)
{
	TextureCoordinates.SetOffset({offset.X, offset.Y});
}

void SegLineRendererClass::Set_Texture_Tile_Factor(float factor)
{
	// Care should be taken to avoid tiling a texture too many times over a single polygon;
	// otherwise performance may be adversely affected.
	///@todo: I raised this number and didn't see much difference on our min-spec. -MW
	const static float MAX_LINE_TILING_FACTOR = 50.0f;
	if (factor > MAX_LINE_TILING_FACTOR) {
		WWDEBUG_SAY(("Texture (%s) Tile Factor (%.2f) too large in SegLineRendererClass!", Peek_Texture()->Get_Texture_Name().str(), TextureTileFactor));
		factor = MAX_LINE_TILING_FACTOR;
	} else {
		factor = MAX(factor, 0.0f);
	}
	TextureTileFactor = factor;
}

void SegLineRendererClass::Reset_Line()
{
	TextureCoordinates.Reset(WW3D::Get_Logic_Time_Milliseconds());
}



void SegLineRendererClass::Render
(
	RenderInfoClass & rinfo,
	const Matrix3D & transform,
	unsigned int num_points,
	Vector3 * points,
	const SphereClass & obj_sphere,
	Vector4 * rgbas,
    const SegLineGeometrySink* sink
)
{
	Matrix4x4 view;
	std::copy_n(Graphics::Get_Camera_Matrices().view.values.data(), 16, &view[0][0]);


    const auto offset = TextureCoordinates.Advance(WW3D::Get_Logic_Time_Milliseconds());
    Graphics::RibbonTextureMapping mapping = Graphics::RibbonTextureMapping::Across;
    switch (Get_Texture_Mapping_Mode()) {
    case UNIFORM_LENGTH_TEXTURE_MAP: mapping = Graphics::RibbonTextureMapping::Along; break;
    case TILED_TEXTURE_MAP: mapping = Graphics::RibbonTextureMapping::Tiled; break;
    default: break;
    }

	/*
	** Process line geometry:
	*/


	// We reduce the chunk size to take account of subdivision levels (so that the # of points
	// after subdivision will be no higher than the allowed maximum). We know this will not reduce
	// the chunk size below 2, since the chunk size must be at least two to the power of the
	// maximum allowable number of subdivisions. The plus 1 is because #points = #segments + 1.
	unsigned int chunk_size = (SEGLINE_CHUNK_SIZE >> SubdivisionLevel) + 1;
	if (chunk_size > num_points) chunk_size = num_points;

	// Chunk through the points (we increment by chunk_size - 1 because the last point of this
	// chunk must be reused as the first point of the next chunk. This is also the reason we stop
	// when chidx = NumPoints - 1: the last point has already been processed in the previous
	// iteration so we don't need another one).
    Graphics::RibbonSubdivision subdivision;
    Graphics::RibbonEdges edges;
    Graphics::RibbonIntersections joints;
    Graphics::RibbonGeometry geometry;
	for (unsigned int chidx = 0; chidx < num_points - 1; chidx += (chunk_size - 1)) {
		unsigned int point_cnt = num_points - chidx;
		point_cnt = MIN(point_cnt, chunk_size);

		// We use these different loop indices (which loop INSIDE a chunk) to improve readability:


		/*
		** Transform points in chunk from objectspace to eyespace:
		*/

		Vector3 xformed_pts[MAX_SEGLINE_POINT_BUFFER_SIZE];

		Matrix3D view2(	view[0].X,view[0].Y,view[0].Z,view[0].W,
								view[1].X,view[1].Y,view[1].Z,view[1].W,
								view[2].X,view[2].Y,view[2].Z,view[2].W);

#ifdef ALLOW_TEMPORARIES
		Matrix3D modelview=view2*transform;
#else
		Matrix3D modelview;
		modelview.mul(view2, transform);
#endif

		VectorProcessorClass::Transform(&xformed_pts[0],
			&points[chidx], modelview, point_cnt);


		/*
		** Fractal noise recursive subdivision:
		** We find the midpoint for each section, apply a random offset, and recurse. We also find
		** the average V coordinate of the endpoints which is the midpoint V (for tiled texture
		** mapping).
		*/

		Vector4 *rgbasPointer = rgbas ? &rgbas[ chidx ] : nullptr;

        Random3Class randomize;
        const float oo_int_max = 1.0f / (float)INT_MAX;
        Vector3SolidBoxRandomizer randomizer(Vector3(1,1,1));
        if (!subdivision.Build(point_cnt,SubdivisionLevel,NoiseAmplitude,[&](std::size_t index) {
            const auto& position=xformed_pts[index];
            const Vector4 color=rgbasPointer?rgbasPointer[index]:Vector4(Color.X,Color.Y,Color.Z,Opacity);
            return Graphics::RibbonPoint{{position.X,position.Y,position.Z},
                {color.X,color.Y,color.Z,color.W},
                Graphics::Ribbon_Texture_V(mapping, chidx + index, TextureTileFactor)};
        },[&] {
            Vector3 offset(0,0,0);
            if (Is_Freeze_Random())
                offset.Set(randomize*oo_int_max,randomize*oo_int_max,randomize*oo_int_max);
            else randomizer.Get_Vector(offset);
            return std::array<float,3>{offset.X,offset.Y,offset.Z};
        })) return;
        if (!edges.Build(subdivision.Points(), Width)) return;
        if (!joints.Build(edges,Is_Merge_Intersections()!=0,Width,MergeAbortFactor)) return;
        if (!geometry.Build(edges.Points(),joints.Top(),joints.Bottom(),mapping,offset)) return;
        if (sink) {
            if (sink->submit) sink->submit(sink->context,geometry.Vertices().data(),
                static_cast<unsigned>(geometry.Vertices().size()),geometry.Indices().data(),
                static_cast<unsigned>(geometry.Indices().size()));
            continue;
        }
        Matrix4x4 projection;
        rinfo.Camera.Get_Backend_Projection_Matrix(&projection);
        Graphics::MaterialState shader = Shader;
        shader.Set_Cull_Mode(Graphics::MaterialState::CULL_MODE_DISABLE);
        shader.Set_Primary_Gradient(Graphics::MaterialState::GRADIENT_MODULATE);
        const bool sorting = !Is_Sorting_Disabled() && WW3D::Is_Sorting_Enabled()
            && Shader.Get_Dst_Blend_Func()!=Graphics::MaterialState::DSTBLEND_ZERO
            && Shader.Get_Alpha_Test()==Graphics::MaterialState::ALPHATEST_DISABLE;
        const Matrix4x4 camera_space(true);
        Draw_Graphics_Material_Geometry(geometry.Vertices(),geometry.Indices(),projection,shader,
            {Texture,nullptr},{},sorting ? &camera_space : nullptr);

	}


}


void SegLineRendererClass::Scale(float scale)
{
	Width *= scale;
	NoiseAmplitude *= scale;
}
