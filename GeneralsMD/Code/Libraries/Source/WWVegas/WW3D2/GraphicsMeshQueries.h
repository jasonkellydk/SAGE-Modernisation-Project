#pragma once
#include "MeshGeometry.h"
#include "WWMath/colmathinlines.h"
#include "WWMath/tri.h"
import Assets.Math;
import Graphics.Scene.Models.BoundsTree;

// Borrow mesh geometry and translate query types; graphics owns traversal and
// leaf-result policy. No mesh or query lifetime is retained here.
namespace MeshQueryAdapter {
template<class Operation>
bool Triangle(MeshGeometryClass& mesh,std::uint32_t polygon,Operation&& operation)
{
    const auto* vertices=mesh.Get_Vertex_Array();
    const auto& indices=mesh.Get_Polygon_Array()[polygon];
    TriClass triangle;
    for(unsigned corner=0;corner<3;++corner)triangle.V[corner]=&vertices[indices[corner]];
#if (!OPTIMIZE_PLANEEQ_RAM)
    triangle.N=(Vector3*)&mesh.Get_Plane_Array()[polygon];
#else
    Vector3 normal;triangle.N=&normal;triangle.Compute_Normal();
#endif
    return operation(triangle);
}

template<class Test>
bool Cull(const Assets::Bounds3f& bounds,Test& test)
{
    return test.Cull(Vector3(bounds.minimum.x,bounds.minimum.y,bounds.minimum.z),
        Vector3(bounds.maximum.x,bounds.maximum.y,bounds.maximum.z));
}

inline bool Collide(RayCollisionTestClass& test,const TriClass& triangle)
{ return CollisionMath::Collide(test.Ray,triangle,test.Result); }
inline bool Collide(AABoxCollisionTestClass& test,const TriClass& triangle)
{ return CollisionMath::Collide(test.Box,test.Move,triangle,test.Result); }
inline bool Collide(OBBoxCollisionTestClass& test,const TriClass& triangle)
{ return CollisionMath::Collide(test.Box,test.Move,triangle,Vector3(0,0,0),test.Result); }

template<class Test>
bool Cast(const Graphics::ModelBoundsTree& tree,MeshGeometryClass& mesh,Test& test)
{
    return tree.Cast([&](const Assets::Bounds3f& bounds) { return Cull(bounds,test); },
        [&](std::uint32_t polygon) { return Triangle(mesh,polygon,[&](const TriClass& triangle) { return Collide(test,triangle); }); },
        [&] { return test.Result->StartBad; },
        [&](std::uint32_t polygon) { test.Result->SurfaceType=mesh.Get_Poly_Surface_Type(polygon); });
}

inline bool Intersects(const Graphics::ModelBoundsTree& tree,MeshGeometryClass& mesh,OBBoxIntersectionTestClass& test)
{
    return tree.Intersects([&](const Assets::Bounds3f& bounds) { return Cull(bounds,test); },
        [&](std::uint32_t polygon) { return Triangle(mesh,polygon,[&](const TriClass& triangle) {
            return CollisionMath::Intersection_Test(test.Box,triangle);
        }); });
}

inline void Collect(const Graphics::ModelBoundsTree& tree,MeshGeometryClass& mesh,const OBBoxClass& box,
    SimpleDynVecClass<uint32>& output,const Vector3* view=nullptr)
{
    tree.Visit([&](const Assets::Bounds3f& bounds) {
        AABoxClass node;
        node.Init_Min_Max(Vector3(bounds.minimum.x,bounds.minimum.y,bounds.minimum.z),
            Vector3(bounds.maximum.x,bounds.maximum.y,bounds.maximum.z));
        return !CollisionMath::Intersection_Test(box,node);
    },[&](std::span<const std::uint32_t> polygons) {
        for(const auto polygon:polygons)
            if(Triangle(mesh,polygon,[&](const TriClass& triangle) {
                return (!view || Vector3::Dot_Product(*triangle.N,*view)<0.0f) && CollisionMath::Intersection_Test(box,triangle);
            })) output.Add(polygon);
    });
}

inline int Count_Axis_Ray(const Graphics::ModelBoundsTree& tree,MeshGeometryClass& mesh,
    const Vector3& point,int axis_dir,unsigned char& flags)
{
    static const int axes[6]={0,0,1,1,2,2},first_axes[6]={1,1,2,2,0,0},second_axes[6]={2,2,0,0,1,1};
    static const int directions[6]={1,0,1,0,1,0};
    WWASSERT(axis_dir>=0 && axis_dir<6);
    const int axis=axes[axis_dir],first=first_axes[axis_dir],second=second_axes[axis_dir],direction=directions[axis_dir];
    flags=TRI_RAYCAST_FLAG_NONE;
    int count=0;
    tree.Visit([&](const Assets::Bounds3f& bounds) {
        const Vector3 minimum(bounds.minimum.x,bounds.minimum.y,bounds.minimum.z),maximum(bounds.maximum.x,bounds.maximum.y,bounds.maximum.z);
        const float ends[2]={-minimum[axis],maximum[axis]},start[2]={-point[axis],point[axis]};
        return point[first]<minimum[first] || point[second]<minimum[second] ||
            point[first]>maximum[first] || point[second]>maximum[second] || start[direction]>ends[direction];
    },[&](std::span<const std::uint32_t> polygons) {
        if(polygons.empty())return;
        const auto* vertices=mesh.Get_Vertex_Array();const auto* triangles=mesh.Get_Polygon_Array();
        const auto* planes=mesh.Get_Plane_Array();
        for(const auto polygon:polygons) {
            const auto& indices=triangles[polygon];
            count+=static_cast<unsigned>(Cast_Semi_Infinite_Axis_Aligned_Ray_To_Triangle(vertices[indices[0]],
                vertices[indices[1]],vertices[indices[2]],planes[polygon],point,axis,first,second,direction,flags));
        }
    });
    return count;
}
}
