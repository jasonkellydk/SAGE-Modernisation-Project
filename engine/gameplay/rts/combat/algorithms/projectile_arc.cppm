export module engine.gameplay.rts.combat.algorithms.projectile_arc;
import std;

export import Engine.Core.Math.FixedVector;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.weapons.definitions.weapon;

// A lobbed projectile's flight (the original's DumbProjectileBehavior): a
// cubic Bezier from the launch point to the aim point whose inner points sit
// `indent` of the way along the line, `height` above the highest terrain in
// between (or either end, if higher), walked one point a tick. The number of
// points is the curve's approximate length over the weapon's speed, so the
// flight takes as long as its path. Fixed point throughout.
export namespace engine::gameplay
{
using ArcPoints = std::array<Engine::Math::FixedVector3, 4>;

namespace projectile_arc_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;

inline FixedVector3 Lerp(const FixedVector3 &a, const FixedVector3 &b, Fixed t) noexcept { return a + (b - a) * t; }

// BezierSegment::getApproximateLength: half the chord plus half the control polygon, split in two
// while they differ by more than the tolerance.
inline Fixed Length(const ArcPoints &points, Fixed tolerance, int depth) noexcept
{
	const Fixed chord = Engine::Math::Length(points[3] - points[0]);
	const Fixed polygon = Engine::Math::Length(points[1] - points[0]) + Engine::Math::Length(points[2] - points[1]) +
		Engine::Math::Length(points[3] - points[2]);
	if (polygon - chord > tolerance && depth < 16)
	{
		// splitSegmentAtT(0.5): de Casteljau.
		const Fixed half = Fixed::FromRatio(1, 2);
		const FixedVector3 a = Lerp(points[0], points[1], half), b = Lerp(points[1], points[2], half), c = Lerp(points[2], points[3], half);
		const FixedVector3 d = Lerp(a, b, half), e = Lerp(b, c, half), mid = Lerp(d, e, half);
		return Length({points[0], a, d, mid}, tolerance, depth + 1) + Length({mid, e, c, points[3]}, tolerance, depth + 1);
	}
	return (chord + polygon) / Fixed::FromInt(2);
}
}

// PartitionManager::estimateTerrainExtremesAlongLine: the highest ground in the partition cells
// (`cellSize` square, each sampled every `step` corner to corner, calcHeights) the line crosses
// (iterateCellsAlongLine's Bresenham walk). Water does not count.
inline Engine::Math::Fixed HighestTerrainAlong(const GroundHeight &ground, Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 to,
	Engine::Math::Fixed cellSize = Engine::Math::Fixed::FromInt(40), Engine::Math::Fixed step = Engine::Math::Fixed::FromInt(10))
{
	using Engine::Math::Fixed;
	const auto cell = [&](Fixed value) { return static_cast<std::int64_t>((value / cellSize).Floor()); };
	std::int64_t x = cell(from.x), y = cell(from.y);
	const std::int64_t endX = cell(to.x), endY = cell(to.y);
	const std::int64_t dx = std::llabs(endX - x), dy = std::llabs(endY - y);
	const std::int64_t sx = endX >= x ? 1 : -1, sy = endY >= y ? 1 : -1;
	const bool alongX = dx >= dy;
	const std::int64_t den = alongX ? dx : dy, add = alongX ? dy : dx, pixels = den;
	std::int64_t num = den / 2;
	const std::int64_t steps = std::max<std::int64_t>(1, (cellSize / step).Ceil());
	const Fixed stride = cellSize / Fixed::FromInt(steps);
	Fixed highest = Fixed::FromInt(-1000000);
	for (std::int64_t pixel = 0; pixel <= pixels; ++pixel)
	{
		const Fixed baseX = Fixed::FromInt(x) * cellSize, baseY = Fixed::FromInt(y) * cellSize;
		for (std::int64_t j = 0; j <= steps; ++j)
			for (std::int64_t i = 0; i <= steps; ++i)
				highest = std::max(highest, ground.At({baseX + stride * Fixed::FromInt(i), baseY + stride * Fixed::FromInt(j)}));
		num += add;
		if (num >= den)
		{
			num -= den;
			if (alongX)
				y += sy;
			else
				x += sx;
		}
		if (alongX)
			x += sx;
		else
			y += sy;
	}
	return highest;
}

// DumbProjectileBehavior::calcFlightPath's control points.
inline ArcPoints ArcControlPoints(const Engine::Math::FixedVector3 &start, const Engine::Math::FixedVector3 &end, const ProjectileArc &arc,
	Engine::Math::Fixed highestTerrain) noexcept
{
	const Engine::Math::Fixed top = std::max({highestTerrain, start.z, end.z});
	const Engine::Math::FixedVector3 line = end - start;
	ArcPoints points{start, start, start, end};
	points[1].x = start.x + line.x * arc.firstIndent;
	points[1].y = start.y + line.y * arc.firstIndent;
	points[1].z = top + arc.firstHeight;
	points[2].x = start.x + line.x * arc.secondIndent;
	points[2].y = start.y + line.y * arc.secondIndent;
	points[2].z = top + arc.secondHeight;
	return points;
}

// BezierSegment::getApproximateLength (USUAL_TOLERANCE 1).
inline Engine::Math::Fixed ArcLength(const ArcPoints &points, Engine::Math::Fixed tolerance = Engine::Math::Fixed::One()) noexcept
{
	return projectile_arc_detail::Length(points, tolerance, 0);
}

// The points walked: ceil(length / speed) (a tick's travel), none when it cannot move.
inline std::uint32_t ArcSegments(const ArcPoints &points, Engine::Math::Fixed speed) noexcept
{
	if (speed <= Engine::Math::Fixed{})
		return 0;
	return static_cast<std::uint32_t>(std::clamp<std::int64_t>((ArcLength(points) / speed).Ceil(), 0, 100000));
}

// The `step`th of `segments` points (BezierSegment::getSegmentPoints: t = step / (segments - 1)).
inline Engine::Math::FixedVector3 ArcPoint(const ArcPoints &points, std::uint32_t segments, std::uint32_t step) noexcept
{
	using Engine::Math::Fixed;
	if (segments <= 1)
		return points[3];
	const Fixed t = Fixed::FromInt(std::min(step, segments - 1)) / Fixed::FromInt(segments - 1);
	const Fixed u = Fixed::One() - t;
	const Fixed w0 = u * u * u, w1 = Fixed::FromInt(3) * u * u * t, w2 = Fixed::FromInt(3) * u * t * t, w3 = t * t * t;
	return points[0] * w0 + points[1] * w1 + points[2] * w2 + points[3] * w3;
}
}
