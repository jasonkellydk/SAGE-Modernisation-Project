module;

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

export module Engine.Core.Math.Sphere3;

export import Engine.Core.Math.Vector3;

export namespace Engine::Math
{
struct Sphere3 final
{
	Vector3 center{};
	float radius = 0.0f;

	friend constexpr bool operator==(const Sphere3 &, const Sphere3 &) noexcept = default;

	Sphere3 Translated(Vector3 displacement) const noexcept
	{
		return {center + displacement, radius};
	}

	Sphere3 Scaled(float factor) const noexcept
	{
		if (!std::isfinite(factor)) {
			const float invalid = std::numeric_limits<float>::quiet_NaN();
			return {{invalid, invalid, invalid}, invalid};
		}
		return {center * factor, radius * std::abs(factor)};
	}

	void Include(const Sphere3 &other) noexcept
	{
		if (!other.Is_Valid()) return;
		if (!Is_Valid()) { *this = other; return; }

		const double dx = static_cast<double>(other.center.x) - center.x;
		const double dy = static_cast<double>(other.center.y) - center.y;
		const double dz = static_cast<double>(other.center.z) - center.z;
		const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		const double current_radius = radius;
		const double other_radius = other.radius;
		if (current_radius >= distance + other_radius) return;
		if (other_radius >= distance + current_radius) { *this = other; return; }

		const double enclosing_radius = (distance + current_radius + other_radius) * 0.5;
		const double center_fraction = distance == 0.0 ? 0.0
			: (enclosing_radius - current_radius) / distance;
		center = {
			static_cast<float>(static_cast<double>(center.x) + dx * center_fraction),
			static_cast<float>(static_cast<double>(center.y) + dy * center_fraction),
			static_cast<float>(static_cast<double>(center.z) + dz * center_fraction)};
		radius = static_cast<float>(enclosing_radius);
	}

	bool Is_Valid() const noexcept
	{
		return std::isfinite(center.x) && std::isfinite(center.y) && std::isfinite(center.z)
			&& std::isfinite(radius) && radius >= 0.0f;
	}

	bool Contains(Vector3 point) const noexcept
	{
		if (!Is_Valid() || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
			return false;
		const double dx = static_cast<double>(point.x) - center.x;
		const double dy = static_cast<double>(point.y) - center.y;
		const double dz = static_cast<double>(point.z) - center.z;
		const double expanded_radius = radius;
		return dx * dx + dy * dy + dz * dz <= expanded_radius * expanded_radius;
	}

	bool Intersects(const Sphere3 &other) const noexcept
	{
		if (!Is_Valid() || !other.Is_Valid()) return false;
		const double dx = static_cast<double>(other.center.x) - center.x;
		const double dy = static_cast<double>(other.center.y) - center.y;
		const double dz = static_cast<double>(other.center.z) - center.z;
		const double combined_radius = static_cast<double>(radius) + other.radius;
		return dx * dx + dy * dy + dz * dz <= combined_radius * combined_radius;
	}
};

inline std::optional<Sphere3> Try_Enclosing_Sphere(std::span<const Vector3> points) noexcept
{
	if (points.empty()) return std::nullopt;
	for (const Vector3 point : points)
		if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
			return std::nullopt;
	double minimum[3] = {points.front().x, points.front().y, points.front().z};
	double maximum[3] = {points.front().x, points.front().y, points.front().z};
	for (const Vector3 point : points) {
		const double components[3] = {point.x, point.y, point.z};
		for (std::size_t axis = 0; axis < 3; ++axis) {
			if (components[axis] < minimum[axis]) minimum[axis] = components[axis];
			if (components[axis] > maximum[axis]) maximum[axis] = components[axis];
		}
	}
	const double center[3] = {
		minimum[0] + (maximum[0] - minimum[0]) * 0.5,
		minimum[1] + (maximum[1] - minimum[1]) * 0.5,
		minimum[2] + (maximum[2] - minimum[2]) * 0.5};
	double radius_squared = 0.0;
	for (const Vector3 point : points) {
		const double dx = static_cast<double>(point.x) - center[0];
		const double dy = static_cast<double>(point.y) - center[1];
		const double dz = static_cast<double>(point.z) - center[2];
		const double distance_squared = dx * dx + dy * dy + dz * dz;
		if (distance_squared > radius_squared) radius_squared = distance_squared;
	}
	const Sphere3 result{{static_cast<float>(center[0]), static_cast<float>(center[1]), static_cast<float>(center[2])},
		static_cast<float>(std::sqrt(radius_squared))};
	return result.Is_Valid() ? std::optional<Sphere3>(result) : std::nullopt;
}
}
