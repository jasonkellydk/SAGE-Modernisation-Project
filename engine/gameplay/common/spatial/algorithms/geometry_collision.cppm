export module engine.gameplay.common.spatial.algorithms.geometry_collision;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// Whether two bodies touch in 3D (the original's PartitionFilterWouldCollide: the z ranges first, then its collide test
// per pair of shapes). A sphere of radius `major`; a cylinder of radius `major` and `height` up; a box `major` along its
// angle by `minor` across (half extents), `height` up. As the original: a circle against a box is tested as a square of
// its radius turned the circle's way; boxes touch when a corner of either lies within the other; a sphere against a
// cylinder or box is cut at the other's bottom or top when it pokes past it.
export namespace engine::gameplay
{
enum class BodyShape : std::uint8_t
{
	Sphere,
	Cylinder,
	Box,
};

struct CollisionBody
{
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle angle;
	BodyShape shape{BodyShape::Sphere};
	Engine::Math::Fixed major;
	Engine::Math::Fixed minor;
	Engine::Math::Fixed height;
};

namespace geometry_collision_detail
{
using Engine::Math::Fixed;

// GeometryInfo::getMaxHeightAbovePosition.
inline Fixed Top(const CollisionBody &body) noexcept { return body.shape == BodyShape::Sphere ? body.major : body.height; }

// testRotatedPointsAgainstRect: whether any of the points lies within `rect` (a sphere's minor is its major).
inline bool AnyPointInside(const std::array<Engine::Math::FixedVector2, 4> &points, const CollisionBody &rect) noexcept
{
	const Fixed major = rect.major;
	const Fixed minor = rect.shape == BodyShape::Sphere ? rect.major : rect.minor;
	const Engine::Math::TurnAngle back{0u - rect.angle.units};
	const Fixed c = Engine::Math::Cos(back), s = Engine::Math::Sin(back);
	for (const Engine::Math::FixedVector2 &point : points)
	{
		const Fixed x = point.x - rect.position.x, y = point.y - rect.position.y;
		if (Engine::Math::Abs(x * c - y * s) <= major && Engine::Math::Abs(x * s + y * c) <= minor)
			return true;
	}
	return false;
}

// rectToFourPoints.
inline std::array<Engine::Math::FixedVector2, 4> Corners(const CollisionBody &rect) noexcept
{
	const Fixed c = Engine::Math::Cos(rect.angle), s = Engine::Math::Sin(rect.angle);
	const Fixed exc = rect.major * c, eyc = rect.minor * c, exs = rect.major * s, eys = rect.minor * s;
	const auto &p = rect.position;
	return {Engine::Math::FixedVector2{p.x - exc - eys, p.y + eyc - exs}, Engine::Math::FixedVector2{p.x + exc - eys, p.y + eyc + exs},
		Engine::Math::FixedVector2{p.x - exc + eys, p.y - eyc - exs}, Engine::Math::FixedVector2{p.x + exc + eys, p.y - eyc + exs}};
}

// xy_collideTest_Rect_Rect.
inline bool RectRect(const CollisionBody &a, const CollisionBody &b) noexcept
{
	return AnyPointInside(Corners(a), b) || AnyPointInside(Corners(b), a);
}

// xy_collideTest_Rect_Circle: the circle as a square of its radius.
inline bool RectCircle(const CollisionBody &rect, const CollisionBody &circle) noexcept
{
	CollisionBody square = circle;
	square.minor = square.major;
	return RectRect(rect, square);
}

inline bool CircleRect(const CollisionBody &circle, const CollisionBody &rect) noexcept { return RectCircle(rect, circle); }

// xy_collideTest_Circle_Circle.
inline bool CircleCircle(const CollisionBody &a, const CollisionBody &b) noexcept
{
	const Fixed reach = a.major + b.major;
	return Engine::Math::DistanceSquared(a.position.XY(), b.position.XY()) <= reach * reach;
}

using XyTest = bool (*)(const CollisionBody &, const CollisionBody &) noexcept;

// z_collideTest_Sphere_Nonsphere: a sphere within the other's height is tested flat; one poking past its bottom or top
// as the circle it cuts there.
inline bool SphereNonsphere(XyTest xy, const CollisionBody &sphere, const CollisionBody &other) noexcept
{
	const Fixed bottom = other.position.z, top = other.position.z + Top(other);
	if (sphere.position.z >= bottom && sphere.position.z <= top)
		return xy(sphere, other);
	const auto cut = [&](Fixed at, Fixed depth) {
		CollisionBody slice = sphere;
		slice.position.z = at;
		slice.major = Engine::Math::Sqrt(sphere.major * sphere.major - depth * depth);
		return xy(slice, other);
	};
	if (sphere.position.z < bottom && sphere.position.z + sphere.major >= bottom)
		return cut(bottom, bottom - sphere.position.z);
	if (sphere.position.z > top && sphere.position.z - sphere.major <= top)
		return cut(top, sphere.position.z - top);
	return false;
}

// z_collideTest_Nonsphere_Nonsphere: centres closer than the smallest of the four radii touch outright.
inline bool NonsphereNonsphere(XyTest xy, const CollisionBody &a, const CollisionBody &b) noexcept
{
	const Fixed smallest = std::min({a.major, a.minor, b.major, b.minor});
	if (smallest * smallest > Engine::Math::DistanceSquared(a.position.XY(), b.position.XY()))
		return true;
	return xy(a, b);
}
}

// PartitionFilterWouldCollide::allow: `a` against `b`.
inline bool WouldCollide(const CollisionBody &a, const CollisionBody &b) noexcept
{
	using namespace geometry_collision_detail;
	if (!(a.position.z + Top(a) >= b.position.z && a.position.z <= b.position.z + Top(b)))
		return false;
	switch (a.shape)
	{
	case BodyShape::Sphere:
		switch (b.shape)
		{
		case BodyShape::Sphere:
		{
			const Fixed reach = a.major + b.major;
			return Engine::Math::DistanceSquared(a.position, b.position) <= reach * reach;
		}
		case BodyShape::Cylinder: return SphereNonsphere(CircleCircle, a, b);
		case BodyShape::Box: return SphereNonsphere(CircleRect, a, b);
		}
		break;
	case BodyShape::Cylinder:
		switch (b.shape)
		{
		case BodyShape::Sphere: return SphereNonsphere(CircleCircle, b, a);
		// collideTest_Cylinder_Cylinder (cylinderContact): the heights overlap (above) and the circles touch.
		case BodyShape::Cylinder: return CircleCircle(a, b);
		case BodyShape::Box: return NonsphereNonsphere(CircleRect, a, b);
		}
		break;
	case BodyShape::Box:
		switch (b.shape)
		{
		case BodyShape::Sphere: return SphereNonsphere(CircleRect, b, a);
		case BodyShape::Cylinder: return NonsphereNonsphere(RectCircle, a, b);
		case BodyShape::Box: return NonsphereNonsphere(RectRect, a, b);
		}
		break;
	}
	return false;
}
}
