module;

#include <cmath>
#include <optional>

export module Engine.Core.Math.Plane3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
enum class SegmentPlaneHit
{
	Parallel,
	Within_Segment,
	Outside_Segment,
};

// A normalized plane with the equation dot(normal, point) = distance.
struct Plane3 final
{
	Vector3 normal{0.0f, 0.0f, 1.0f};
	float distance = 0.0f;

	static std::optional<Plane3> From_Normal_Distance(Vector3 plane_normal, float plane_distance) noexcept
	{
		const float length = plane_normal.Length();
		if (!(length > 0.0f) || !std::isfinite(length) || !std::isfinite(plane_distance)) return std::nullopt;
		return Plane3{plane_normal / length, plane_distance / length};
	}

	static std::optional<Plane3> From_Points(Vector3 first, Vector3 second, Vector3 third) noexcept
	{
		const Vector3 cross = (second - first).Cross(third - first);
		const float length = cross.Length();
		if (!(length > 0.0f) || !std::isfinite(length)) return std::nullopt;
		const Vector3 unit_normal = cross / length;
		const float plane_distance = unit_normal.Dot(first);
		if (!std::isfinite(plane_distance)) return std::nullopt;
		return Plane3{unit_normal, plane_distance};
	}

	constexpr float Signed_Distance(Vector3 point) const noexcept
	{
		return normal.Dot(point) - distance;
	}

	// Returns the line parameter t for point = start + t * (end - start).
	// Values outside [0, 1] are valid intersections with the infinite line.
	std::optional<float> Intersect_Line(Vector3 start, Vector3 end) const noexcept
	{
		const float denominator = normal.Dot(end - start);
		if (denominator == 0.0f || !std::isfinite(denominator)) return std::nullopt;
		const float parameter = (distance - normal.Dot(start)) / denominator;
		if (!std::isfinite(parameter)) return std::nullopt;
		return parameter;
	}

	std::optional<float> Intersect_Segment(Vector3 start, Vector3 end) const noexcept
	{
		const auto parameter = Intersect_Line(start, end);
		if (!parameter || *parameter < 0.0f || *parameter > 1.0f) return std::nullopt;
		return parameter;
	}

	static constexpr SegmentPlaneHit Classify_Segment_Intersection(std::optional<float> parameter) noexcept
	{
		if (!parameter) return SegmentPlaneHit::Parallel;
		return *parameter >= 0.0f && *parameter <= 1.0f
			? SegmentPlaneHit::Within_Segment
			: SegmentPlaneHit::Outside_Segment;
	}
};
}
