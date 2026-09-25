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

#include <array>
#include <bit>
#include <ranges>
#include <limits>
#include <vector>
#include "W3DDevice/GameClient/W3DMeshGeometry.h"
#include "W3DDevice/GameClient/W3DMeshQueries.h"
#include "W3DDevice/GameClient/W3DCastQuery.h"
#include "W3DDevice/GameClient/W3DIntersectionQuery.h"
#include "WWLib/chunkio.h"


import Engine.Core.Math.Scalar;
import Engine.Core.Math.AxisAlignedBox3;
import Engine.Core.Math.Sphere3;
import Engine.Core.Math.RandomStream;
import Engine.Core.Math.AffineTransform3;
import engine.debug;
import Assets.Adapters.W3D.Chunks;
import Graphics.Scene.Models.Hierarchy;
import Graphics.Scene.Models.GeometryMath;
import Graphics.Scene.Models.GeometryQueries;
import Assets.Adapters.W3D.Geometry;
import Assets.Adapters.W3D.MeshData;
import Assets.Math;
import Assets.MeshBoundsTree;


W3DMeshGeometry::W3DMeshGeometry() = default;

W3DMeshGeometry::W3DMeshGeometry(const W3DMeshGeometry& that)
    : Geometry(that.Geometry), Flags(that.Flags), SortLevel(that.SortLevel), W3dAttributes(that.W3dAttributes),
      CullTree(that.CullTree ? std::make_unique<Graphics::ModelBoundsTree>(*that.CullTree) : nullptr)
{
}

W3DMeshGeometry& W3DMeshGeometry::operator=(const W3DMeshGeometry& that)
{
    if (this != &that) {
        Geometry = that.Geometry;
        Flags = that.Flags;
        SortLevel = that.SortLevel;
        W3dAttributes = that.W3dAttributes;
        CullTree = that.CullTree ? std::make_unique<Graphics::ModelBoundsTree>(*that.CullTree) : nullptr;
    }
    return *this;
}

W3DMeshGeometry::~W3DMeshGeometry()
{
    Reset_Geometry(0, 0);
}

void W3DMeshGeometry::Reset_Geometry(int polycount, int vertcount)
{
    Geometry.Reset(polycount, vertcount);
    Flags = 0;
    SortLevel = Assets::W3D::W3DMeshSortLevelNone;
    CullTree.reset();
}

const char * W3DMeshGeometry::Get_Name() const
{
    return Geometry.name ? Geometry.name->c_str() : nullptr;
}


void W3DMeshGeometry::Set_Name(const char * newname)
{
    Geometry.name = newname ? std::make_shared<const std::string>(newname) : nullptr;
}


const char * W3DMeshGeometry::Get_User_Text()
{
    return Geometry.user_text ? Geometry.user_text->c_str() : nullptr;
}


void W3DMeshGeometry::Set_User_Text(char * usertext)
{
    Geometry.user_text = usertext ? std::make_shared<const std::string>(usertext) : nullptr;
}


Engine::Math::AxisAlignedBox3 W3DMeshGeometry::Get_Bounding_Box() const noexcept
{
	return {Geometry.minimum, Geometry.maximum};
}


Engine::Math::Sphere3 W3DMeshGeometry::Get_Bounding_Sphere() const noexcept
{
	return {Geometry.sphere_center, Geometry.sphere_radius};
}


void W3DMeshGeometry::Collect_Visible_Polygons(Engine::Math::Vector3 view_direction,
    std::vector<std::uint32_t> &polygons)
{
    const MeshQueryAdapter::Triangles triangles(*this);
    Graphics::Collect_Model_Polygons(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return triangle.Normal().Dot(view_direction) < 0.0f;
        }); }, [&](std::uint32_t polygon) { polygons.push_back(polygon); });
}


