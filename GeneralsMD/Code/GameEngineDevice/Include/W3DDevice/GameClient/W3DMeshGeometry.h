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

#pragma once


#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

import Graphics.Scene.Models.Hierarchy;
import Graphics.Scene.Models.BoundsTree;
import Graphics.Scene.Models.SourceRevision;

#include "WWLib/always.h"
#include "WWLib/bittype.h"
import Graphics.Scene.Models.GeometrySource;
import Assets.Adapters.W3D.MeshData;
import Engine.Core.Math.Index3;
import Engine.Core.Math.Vector4;
import Engine.Core.Math.Vector3;
import Engine.Core.Math.Triangle3;
import Engine.Core.Math.OrientedBox3;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.Sphere3;

#include "W3DDevice/GameClient/W3DCastQuery.h"
#include "W3DDevice/GameClient/W3DIntersectionQuery.h"


class ChunkLoadClass;

class W3DRenderContext;

using TriIndex = Engine::Math::Index3u16;


/**
** W3DMeshGeometry
** This class encapsulates the geometry data for a triangle mesh.
*/

class W3DMeshGeometry : public RefCountClass
{
public:

	W3DMeshGeometry();
	W3DMeshGeometry(const W3DMeshGeometry & that);
	virtual ~W3DMeshGeometry() override;

	W3DMeshGeometry & operator = (const W3DMeshGeometry & that);

	enum FlagsType
	{
		DIRTY_BOUNDS							= 0x00000001,
		DIRTY_PLANES							= 0x00000002,
		DIRTY_VNORMALS							= 0x00000004,

		SORT										= 0x00000010,
		DISABLE_BOUNDING_BOX					= 0x00000020,
		DISABLE_BOUNDING_SPHERE				= 0x00000040,
		DISABLE_PLANE_EQ						= 0x00000080,
		TWO_SIDED								= 0x00000100,

		ALIGNED									= 0x00000200,
		SKIN										= 0x00000400,
		ORIENTED									= 0x00000800,
		CAST_SHADOW								= 0x00001000,

		PRELIT_MASK								= 0x0000E000,
		PRELIT_VERTEX							= 0x00002000,
		PRELIT_LIGHTMAP_MULTI_PASS			= 0x00004000,
		PRELIT_LIGHTMAP_MULTI_TEXTURE		= 0x00008000,

		ALLOW_NPATCHES							= 0x00010000,
	};

	void							Reset_Geometry(int polycount,int vertcount);

	const char *				Get_Name() const;
	void							Set_Name(const char * newname);

	const char *				Get_User_Text();
	void							Set_User_Text(char * usertext);

	void							Set_Flag(FlagsType flag,bool onoff)						{ if (onoff) {	Flags |= flag;	} else {	Flags &= ~flag; } }
	int							Get_Flag(FlagsType flag)									{ return Flags & flag; }

	void							Set_Sort_Level(int level)									{ SortLevel = level; }
	int							Get_Sort_Level() const									{ return SortLevel; }

	int							Get_Polygon_Count() const								{ return Geometry.polygon_count; }
	int							Get_Vertex_Count() const								{ return Geometry.vertex_count; }

	const TriIndex*			Get_Polygon_Array()										{ return get_polys(); }
	Engine::Math::Vector3 * Get_Vertex_Array() { assert((Geometry.positions)); Geometry.revision.Expose_Writable(); return Geometry.positions->data(); }
    const Engine::Math::Vector3* Peek_Vertex_Array() const { assert((Geometry.positions)); return Geometry.positions->data(); }
    std::uint64_t Geometry_Revision() const noexcept { return Geometry.revision.Token(); }
	const Engine::Math::Vector3 * Get_Vertex_Normal_Array();
	const Engine::Math::Vector4 *	Get_Plane_Array(bool create = true);
	const uint32 *				Get_Vertex_Shade_Index_Array(bool create = true)	{ return get_shade_indices(create); }
	const uint16 *				Get_Vertex_Bone_Links()								{ return get_bone_links(); }
	uint8 *						Get_Poly_Surface_Type_Array()						{ assert((Geometry.surface_types)); return Geometry.surface_types->data(); }
	uint8							Get_Poly_Surface_Type(int poly_index) const;

	Engine::Math::AxisAlignedBox3 Get_Bounding_Box() const noexcept;
	Engine::Math::Sphere3 Get_Bounding_Sphere() const noexcept;

	// exposed culling support
	bool							Has_Cull_Tree()											{ return CullTree != nullptr; }

