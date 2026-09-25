module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

export module Engine.Core.Math.LineSegment3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
struct LineSegment3 final
{
	Vector3 start{};
	Vector3 end{};

	friend constexpr bool operator==(const LineSegment3 &, const LineSegment3 &) noexcept = default;

	constexpr Vector3 Displacement() const noexcept { return end - start; }
	constexpr Vector3 Point_At(float fraction) const noexcept { return start + Displacement() * fraction; }
	float Length() const noexcept
	{
		const Vector3 displacement = Displacement();
		return static_cast<float>(std::hypot(
			static_cast<double>(displacement.x),
			static_cast<double>(displacement.y),
			static_cast<double>(displacement.z)));
	}
	Vector3 Direction() const noexcept { return Displacement().Normalized(); }
	Vector3 Closest_Point(Vector3 point) const noexcept
	{
		const Vector3 displacement = Displacement();
		const double length_squared = static_cast<double>(displacement.x) * displacement.x
			+ static_cast<double>(displacement.y) * displacement.y
			+ static_cast<double>(displacement.z) * displacement.z;
		if (!(length_squared > 0.0) || !std::isfinite(length_squared)) return start;
		const Vector3 offset = point - start;
		const double projection = static_cast<double>(offset.x) * displacement.x
			+ static_cast<double>(offset.y) * displacement.y
			+ static_cast<double>(offset.z) * displacement.z;
		const float fraction = static_cast<float>((std::max)(0.0, (std::min)(1.0, projection / length_squared)));
		return Point_At(fraction);
	}
};

inline std::optional<Vector3> Try_Point_At_Z(const LineSegment3 &segment, float height) noexcept
{
	if (!std::isfinite(height) || !std::isfinite(segment.start.x) || !std::isfinite(segment.start.y)
		|| !std::isfinite(segment.start.z) || !std::isfinite(segment.end.x)
		|| !std::isfinite(segment.end.y) || !std::isfinite(segment.end.z))
		return std::nullopt;
	const double height_delta = static_cast<double>(segment.end.z) - segment.start.z;
	if (height_delta == 0.0) return std::nullopt;
	const double fraction = (static_cast<double>(height) - segment.start.z) / height_delta;
	const double x = static_cast<double>(segment.start.x)
		+ (static_cast<double>(segment.end.x) - segment.start.x) * fraction;
	const double y = static_cast<double>(segment.start.y)
		+ (static_cast<double>(segment.end.y) - segment.start.y) * fraction;
	const double z = static_cast<double>(segment.start.z) + height_delta * fraction;
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
		|| std::abs(x) > (std::numeric_limits<float>::max)()
		|| std::abs(y) > (std::numeric_limits<float>::max)()
		|| std::abs(z) > (std::numeric_limits<float>::max)())
		return std::nullopt;
	return Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}
}