void W3DMeshGeometry::Collect_Visible_Polygons(const Engine::Math::OrientedBox3 &local_bounds,
    std::vector<std::uint32_t> &polygons)
{
    if (CullTree) { MeshQueryAdapter::Collect(*CullTree, *this, local_bounds, polygons); return; }
    const MeshQueryAdapter::Triangles triangles(*this);
    Graphics::Collect_Model_Polygons(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return triangle.Intersects(local_bounds);
        }); }, [&](std::uint32_t polygon) { polygons.push_back(polygon); });
}


void W3DMeshGeometry::Collect_Visible_Polygons(const Engine::Math::OrientedBox3 &local_bounds,
    Engine::Math::Vector3 view_direction, std::vector<std::uint32_t> &polygons)
{
    if (CullTree) { MeshQueryAdapter::Collect(*CullTree, *this, local_bounds, polygons, &view_direction); return; }
    const MeshQueryAdapter::Triangles triangles(*this);
    Graphics::Collect_Model_Polygons(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return triangle.Normal().Dot(view_direction) < 0.0f
                && triangle.Intersects(local_bounds);
        }); }, [&](std::uint32_t polygon) { polygons.push_back(polygon); });
}


bool W3DMeshGeometry::Contains(const Engine::Math::Vector3 &point)
{
    return Graphics::Model_Contains_Point([&](int axis) {
        unsigned char flags = 0;
        const auto count = cast_semi_infinite_axis_aligned_ray(point, axis, flags);
        return Graphics::ModelAxisRayVote{static_cast<unsigned>(count),
            (flags & static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::TouchesEdge)) != 0,
            (flags & static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::StartsInside)) != 0};
    });
}


bool W3DMeshGeometry::Cast_Ray(W3DRayCastQuery & raytest)
{
	bool hit = false;

	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,raytest);
	} else {
		hit = cast_ray_brute_force(raytest);
	}

	return hit;
}


bool W3DMeshGeometry::Cast_AABox(W3DBoxCastQuery & boxtest)
{
	bool hit = false;

	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,boxtest);
	} else {
		hit = cast_aabox_brute_force(boxtest);
	}

	return hit;
}


bool W3DMeshGeometry::Cast_OBBox(W3DOrientedBoxCastQuery & boxtest)
{
	bool hit = false;

	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,boxtest);
	} else {
		hit = cast_obbox_brute_force(boxtest);
	}

	return hit;
}


bool W3DMeshGeometry::Intersect_OBBox(W3DOrientedBoxIntersectionQuery & boxtest)
{
	bool hit = false;

	if (CullTree) {
		hit = MeshQueryAdapter::Intersects(*CullTree,*this,boxtest);
	} else {
		hit = intersect_obbox_brute_force(boxtest);
	}

	return hit;
}


bool W3DMeshGeometry::Cast_World_Space_AABox(W3DBoxCastQuery & boxtest,
	const Engine::Math::AffineTransform3 &transform)
{
	/*
	** Attempt to classify the transform:
	** NOTE: This code assumes that the matrix is orthogonal!  Much code in W3D assumes
	** orthogonal matrices, if that convention is broken this code is also broken.
	** Basically what I'm doing here is doing the minimum number of compares needed
	** to identify a transform which is a 90 degree rotation about the z axis.
	** TODO: cache some transform flags somewhere to reduce the number of times
	** that these compares are done
	*/
	bool hit = false;

	if ((transform.elements[0] == 1.0f) && (transform.elements[5] == 1.0f)) {

		hit = cast_aabox_identity(boxtest, transform.Translation() * -1.0f);

	} else if ((transform.elements[1] == -1.0f) && (transform.elements[4] == 1.0f)) {

		// this mesh has been rotated 90 degrees about z
		hit = cast_aabox_z90(boxtest, transform.Translation() * -1.0f);

	} else if ((transform.elements[0] == -1.0f) && (transform.elements[5] == -1.0f)) {

		// this mesh has been rotated 180
		hit = cast_aabox_z180(boxtest, transform.Translation() * -1.0f);

	} else if ((transform.elements[1] == 1.0f) && (transform.elements[4] == -1.0f)) {

		// this mesh has been rotated 270
		hit = cast_aabox_z270(boxtest, transform.Translation() * -1.0f);

	} else {

		/*
		** Ok, we fell through to here, that means there is a more general
		** transform on this mesh.  In this case, I create a new test which
		** is an oriented box test in the coordinate system of the mesh and cast it.
		*/
		const auto world_to_obj = transform.Inverse();
		if (!world_to_obj)
			return false;
		W3DOrientedBoxCastQuery obbox(boxtest, *world_to_obj);

		if (CullTree) {
			hit = MeshQueryAdapter::Cast(*CullTree,*this,obbox);
		} else {
			hit = cast_obbox_brute_force(obbox);
		}

		/*
		** now, we must transform the results of the test back to the original
		** coordinate system.
		*/
		if (hit) {
			obbox.Result->normal = transform.Transform_Vector(obbox.Result->normal);
			if (boxtest.Result->compute_contact_point) {
				obbox.Result->contact_point = transform.Transform_Point(obbox.Result->contact_point);
			}
		}
	}

	return hit;
}


