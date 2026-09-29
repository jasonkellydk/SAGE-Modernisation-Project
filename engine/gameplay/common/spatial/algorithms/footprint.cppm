export module engine.gameplay.common.spatial.algorithms.footprint;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// The ground an object stands on, from above (the original's GeometryInfo in
// 2D): a box `major` along its facing by `minor` across (both half extents),
// or a circle of radius `major`. Where two such footprints overlap
// (PartitionManager::geomCollidesWithGeom, 2D), and the points of one sampled
// on a grid (BuildAssistant::iterateFootprint).
export namespace engine::gameplay
{
enum class FootprintShape : std::uint8_t
{
	Box,
	Circle,
};

struct Footprint
{
	FootprintShape shape{FootprintShape::Circle};
	Engine::Math::Fixed major;
	Engine::Math::Fixed minor;
};

namespace footprint_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline FixedVector2 Turned(FixedVector2 local, Engine::Math::TurnAngle facing) noexcept
{
	const Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	return {local.x * c - local.y * s, local.x * s + local.y * c};
}

inline Fixed Along(FixedVector2 a, FixedVector2 b) noexcept { return a.x * b.x + a.y * b.y; }

// How far a box reaches along a unit axis from its centre.
inline Fixed Reach(const Footprint &box, Engine::Math::TurnAngle facing, FixedVector2 axis) noexcept
{
	const FixedVector2 along = Turned({Fixed::One(), Fixed{}}, facing), across = Turned({Fixed{}, Fixed::One()}, facing);
	return box.major * Engine::Math::Abs(Along(along, axis)) + box.minor * Engine::Math::Abs(Along(across, axis));
}

inline bool BoxCircle(const Footprint &box, FixedVector2 at, Engine::Math::TurnAngle facing, Fixed radius, FixedVector2 centre) noexcept
{
	// The circle's centre in the box's frame, the nearest point of the box to it.
	const FixedVector2 local = Turned(centre - at, Engine::Math::TurnAngle{0u - facing.units});
	const FixedVector2 nearest{std::clamp(local.x, Fixed{} - box.major, box.major), std::clamp(local.y, Fixed{} - box.minor, box.minor)};
	return Engine::Math::DistanceSquared(local, nearest) < radius * radius;
}
}

// Whether two footprints overlap (touching edges do not).
inline bool FootprintsOverlap(const Footprint &a, Engine::Math::FixedVector2 atA, Engine::Math::TurnAngle facingA, const Footprint &b,
	Engine::Math::FixedVector2 atB, Engine::Math::TurnAngle facingB) noexcept
{
	using namespace footprint_detail;
	if (a.shape == FootprintShape::Circle && b.shape == FootprintShape::Circle)
	{
		const Fixed reach = a.major + b.major;
		return Engine::Math::DistanceSquared(atA, atB) < reach * reach;
	}
	if (a.shape == FootprintShape::Circle)
		return BoxCircle(b, atB, facingB, a.major, atA);
	if (b.shape == FootprintShape::Circle)
		return BoxCircle(a, atA, facingA, b.major, atB);
	// Two boxes: separated along one of their four axes, or overlapping.
	const FixedVector2 between = atB - atA;
	for (const Engine::Math::TurnAngle facing : {facingA, facingB})
		for (const FixedVector2 local : {FixedVector2{Fixed::One(), Fixed{}}, FixedVector2{Fixed{}, Fixed::One()}})
		{
			const FixedVector2 axis = Turned(local, facing);
			if (Engine::Math::Abs(Along(between, axis)) >= Reach(a, facingA, axis) + Reach(b, facingB, axis))
				return false;
		}
	return true;
}

// BuildAssistant::iterateFootprint: `visit(point)` for the grid of points `step` apart over the footprint in its own
// frame (a box from -major to major along and -minor to minor across, each side's last point on its edge; a circle
// over its bounding square, only the points within it), turned by `facing` and moved to `at`.
template<typename Visit>
void ForEachFootprintSample(const Footprint &footprint, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing, Engine::Math::Fixed step, Visit &&visit)
{
	using Engine::Math::Fixed;
	if (step <= Fixed{})
		return;
	const bool circle = footprint.shape == FootprintShape::Circle;
	const Fixed halfWidth = footprint.major, halfHeight = circle ? footprint.major : footprint.minor;
	for (Fixed y = Fixed{} - halfHeight; y < halfHeight + step; y += step)
	{
		if (y > halfHeight)
			y = halfHeight;
		for (Fixed x = Fixed{} - halfWidth; x < halfWidth + step; x += step)
		{
			if (x > halfWidth)
				x = halfWidth;
			const Engine::Math::FixedVector2 point = at + footprint_detail::Turned({x, y}, facing);
			if (circle && Engine::Math::DistanceSquared(point, at) > halfWidth * halfWidth)
				continue;
			visit(point);
		}
	}
}
}
