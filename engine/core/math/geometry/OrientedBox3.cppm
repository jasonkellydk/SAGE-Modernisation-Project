module;

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

export module Engine.Core.Math.OrientedBox3;

export import Engine.Core.Math.AxisAlignedBox3;

export namespace Engine::Math
{
struct BoxSegmentHit3 final
{
	float fraction = 0.0f;
	Vector3 point{};
	Vector3 normal{};
	// The segment starts inside (or on) the box: fraction is 0, point is the
	// segment start, and normal is zero.
	bool starts_inside = false;
};

struct SweptBoxHit3 final
{
	float fraction = 0.0f;
	Vector3 point{};
	Vector3 normal{};
	bool starts_overlapping = false;
};

// The three axes must be unit length and mutually perpendicular. Extents are
// non-negative half lengths along those axes.
struct OrientedBox3 final
{
	Vector3 center{};
	Vector3 half_extent{};
	std::array<Vector3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

	bool Is_Valid() const noexcept
	{
		const auto finite = [](Vector3 value) {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		if (!finite(center) || !finite(half_extent)
			|| half_extent.x < 0.0f || half_extent.y < 0.0f || half_extent.z < 0.0f)
			return false;
		for (const Vector3 axis : axes)
			if (!finite(axis) || std::abs(axis.Dot(axis) - 1.0f) > 1.0e-4f)
				return false;
		return std::abs(axes[0].Dot(axes[1])) <= 1.0e-4f
			&& std::abs(axes[0].Dot(axes[2])) <= 1.0e-4f
			&& std::abs(axes[1].Dot(axes[2])) <= 1.0e-4f;
	}

	bool Contains(Vector3 point) const noexcept
	{
		if (!Is_Valid() || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
			return false;
		const Vector3 offset = point - center;
		return std::abs(offset.Dot(axes[0])) <= half_extent.x
			&& std::abs(offset.Dot(axes[1])) <= half_extent.y
			&& std::abs(offset.Dot(axes[2])) <= half_extent.z;
	}

	AxisAlignedBox3 To_Axis_Aligned() const noexcept
	{
		// Works for any finite basis (scaled or skewed axes included), matching
		// the legacy OBBox Compute_Axis_Aligned_Extent term order.
		const auto finite = [](Vector3 value) {
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		if (!finite(center) || !finite(half_extent) || !finite(axes[0]) || !finite(axes[1]) || !finite(axes[2]))
			return {{1, 1, 1}, {-1, -1, -1}};
		const Vector3 extent{
			std::abs(half_extent.x * axes[0].x) + std::abs(half_extent.y * axes[1].x) + std::abs(half_extent.z * axes[2].x),
			std::abs(half_extent.x * axes[0].y) + std::abs(half_extent.y * axes[1].y) + std::abs(half_extent.z * axes[2].y),
			std::abs(half_extent.x * axes[0].z) + std::abs(half_extent.y * axes[1].z) + std::abs(half_extent.z * axes[2].z)};
		return {center - extent, center + extent};
	}

	bool Intersects(const OrientedBox3 &other) const noexcept
	{
		if (!Is_Valid() || !other.Is_Valid()) return false;
		// Test the three axes from each box and the nine pairwise cross products.
		// Fixed traversal and binary64 radii arithmetic reduce avoidable drift.
		const double rotation[3][3] = {
			{axes[0].Dot(other.axes[0]), axes[0].Dot(other.axes[1]), axes[0].Dot(other.axes[2])},
			{axes[1].Dot(other.axes[0]), axes[1].Dot(other.axes[1]), axes[1].Dot(other.axes[2])},
			{axes[2].Dot(other.axes[0]), axes[2].Dot(other.axes[1]), axes[2].Dot(other.axes[2])}};
		double absolute[3][3];
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				absolute[i][j] = std::abs(rotation[i][j]);

		const Vector3 delta = other.center - center;
		const double translated[3] = {delta.Dot(axes[0]), delta.Dot(axes[1]), delta.Dot(axes[2])};
		const double a[3] = {half_extent.x, half_extent.y, half_extent.z};
		const double b[3] = {other.half_extent.x, other.half_extent.y, other.half_extent.z};
		for (int i = 0; i < 3; ++i) {
			const double radius_b = b[0] * absolute[i][0] + b[1] * absolute[i][1] + b[2] * absolute[i][2];
			if (std::abs(translated[i]) > a[i] + radius_b) return false;
		}
		for (int j = 0; j < 3; ++j) {
			const double distance = std::abs(translated[0] * rotation[0][j]
				+ translated[1] * rotation[1][j] + translated[2] * rotation[2][j]);
			const double radius_a = a[0] * absolute[0][j] + a[1] * absolute[1][j] + a[2] * absolute[2][j];
			if (distance > radius_a + b[j]) return false;
		}
		for (int i = 0; i < 3; ++i) {
			const int i1 = (i + 1) % 3;
			const int i2 = (i + 2) % 3;
			for (int j = 0; j < 3; ++j) {
				const int j1 = (j + 1) % 3;
				const int j2 = (j + 2) % 3;
				const double distance = std::abs(translated[i2] * rotation[i1][j]
					- translated[i1] * rotation[i2][j]);
				const double radius_a = a[i1] * absolute[i2][j] + a[i2] * absolute[i1][j];
				const double radius_b = b[j1] * absolute[i][j2] + b[j2] * absolute[i][j1];
				if (distance > radius_a + radius_b) return false;
			}
		}
		return true;
	}

	std::optional<BoxSegmentHit3> Intersect_Segment(Vector3 start, Vector3 end) const noexcept
	{
		if (!Is_Valid() || !std::isfinite(start.x) || !std::isfinite(start.y)
			|| !std::isfinite(start.z) || !std::isfinite(end.x) || !std::isfinite(end.y)
			|| !std::isfinite(end.z)) return std::nullopt;
		const Vector3 offset = start - center;
		const Vector3 direction = end - start;
		bool starts_inside = true;
		for (unsigned axis = 0; axis < 3; ++axis) {
			const float origin = offset.Dot(axes[axis]);
			const float extent = Component(half_extent, axis);
			if (origin < -extent || origin > extent) {
				starts_inside = false;
				break;
			}
		}
		if (starts_inside) return BoxSegmentHit3{0.0f, start, {}, true};
		double entry = 0.0;
		double exit = 1.0;
		Vector3 entry_normal{};
		for (unsigned axis = 0; axis < 3; ++axis) {
			const double origin = offset.Dot(axes[axis]);
			const double speed = direction.Dot(axes[axis]);
			const double extent = Component(half_extent, axis);
			if (speed == 0.0) {
				if (origin < -extent || origin > extent) return std::nullopt;
				continue;
			}
			double near_time = (-extent - origin) / speed;
			double far_time = (extent - origin) / speed;
			Vector3 normal{-axes[axis].x, -axes[axis].y, -axes[axis].z};
			if (near_time > far_time) {
				std::swap(near_time, far_time);
				normal = axes[axis];
			}
			if (near_time > entry) {
				entry = near_time;
				entry_normal = normal;
			}
			if (far_time < exit) exit = far_time;
			if (entry > exit) return std::nullopt;
		}
		if (exit < 0.0 || entry > 1.0) return std::nullopt;
		const float fraction = static_cast<float>(entry);
		return BoxSegmentHit3{fraction, start + direction * fraction, entry_normal};
	}

	// Legacy bit-compatible port of CollisionMath::Collide(LineSeg, OBBox):
	// the segment is moved into box space with the transposed basis and run
	// through the legacy aligned-box test. A start inside the box sets
	// result.starts_overlapping (legacy StartBad), stores the start as the
	// contact point when requested, leaves the fraction unchanged and returns
	// true. Otherwise a hit closer than result.fraction replaces fraction,
	// normal (the hit axis, negated for the negative face) and contact point.
	// Performs no validity checks, like the legacy code.
	bool Collide_Segment_Legacy(Vector3 start, Vector3 end, CollisionResult3 &result) const noexcept
	{
		const Vector3 delta = end - start;
		const Vector3 relative = start - center;
		const Vector3 local_start = Vector3{relative.Dot(axes[0]), relative.Dot(axes[1]), relative.Dot(axes[2])} + center;
		const Vector3 local_delta{delta.Dot(axes[0]), delta.Dot(axes[1]), delta.Dot(axes[2])};
		LegacyBoxSegmentTest3 test;
		if (!Test_Aligned_Box_Legacy(center - half_extent, center + half_extent, local_start, local_delta, test))
			return false;
		if (test.inside) {
			result.starts_overlapping = true;
			if (result.compute_contact_point) result.contact_point = start;
			return true;
		}
		if (test.fraction < result.fraction) {
			result.fraction = test.fraction;
			result.normal = axes[test.axis];
			if (!test.positive_side) result.normal = -result.normal;
			if (result.compute_contact_point) result.contact_point = start + result.fraction * delta;
			return true;
		}
		return false;
	}

	std::optional<SweptBoxHit3> Sweep(const OrientedBox3 &target, Vector3 movement) const noexcept
	{
		if (!Is_Valid() || !target.Is_Valid()
			|| !std::isfinite(movement.x) || !std::isfinite(movement.y) || !std::isfinite(movement.z))
			return std::nullopt;
		std::array<Vector3, 15> axes_to_test{};
		for (unsigned axis = 0; axis < 3; ++axis) axes_to_test[axis] = axes[axis];
		for (unsigned axis = 0; axis < 3; ++axis) axes_to_test[3 + axis] = target.axes[axis];
		unsigned next = 6;
		for (const auto first_axis : axes)
			for (const auto second_axis : target.axes)
				axes_to_test[next++] = first_axis.Cross(second_axis);

		const Vector3 initial_offset = center - target.center;
		double entry = -std::numeric_limits<double>::infinity();
		double exit = std::numeric_limits<double>::infinity();
		Vector3 entry_normal{};
		bool starts_overlapping = true;
		for (const auto axis : axes_to_test) {
			const double axis_length_squared = axis.Dot(axis);
			if (axis_length_squared <= 1.0e-12) continue;
			const double distance = initial_offset.Dot(axis);
			const double moving_radius = half_extent.x * std::abs(axes[0].Dot(axis))
				+ half_extent.y * std::abs(axes[1].Dot(axis))
				+ half_extent.z * std::abs(axes[2].Dot(axis));
			const double target_radius = target.half_extent.x * std::abs(target.axes[0].Dot(axis))
				+ target.half_extent.y * std::abs(target.axes[1].Dot(axis))
				+ target.half_extent.z * std::abs(target.axes[2].Dot(axis));
			const double radius = moving_radius + target_radius;
			starts_overlapping = starts_overlapping && std::abs(distance) <= radius;
			const double speed = movement.Dot(axis);
			if (speed == 0.0) {
				if (std::abs(distance) > radius) return std::nullopt;
				continue;
			}
			double near_time = (-radius - distance) / speed;
			double far_time = (radius - distance) / speed;
			Vector3 normal{-axis.x, -axis.y, -axis.z};
			if (near_time > far_time) {
				std::swap(near_time, far_time);
				normal = axis;
			}
			if (near_time > entry) {
				entry = near_time;
				entry_normal = normal;
			}
			if (far_time < exit) exit = far_time;
			if (entry > exit) return std::nullopt;
		}
		if (starts_overlapping)
			return SweptBoxHit3{0.0f, Closest_Point(target, center),
				(center - target.center).Normalized(), true};
		if (exit < 0.0 || entry < 0.0 || entry > 1.0) return std::nullopt;
		const Vector3 impact_center = center + movement * static_cast<float>(entry);
		return SweptBoxHit3{static_cast<float>(entry), Closest_Point(target, impact_center),
			entry_normal.Normalized(), false};
	}

private:
	static Vector3 Closest_Point(const OrientedBox3 &box, Vector3 point) noexcept
	{
		const Vector3 offset = point - box.center;
		Vector3 result = box.center;
		for (unsigned axis = 0; axis < 3; ++axis) {
			const float extent = Component(box.half_extent, axis);
			const float coordinate = std::clamp(offset.Dot(box.axes[axis]), -extent, extent);
			result = result + box.axes[axis] * coordinate;
		}
		return result;
	}

	static float Component(Vector3 value, unsigned axis) noexcept
	{
		return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
	}
};
}