	void Collect_Visible_Polygons(Engine::Math::Vector3 view_direction, std::vector<std::uint32_t> &polygons);
	void Collect_Visible_Polygons(const Engine::Math::OrientedBox3 &local_bounds, std::vector<std::uint32_t> &polygons);
	void Collect_Visible_Polygons(const Engine::Math::OrientedBox3 &local_bounds,
		Engine::Math::Vector3 view_direction, std::vector<std::uint32_t> &polygons);

	// containment
	bool Contains(const Engine::Math::Vector3 &point);

	// ray casting and intersection (takes a transform for the mesh). Note that unlike the W3DMeshRenderObject
	// functions with similar names, these work in object space.
	bool							Cast_Ray(W3DRayCastQuery & raytest);
	bool							Cast_AABox(W3DBoxCastQuery & boxtest);
	bool							Cast_OBBox(W3DOrientedBoxCastQuery & boxtest);
	bool							Intersect_OBBox(W3DOrientedBoxIntersectionQuery & boxtest);

	// This function analyses the transform passed into it to call various optimized functions if
	// the transform is identity or a simple rotation about the z-axis. Otherwise it transforms
	// boxtest into object space, performs an oob cast and transforms the result back.
	bool							Cast_World_Space_AABox(W3DBoxCastQuery & boxtest, const Engine::Math::AffineTransform3 &transform);

	// W3D File Format support.  Note that derived classes have to override these functions completely
	// so that they can handle their extra chunks.  Using these functions you could load mesh data out
	// of a W3D file while ignoring all materials, textures, etc.
	virtual bool			Load_W3D(ChunkLoadClass & cload);

	void Scale(const Engine::Math::Vector3 &sc);

protected:

	// internal accessor functions that are not exposed to the user (non-const...)
	TriIndex *					get_polys();
	Engine::Math::Vector3 *get_vert_normals();
	uint32 *						get_shade_indices(bool create = true);
	Engine::Math::Vector4 *		get_planes(bool create = true);
	uint16 *						get_bone_links(bool create = true);

	// Utility functions (used by collision/intersection functions)
	int cast_semi_infinite_axis_aligned_ray(const Engine::Math::Vector3 &start_point, int axis_dir, unsigned char &flags);

	bool cast_aabox_identity(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &trans);
	bool cast_aabox_z90(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &trans);
	bool cast_aabox_z180(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &trans);
	bool cast_aabox_z270(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &trans);

	bool							intersect_obbox_brute_force(W3DOrientedBoxIntersectionQuery & localtest);
	bool							cast_ray_brute_force(W3DRayCastQuery & raytest);
	bool							cast_aabox_brute_force(W3DBoxCastQuery & boxtest);
	bool							cast_obbox_brute_force(W3DOrientedBoxCastQuery & boxtest);

	// functions to recompute dirty normals and bounding volumes.
	virtual void				Compute_Plane_Equations(Engine::Math::Vector4 * array);
	virtual void Compute_Vertex_Normals(Engine::Math::Vector3 *array);
	virtual void Compute_Bounds(Engine::Math::Vector3 *verts);
	void							Generate_Culling_Tree();

	// W3D chunk reading
    static bool Can_Install_Geometry(const Assets::W3D::W3DMeshData& mesh);
    void Install_Geometry(const Assets::W3D::W3DMeshData& mesh);

	// functions to compute the deformed vertices of skins.
	// Destination pointers MUST point to arrays large enough to hold all vertices
	void get_deformed_vertices(Engine::Math::Vector3 *dst_vert, Engine::Math::Vector3 *dst_norm, const Graphics::ModelHierarchy *htree);
	void get_deformed_vertices(Engine::Math::Vector3 *dst_vert, const Graphics::ModelHierarchy *htree);

    Graphics::ModelGeometrySource<Engine::Math::Vector3, TriIndex, Engine::Math::Vector4, uint32> Geometry;
    int Flags = 0;
    char SortLevel = 0;
    uint32 W3dAttributes = 0;
	std::unique_ptr<Graphics::ModelBoundsTree> CullTree;

};

/*
** Inline functions for W3DMeshGeometry
*/
inline TriIndex * W3DMeshGeometry::get_polys()
{
	assert((Geometry.triangles));
	return Geometry.triangles->data();
}


inline uint32 * W3DMeshGeometry::get_shade_indices(bool create)
{
    return Geometry.Shade_Indices(create);
}

inline uint16 * W3DMeshGeometry::get_bone_links(bool create)
{
    return Geometry.Bone_Indices(create);
}

inline uint8 W3DMeshGeometry::Get_Poly_Surface_Type(int poly_index) const
{
	assert((Geometry.surface_types));
	assert((poly_index >= 0 && poly_index < Geometry.polygon_count));
	uint8 *type = Geometry.surface_types->data();
	return type[poly_index];
}