int W3DMeshGeometry::cast_semi_infinite_axis_aligned_ray(const Engine::Math::Vector3 &start_point, int axis_dir,
	unsigned char & flags)
{
    if (CullTree) return MeshQueryAdapter::Count_Axis_Ray(*CullTree, *this, start_point, axis_dir, flags);
    engine::debug::assert_condition((axis_dir >= 0 && axis_dir < 6), "axis_dir >= 0 && axis_dir < 6", __FILE__, __LINE__, "assertion failed");
    const int axis = axis_dir / 2, first = (axis + 1) % 3, second = (axis + 2) % 3;
    const int direction = (axis_dir & 1) == 0;
    const auto* vertices = Peek_Vertex_Array();
    const auto* triangles = Get_Polygon_Array();
    const auto* planes = Get_Plane_Array();
    flags = 0;
    return Graphics::Count_Model_Axis_Intersections(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())), [&](std::uint32_t polygon) {
        const auto& indices = triangles[polygon];
        const auto hit = Engine::Math::Triangle3::Intersect_Semi_Infinite_Axis_Ray(
            vertices[indices[0]], vertices[indices[1]],
            vertices[indices[2]], planes[polygon], start_point,
            axis, first, second, direction != 0);
        if (hit.touches_edge)
            flags |= static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::TouchesEdge);
        if (hit.starts_inside)
            flags |= static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::StartsInside);
        return hit.intersects;
    });
}


bool W3DMeshGeometry::cast_aabox_identity(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &translation)
{
	// transform the test into the mesh's coordinate system
	W3DBoxCastQuery newbox(boxtest);
	newbox.Translate(translation);

	// cast the box against the mesh
	if (CullTree) {
		return MeshQueryAdapter::Cast(*CullTree,*this,newbox);
	} else {
		return cast_aabox_brute_force(newbox);
	}
}


bool W3DMeshGeometry::cast_aabox_z90(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &translation)
{
	// transform the test into the mesh's coordinate system
	W3DBoxCastQuery newbox(boxtest);
	newbox.Translate(translation);
	newbox.Rotate(W3DBoxCastQuery::ROTATE_Z270);

	// cast the box against the mesh, using culling if possible
	bool hit;
	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,newbox);
	} else {
		hit = cast_aabox_brute_force(newbox);
	}

	// if we hit something, we need to rotate the normal back out of the mesh coordinate system
	if (hit) {
		// rotating the normal by 90 degrees about Z
		const float tmp = boxtest.Result->normal.x;
		boxtest.Result->normal.x = -boxtest.Result->normal.y;
		boxtest.Result->normal.y = tmp;
	}

	return hit;
}


