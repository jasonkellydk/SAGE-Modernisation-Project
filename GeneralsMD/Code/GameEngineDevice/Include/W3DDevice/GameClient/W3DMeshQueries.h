#pragma once

#include <cassert>
#include <cstdint>
#include <vector>
#include "W3DDevice/GameClient/W3DMeshGeometry.h"
#include "W3DDevice/GameClient/W3DCastQuery.h"
#include "W3DDevice/GameClient/W3DIntersectionQuery.h"
import Assets.Math;
import Graphics.Scene.Models.GeometryQueries;
import Graphics.Scene.Models.BoundsTree;
import Engine.Core.Math.Triangle3;

// Borrow mesh geometry and translate query types; graphics owns traversal and
// leaf-result policy. No mesh or query lifetime is retained here.
namespace MeshQueryAdapter {
class Triangles final {
public:
    explicit Triangles(W3DMeshGeometry& mesh)
        : m_positions(mesh.Peek_Vertex_Array()), m_triangles(mesh.Get_Polygon_Array()) {}
    template<class Operation>
    bool Test(std::uint32_t polygon, Operation&& operation) const {
        const auto& indices = m_triangles[polygon];
        const Engine::Math::Triangle3 triangle{m_positions[indices[0]],
            m_positions[indices[1]], m_positions[indices[2]]};
        return operation(triangle);
    }
private:
    const Engine::Math::Vector3* m_positions;
    const TriIndex* m_triangles;
};

template<class Test>
bool Cull(const Assets::Bounds3f& bounds,Test& test)
{
    return test.Cull({
        {bounds.minimum.x,bounds.minimum.y,bounds.minimum.z},
        {bounds.maximum.x,bounds.maximum.y,bounds.maximum.z}});
}

inline bool Collide(W3DRayCastQuery& test,const Engine::Math::Triangle3& triangle)
{
    const auto intersection = Engine::Math::Triangle3::Intersect_Segment(
        test.Ray.start, test.Ray.end,
        triangle.first, triangle.second, triangle.third);
    if (!intersection || intersection->fraction >= test.Result->fraction) return false;
    test.Result->fraction = intersection->fraction;
    test.Result->normal = intersection->normal;
    if (test.Result->compute_contact_point) {
        test.Result->contact_point = intersection->point;
    }
    return true;
}
inline bool Collide(W3DBoxCastQuery& test,const Engine::Math::Triangle3& triangle)
{
    const auto hit = triangle.Sweep({test.Box.Center(), test.Box.Extent()}, test.Move);
    if (!hit) return false;
    if (hit->starts_overlapping) test.Result->starts_overlapping = true;
    else if (hit->fraction > test.Result->fraction || hit->fraction >= 1.0f) return false;
    test.Result->fraction = hit->starts_overlapping ? 0.0f : hit->fraction;
    test.Result->normal = hit->normal;
    if (test.Result->compute_contact_point)
        test.Result->contact_point = hit->point;
    return true;
}
inline bool Collide(W3DOrientedBoxCastQuery& test,const Engine::Math::Triangle3& triangle)
{
    const auto hit = triangle.Sweep(test.Box, test.Move);
    if (!hit) return false;
    if (hit->starts_overlapping) test.Result->starts_overlapping = true;
    else if (hit->fraction > test.Result->fraction || hit->fraction >= 1.0f) return false;
    test.Result->fraction = hit->starts_overlapping ? 0.0f : hit->fraction;
    test.Result->normal = hit->normal;
    if (test.Result->compute_contact_point)
        test.Result->contact_point = hit->point;
    return true;
}

template<class Test>
bool Cast(const Graphics::ModelBoundsTree& tree,W3DMeshGeometry& mesh,Test& test)
{
    const Triangles triangles(mesh);
    return tree.Cast([&](const Assets::Bounds3f& bounds) { return Cull(bounds,test); },
        [&](std::uint32_t polygon) { return triangles.Test(polygon,[&](const Engine::Math::Triangle3& triangle) { return Collide(test,triangle); }); },
        [&] { return test.Result->starts_overlapping; },
        [&](std::uint32_t polygon) { test.Result->surface_type=mesh.Get_Poly_Surface_Type(polygon); });
}

inline bool Intersects(const Graphics::ModelBoundsTree& tree,W3DMeshGeometry& mesh,W3DOrientedBoxIntersectionQuery& test)
{
    const Triangles triangles(mesh);
    return tree.Intersects([&](const Assets::Bounds3f& bounds) { return Cull(bounds,test); },
        [&](std::uint32_t polygon) { return triangles.Test(polygon,[&](const Engine::Math::Triangle3& triangle) {
            return triangle.Intersects(test.Box);
        }); });
}

inline void Collect(const Graphics::ModelBoundsTree& tree,W3DMeshGeometry& mesh,
    const Engine::Math::OrientedBox3& box, std::vector<std::uint32_t>& output,
    const Engine::Math::Vector3* view=nullptr)
{
    const Triangles triangles(mesh);
    tree.Visit([&](const Assets::Bounds3f& bounds) {
        const Engine::Math::OrientedBox3 node{{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f}, {
            (bounds.maximum.x - bounds.minimum.x) * 0.5f,
            (bounds.maximum.y - bounds.minimum.y) * 0.5f,
            (bounds.maximum.z - bounds.minimum.z) * 0.5f},
            {{{1.0f,0.0f,0.0f},{0.0f,1.0f,0.0f},{0.0f,0.0f,1.0f}}}};
        return !box.Intersects(node);
    },[&](std::span<const std::uint32_t> polygons) {
        Graphics::Collect_Model_Polygons(polygons, [&](std::uint32_t polygon) {
            return triangles.Test(polygon,[&](const Engine::Math::Triangle3& triangle) {
                return (!view || triangle.Normal().Dot(*view) < 0.0f)
                    && triangle.Intersects(box);
            });
        }, [&](std::uint32_t polygon) { output.push_back(polygon); });
    });
}

inline int Count_Axis_Ray(const Graphics::ModelBoundsTree& tree,W3DMeshGeometry& mesh,
    const Engine::Math::Vector3& point,int axis_dir,unsigned char& flags)
{
    static const int axes[6]={0,0,1,1,2,2},first_axes[6]={1,1,2,2,0,0},second_axes[6]={2,2,0,0,1,1};
    static const int directions[6]={1,0,1,0,1,0};
    assert((axis_dir>=0 && axis_dir<6));
    const int axis=axes[axis_dir],first=first_axes[axis_dir],second=second_axes[axis_dir],direction=directions[axis_dir];
    flags=0;
    int count=0;
    const auto* vertices=mesh.Peek_Vertex_Array();
    const auto* triangles=mesh.Get_Polygon_Array();
    const auto* planes=mesh.Get_Plane_Array();
    tree.Visit([&](const Assets::Bounds3f& bounds) {
        const Engine::Math::Vector3 minimum{bounds.minimum.x,bounds.minimum.y,bounds.minimum.z};
        const Engine::Math::Vector3 maximum{bounds.maximum.x,bounds.maximum.y,bounds.maximum.z};
        const float ends[2]={-minimum[axis],maximum[axis]},start[2]={-point[axis],point[axis]};
        return point[first]<minimum[first] || point[second]<minimum[second] ||
            point[first]>maximum[first] || point[second]>maximum[second] || start[direction]>ends[direction];
    },[&](std::span<const std::uint32_t> polygons) {
        count += Graphics::Count_Model_Axis_Intersections(polygons, [&](std::uint32_t polygon) {
            const auto& indices=triangles[polygon];
            const auto hit = Engine::Math::Triangle3::Intersect_Semi_Infinite_Axis_Ray(
                vertices[indices[0]], vertices[indices[1]],
                vertices[indices[2]], planes[polygon], point,
                axis, first, second, direction != 0);
            if (hit.touches_edge)
                flags |= static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::TouchesEdge);
            if (hit.starts_inside)
                flags |= static_cast<unsigned char>(Engine::Math::Triangle3::AxisRayFlag::StartsInside);
            return hit.intersects;
        });
    });
    return count;
}
}
