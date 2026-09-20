module;
#include <cmath>

export module engine.navigation.spatial.collision.cylinder_contact;

export namespace navigation::collision {
struct Point { float x=0,y=0,z=0; };
struct Contact { Point position,normal; };
struct Cylinder { Point position; float radius=0,height=0; };

inline Point unitDirection(Point direction) {
    const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    return length>0?Point{direction.x/length,direction.y/length,direction.z/length}:Point{1,0,0};
}

// The caller has established overlap. Choose a complete side contact even
// when the centers coincide, where there is no geometric preferred direction.
// Contact traversal supplies the opposite normal to the second participant.
inline Contact overlapContact(Cylinder a,Cylinder b) {
    const float dx=b.position.x-a.position.x,dy=b.position.y-a.position.y;
    const Point normal=unitDirection({dx,dy,0});
    const float z=b.position.z>a.position.z?
        (b.position.z+a.position.z+a.height)*0.5f:
        (a.position.z+b.position.z+b.height)*0.5f;
    return {{a.position.x+normal.x*a.radius,a.position.y+normal.y*a.radius,z},normal};
}

inline bool cylinderContact(Cylinder a,Cylinder b,Contact* contact) {
    const float radius=a.radius+b.radius;
    const float dx=b.position.x-a.position.x,dy=b.position.y-a.position.y;
    const float distanceSquared=dx*dx+dy*dy;
    const float radiusSquared=radius*radius;
    const float topA=a.position.z+a.height,topB=b.position.z+b.height;
    if (a.radius<0 || b.radius<0 || a.height<0 || b.height<0 ||
        !std::isfinite(distanceSquared) || !std::isfinite(radiusSquared) ||
        !std::isfinite(topA) || !std::isfinite(topB) ||
        !(topA>=b.position.z && a.position.z<=topB) || distanceSquared>radiusSquared)
        return false;
    if (contact) *contact=overlapContact(a,b);
    return true;
}
}