bool W3DMeshGeometry::cast_aabox_z180(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &translation)
{
	// transform the test into the meshes coordinate system
	W3DBoxCastQuery newbox(boxtest);
	newbox.Translate(translation);
	newbox.Rotate(W3DBoxCastQuery::ROTATE_Z180);

	// cast the box against the mesh, using culling if possible
	bool hit;
	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,newbox);
	} else {
		hit = cast_aabox_brute_force(newbox);
	}

	// if we hit something, we need to rotate the normal back out of the mesh coordinate system
	if (hit) {
		// rotating the normal by 180 degrees about Z
		boxtest.Result->normal.x = -boxtest.Result->normal.x;
		boxtest.Result->normal.y = -boxtest.Result->normal.y;
	}

	return hit;
}


bool W3DMeshGeometry::cast_aabox_z270(W3DBoxCastQuery &boxtest, const Engine::Math::Vector3 &translation)
{
	// transform the test into the mesh's coordinate system
	W3DBoxCastQuery newbox(boxtest);
	newbox.Translate(translation);
	newbox.Rotate(W3DBoxCastQuery::ROTATE_Z90);

	// cast the box against the mesh, using culling if possible
	bool hit;
	if (CullTree) {
		hit = MeshQueryAdapter::Cast(*CullTree,*this,newbox);
	} else {
		hit = cast_aabox_brute_force(newbox);
	}

	// if we hit something, we need to rotate the normal back out of the mesh coordinate system
	if (hit) {
		// rotating the normal by 270 degrees about Z
		const float tmp = boxtest.Result->normal.x;
		boxtest.Result->normal.x = boxtest.Result->normal.y;
		boxtest.Result->normal.y = -tmp;
	}

	return hit;
}


bool W3DMeshGeometry::intersect_obbox_brute_force(W3DOrientedBoxIntersectionQuery & localtest)
{
    const MeshQueryAdapter::Triangles triangles(*this);
    return Graphics::Intersect_Model_Polygons(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
			return triangle.Intersects(localtest.Box);
        }); });
}


bool W3DMeshGeometry::cast_ray_brute_force(W3DRayCastQuery & raytest)
{
    const MeshQueryAdapter::Triangles triangles(*this);
    return Graphics::Cast_Model_Ray(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return MeshQueryAdapter::Collide(raytest, triangle);
        }); }, [&] { return raytest.Result->starts_overlapping; },
        [&](std::uint32_t polygon) { raytest.Result->surface_type = Get_Poly_Surface_Type(polygon); });
}


bool W3DMeshGeometry::cast_aabox_brute_force(W3DBoxCastQuery & boxtest)
{
    const MeshQueryAdapter::Triangles triangles(*this);
    return Graphics::Cast_Model_Volume(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return MeshQueryAdapter::Collide(boxtest, triangle);
        }); }, [&] { return boxtest.Result->starts_overlapping; },
        [&](std::uint32_t polygon) { boxtest.Result->surface_type = Get_Poly_Surface_Type(polygon); });
}


bool W3DMeshGeometry::cast_obbox_brute_force(W3DOrientedBoxCastQuery & boxtest)
{
    const MeshQueryAdapter::Triangles triangles(*this);
    return Graphics::Cast_Model_Volume(std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(Get_Polygon_Count())),
        [&](std::uint32_t polygon) { return triangles.Test(polygon, [&](const Engine::Math::Triangle3& triangle) {
            return MeshQueryAdapter::Collide(boxtest, triangle);
        }); }, [&] { return boxtest.Result->starts_overlapping; },
        [&](std::uint32_t polygon) { boxtest.Result->surface_type = Get_Poly_Surface_Type(polygon); });
}


void W3DMeshGeometry::Compute_Plane_Equations(Engine::Math::Vector4 * peq)
{
    Graphics::Compute_Model_Planes(std::span<const Engine::Math::Vector3>(*Geometry.positions),
        std::span<const TriIndex>(*Geometry.triangles), std::span(peq, static_cast<std::size_t>(Geometry.polygon_count)));
    Set_Flag(DIRTY_PLANES, false);
}


void W3DMeshGeometry::Compute_Vertex_Normals(Engine::Math::Vector3 *vnorm)
{
    Geometry.revision.Invalidate();
    engine::debug::assert_condition((vnorm), "vnorm", __FILE__, __LINE__, "assertion failed");
    if (Geometry.polygon_count == 0 || Geometry.vertex_count == 0) return;
    const auto* planes = Get_Plane_Array();
    const auto* shade_indices = Get_Vertex_Shade_Index_Array(false);
    Graphics::Compute_Model_Normals(std::span<const TriIndex>(*Geometry.triangles),
        std::span(planes, static_cast<std::size_t>(Geometry.polygon_count)),
        std::span(shade_indices, shade_indices ? static_cast<std::size_t>(Geometry.vertex_count) : 0),
        std::span(vnorm, static_cast<std::size_t>(Geometry.vertex_count)));
    Set_Flag(DIRTY_VNORMALS, false);
}


void W3DMeshGeometry::Compute_Bounds(Engine::Math::Vector3 *verts)
{
    if (!verts && Geometry.positions) verts = Geometry.positions->data();
    if (Graphics::Compute_Model_Bounds(std::span<const Engine::Math::Vector3>(verts, static_cast<std::size_t>(Geometry.vertex_count)),
        Geometry.minimum, Geometry.maximum, Geometry.sphere_center, Geometry.sphere_radius))
        Set_Flag(DIRTY_BOUNDS, false);
}


Engine::Math::Vector3 *W3DMeshGeometry::get_vert_normals()
{
	engine::debug::assert_condition((Geometry.normals != nullptr), "Geometry.normals != nullptr", __FILE__, __LINE__, "assertion failed");
    return Geometry.normals->data();
}


const Engine::Math::Vector3 *W3DMeshGeometry::Get_Vertex_Normal_Array()
{
    if (Get_Flag(DIRTY_VNORMALS)) Compute_Vertex_Normals(get_vert_normals());
    return get_vert_normals();
}


Engine::Math::Vector4 * W3DMeshGeometry::get_planes(bool create)
{
    return Geometry.Planes();
}


const Engine::Math::Vector4 * W3DMeshGeometry::Get_Plane_Array(bool create)
{
    auto* planes = get_planes(create);
    Compute_Plane_Equations(planes);
    return planes;
}

void W3DMeshGeometry::Generate_Culling_Tree()
{

 std::vector<Assets::Vector3f> vertices(Geometry.vertex_count);
 for (int i = 0; i < Geometry.vertex_count; ++i) {
  const auto& source = Geometry.positions->data()[i];
  vertices[i] = {source.x, source.y, source.z};
 }
 std::vector<std::array<std::uint32_t,3>> triangles(Geometry.polygon_count);
 for (int i = 0; i < Geometry.polygon_count; ++i) {
  const auto& source = Geometry.triangles->data()[i];
  triangles[i] = {source[0], source[1], source[2]};
 }
 // Seed split selection from the geometry itself. The builder consumes this
 // stream in a fixed traversal order, so equivalent mesh data produces the
 // same tree independent of process-global RNG state or load order.
 std::uint64_t seed = 14695981039346656037ull;
 const auto mix = [&seed](std::uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte) {
   seed ^= (value >> (byte * 8)) & 0xffu;
   seed *= 1099511628211ull;
  }
 };
 for (const auto& vertex : vertices) {
  mix(std::bit_cast<std::uint32_t>(vertex.x));
  mix(std::bit_cast<std::uint32_t>(vertex.y));
  mix(std::bit_cast<std::uint32_t>(vertex.z));
 }
 for (const auto& triangle : triangles)
  for (const auto index : triangle) mix(index);
 Engine::Math::RandomStream split_samples(seed);
 Assets::MeshBoundsTree tree;
 const bool built = Assets::Build_Mesh_Bounds_Tree(vertices, triangles,
  [&split_samples] { return split_samples.NextUInt32(); }, tree);
 engine::debug::assert_condition((built), "built", __FILE__, __LINE__, "assertion failed");
 if (!built) return;
 engine::debug::invariant((CullTree == nullptr), "CullTree == nullptr", __FILE__, __LINE__, "W3DMeshGeometry::Generate_Culling_Tree: Leaking CullTree");
 CullTree = std::make_unique<Graphics::ModelBoundsTree>(std::move(tree));
}


bool W3DMeshGeometry::Can_Install_Geometry(const Assets::W3D::W3DMeshData& mesh) {
    if (mesh.header.vertex_count > static_cast<unsigned>((std::numeric_limits<int>::max)())
        || mesh.header.triangle_count > static_cast<unsigned>((std::numeric_limits<int>::max)() / 3)) return false;
    for (const auto& triangle : mesh.triangles)
        for (const auto index : triangle.indices)
            if (index > (std::numeric_limits<unsigned short>::max)()) return false;
    return true;
}

void W3DMeshGeometry::Install_Geometry(const Assets::W3D::W3DMeshData& mesh) {
    const auto& header = mesh.header;
    W3dAttributes = header.attributes;
    SortLevel = static_cast<char>(header.sort_level);
    const auto name = header.container_name.empty() ? header.name : header.container_name + "." + header.name;
    Set_Name(name.c_str());
    Geometry.minimum = {header.bounds.minimum.x, header.bounds.minimum.y, header.bounds.minimum.z};
    Geometry.maximum = {header.bounds.maximum.x, header.bounds.maximum.y, header.bounds.maximum.z};
    Geometry.sphere_center = {header.sphere_center.x, header.sphere_center.y, header.sphere_center.z};
    Geometry.sphere_radius = header.sphere_radius;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const auto& value = mesh.positions[i];
        (*Geometry.positions)[i] = {value.x, value.y, value.z};
    }
    for (std::size_t i = 0; i < mesh.normals.size(); ++i) {
        const auto& value = mesh.normals[i];
        (*Geometry.normals)[i] = {value.x, value.y, value.z};
    }
    auto* planes = Geometry.Planes();
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
        const auto& value = mesh.triangles[i];
        (*Geometry.triangles)[i] = {static_cast<unsigned short>(value.indices[0]),
            static_cast<unsigned short>(value.indices[1]), static_cast<unsigned short>(value.indices[2])};
        (*Geometry.surface_types)[i] = static_cast<std::uint8_t>(value.surface_type);
        planes[i] = {value.normal.x, value.normal.y, value.normal.z, -value.distance};
    }
    if (!mesh.shade_indices.empty()) {
        auto* shades = Geometry.Shade_Indices(true);
        for (std::size_t i = 0; i < mesh.shade_indices.size(); ++i) shades[i] = mesh.shade_indices[i];
    }
    if (mesh.bone_indices) {
        Geometry.bone_indices = std::make_shared<std::vector<std::uint16_t>>(*mesh.bone_indices);
        Set_Flag(SKIN, true);
    }
    if (mesh.user_text) Geometry.user_text = std::make_shared<const std::string>(*mesh.user_text);
    if (mesh.bounds_tree) CullTree = std::make_unique<Graphics::ModelBoundsTree>(*mesh.bounds_tree);
    if (header.version >= Assets::W3D::W3DMeshVersion4_1) {
        switch (header.attributes & Assets::W3D::W3DMeshAttributeGeometryTypeMask) {
        case Assets::W3D::W3DMeshAttributeGeometryTypeCameraAligned: Set_Flag(ALIGNED, true); break;
        case Assets::W3D::W3DMeshAttributeGeometryTypeCameraOriented: Set_Flag(ORIENTED, true); break;
        case Assets::W3D::W3DMeshAttributeGeometryTypeSkin: Set_Flag(SKIN, true); break;
        default: break;
        }
    }
    if (header.attributes & Assets::W3D::W3DMeshAttributeTwoSided) Set_Flag(TWO_SIDED, true);
    if (header.attributes & Assets::W3D::W3DMeshAttributeCastShadow) Set_Flag(CAST_SHADOW, true);
    Geometry.revision.Invalidate();
}

bool W3DMeshGeometry::Load_W3D(ChunkLoadClass& cload) {
    std::vector<std::byte> bytes(cload.Cur_Chunk_Length());
    if (cload.Read(bytes.data(), static_cast<unsigned>(bytes.size())) != bytes.size()) return false;
    Assets::W3D::W3DMeshData mesh;
    std::string error;
    if (!Assets::W3D::W3DRead_Mesh_Data(bytes, Assets::W3D::W3DMeshPrelighting::Unlit, mesh, error)
        || !Can_Install_Geometry(mesh)) return false;
    Reset_Geometry(static_cast<int>(mesh.header.triangle_count), static_cast<int>(mesh.header.vertex_count));
    Install_Geometry(mesh);
    if ((W3dAttributes & Assets::W3D::W3DMeshAttributeCollisionTypeMask) && !CullTree)
        Generate_Culling_Tree();
    return true;
}

void W3DMeshGeometry::Scale(const Engine::Math::Vector3 &sc)
{
    Geometry.revision.Invalidate();
	engine::debug::assert_condition((Geometry.positions != nullptr), "Geometry.positions != nullptr", __FILE__, __LINE__, "assertion failed");
    if (Graphics::Scale_Model_Geometry(std::span<Engine::Math::Vector3>(*Geometry.positions), Geometry.minimum, Geometry.maximum,
        Geometry.sphere_center, Geometry.sphere_radius, sc)) Set_Flag(DIRTY_VNORMALS, true);
    Set_Flag(DIRTY_PLANES, true);
	// the cull tree is invalid, release it and make a new one
	if (CullTree) {
		// If the scale is uniform, we can scale the cull tree, which is a lot faster than creating a new one
		if (fabs(sc[0]-sc[1])<Engine::Math::DefaultTolerance && fabs(sc[0]-sc[2])<Engine::Math::DefaultTolerance) {
			// create a copy of the old culltree
			CullTree = std::make_unique<Graphics::ModelBoundsTree>(*CullTree);
			CullTree->Scale(sc[0]);
		}
		else {
			CullTree.reset();
			Generate_Culling_Tree();
		}
	}
}


// Destination pointers MUST point to arrays large enough to hold all vertices
void W3DMeshGeometry::get_deformed_vertices(Engine::Math::Vector3 *dst_vert,const Graphics::ModelHierarchy * htree)
{
    engine::debug::assert_condition((htree && Geometry.positions && Geometry.bone_indices), "htree && Geometry.positions && Geometry.bone_indices", __FILE__, __LINE__, "assertion failed");
    Graphics::Deform_Model_Geometry(std::span<const Engine::Math::Vector3>(*Geometry.positions), std::span<const Engine::Math::Vector3>{},
        std::span<const std::uint16_t>(*Geometry.bone_indices), *htree,
        std::span(dst_vert, static_cast<std::size_t>(Geometry.vertex_count)));
}


// Destination pointers MUST point to arrays large enough to hold all vertices
void W3DMeshGeometry::get_deformed_vertices(Engine::Math::Vector3 *dst_vert, Engine::Math::Vector3 *dst_norm,const Graphics::ModelHierarchy * htree)
{
    engine::debug::assert_condition((htree && Geometry.positions && Geometry.normals && Geometry.bone_indices), "htree && Geometry.positions && Geometry.normals && Geometry.bone_indices", __FILE__, __LINE__, "assertion failed");
    Graphics::Deform_Model_Geometry(std::span<const Engine::Math::Vector3>(*Geometry.positions), std::span<const Engine::Math::Vector3>(*Geometry.normals),
        std::span<const std::uint16_t>(*Geometry.bone_indices), *htree,
        std::span(dst_vert, static_cast<std::size_t>(Geometry.vertex_count)),
        std::span(dst_norm, static_cast<std::size_t>(Geometry.vertex_count)));
}
