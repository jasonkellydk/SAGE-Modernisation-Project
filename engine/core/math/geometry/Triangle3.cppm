module;

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

export module Engine.Core.Math.Triangle3;

export import Engine.Core.Math.Vector3;
export import Engine.Core.Math.Vector4;
export import Engine.Core.Math.AxisAlignedBox3;
export import Engine.Core.Math.OrientedBox3;

export namespace Engine::Math
{
struct SegmentTriangleHit3 final
{
	float fraction = 0.0f;
	Vector3 point{};
	Vector3 normal{};
};

struct SweptTriangleHit3 final
{
	float fraction = 0.0f;
	Vector3 normal{};
	Vector3 point{};
	bool starts_overlapping = false;
};

struct Triangle3 final
{
	Vector3 first{};
	Vector3 second{};
	Vector3 third{};

	struct AxisRayHit final
	{
		bool intersects = false;
		bool touches_edge = false;
		bool starts_inside = false;
	};
	enum class AxisRayFlag : std::uint8_t { TouchesEdge = 1, StartsInside = 2 };

	Vector3 Normal() const noexcept
	{
		return (second - first).Cross(third - first).Normalized();
	}

	bool Intersects(const AxisAlignedBox3 &box) const noexcept
	{
		if (!Is_Finite(first) || !Is_Finite(second) || !Is_Finite(third)
			|| !std::isfinite(box.minimum.x) || !std::isfinite(box.minimum.y)
			|| !std::isfinite(box.minimum.z) || !std::isfinite(box.maximum.x)
			|| !std::isfinite(box.maximum.y) || !std::isfinite(box.maximum.z)
			|| box.minimum.x > box.maximum.x || box.minimum.y > box.maximum.y
			|| box.minimum.z > box.maximum.z)
			return false;
		const std::array<double, 3> center{
			(static_cast<double>(box.minimum.x) + box.maximum.x) * 0.5,
			(static_cast<double>(box.minimum.y) + box.maximum.y) * 0.5,
			(static_cast<double>(box.minimum.z) + box.maximum.z) * 0.5};
		const std::array<double, 3> extent{
			(static_cast<double>(box.maximum.x) - box.minimum.x) * 0.5,
			(static_cast<double>(box.maximum.y) - box.minimum.y) * 0.5,
			(static_cast<double>(box.maximum.z) - box.minimum.z) * 0.5};
		return Intersects_Centered_Box({To_Double(first, center), To_Double(second, center),
			To_Double(third, center)}, extent);
	}

	bool Intersects(const OrientedBox3 &box) const noexcept
	{
		if (!box.Is_Valid() || !Is_Finite(first) || !Is_Finite(second) || !Is_Finite(third))
			return false;
		const std::array<double, 3> extent{
			box.half_extent.x, box.half_extent.y, box.half_extent.z};
		return Intersects_Centered_Box({To_Local(first, box), To_Local(second, box),
			To_Local(third, box)}, extent);
	}

	std::optional<SweptTriangleHit3> Sweep(const OrientedBox3 &box,
		Vector3 movement) const noexcept
	{
		if (!box.Is_Valid() || !Is_Finite(first) || !Is_Finite(second)
			|| !Is_Finite(third) || !Is_Finite(movement))
			return std::nullopt;
		const std::array<DoubleVector3, 3> vertices{
			To_Local(first, box), To_Local(second, box), To_Local(third, box)};
		DoubleVector3 local_movement{};
		for (unsigned axis = 0; axis < 3; ++axis) {
			local_movement[axis] = static_cast<double>(movement.x) * box.axes[axis].x
				+ static_cast<double>(movement.y) * box.axes[axis].y
				+ static_cast<double>(movement.z) * box.axes[axis].z;
		}
		const DoubleVector3 extent{box.half_extent.x, box.half_extent.y, box.half_extent.z};
		const auto axes = Separating_Axes(vertices);
		bool starts_overlapping = true;
		double entry = -std::numeric_limits<double>::infinity();
		double exit = std::numeric_limits<double>::infinity();
		DoubleVector3 entry_axis{};
		for (const auto &axis : axes) {
			if (Is_Zero(axis)) continue;
			const auto [minimum, maximum] = Project(vertices, axis);
			const double radius = extent[0] * std::abs(axis[0])
				+ extent[1] * std::abs(axis[1]) + extent[2] * std::abs(axis[2]);
			const bool overlaps = minimum <= radius && maximum >= -radius;
			starts_overlapping = starts_overlapping && overlaps;
			const double speed = Dot(local_movement, axis);
			if (speed == 0.0) {
				if (!overlaps) return std::nullopt;
				continue;
			}
			double axis_entry;
			double axis_exit;
			DoubleVector3 normal{};
			if (speed > 0.0) {
				axis_entry = (minimum - radius) / speed;
				axis_exit = (maximum + radius) / speed;
				normal = {-axis[0], -axis[1], -axis[2]};
			} else {
				axis_entry = (maximum + radius) / speed;
				axis_exit = (minimum - radius) / speed;
				normal = axis;
			}
			if (axis_entry > entry) {
				entry = axis_entry;
				entry_axis = normal;
			}
			if (axis_exit < exit) exit = axis_exit;
			if (entry > exit) return std::nullopt;
		}

		if (starts_overlapping) {
			return SweptTriangleHit3{0.0f, Normal(), Closest_Point(box.center), true};
		}
		if (exit < 0.0 || entry < 0.0 || entry > 1.0) return std::nullopt;
		DoubleVector3 world_normal{};
		for (unsigned axis = 0; axis < 3; ++axis) {
			world_normal[0] += entry_axis[axis] * box.axes[axis].x;
			world_normal[1] += entry_axis[axis] * box.axes[axis].y;
			world_normal[2] += entry_axis[axis] * box.axes[axis].z;
		}
		const double length = std::sqrt(Dot(world_normal, world_normal));
		if (length == 0.0 || !std::isfinite(length)) return std::nullopt;
		const Vector3 normal{static_cast<float>(world_normal[0] / length),
			static_cast<float>(world_normal[1] / length), static_cast<float>(world_normal[2] / length)};
		const float fraction = static_cast<float>(entry);
		const Vector3 center_at_impact{
			static_cast<float>(box.center.x + static_cast<double>(movement.x) * entry),
			static_cast<float>(box.center.y + static_cast<double>(movement.y) * entry),
			static_cast<float>(box.center.z + static_cast<double>(movement.z) * entry)};
		return SweptTriangleHit3{fraction, normal, Closest_Point(center_at_impact), false};
	}

	private:
	using DoubleVector3 = std::array<double, 3>;

	static bool Is_Finite(Vector3 point) noexcept
	{
		return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
	}

	static DoubleVector3 To_Double(Vector3 point, const DoubleVector3 &origin) noexcept
	{
		return {static_cast<double>(point.x) - origin[0],
			static_cast<double>(point.y) - origin[1], static_cast<double>(point.z) - origin[2]};
	}

	static DoubleVector3 To_Local(Vector3 point, const OrientedBox3 &box) noexcept
	{
		const DoubleVector3 offset{static_cast<double>(point.x) - box.center.x,
			static_cast<double>(point.y) - box.center.y, static_cast<double>(point.z) - box.center.z};
		DoubleVector3 local{};
		for (unsigned axis = 0; axis < 3; ++axis) {
			local[axis] = offset[0] * box.axes[axis].x
				+ offset[1] * box.axes[axis].y + offset[2] * box.axes[axis].z;
		}
		return local;
	}

	static double Dot(const DoubleVector3 &left, const DoubleVector3 &right) noexcept
	{
		return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
	}

	static bool Is_Zero(const DoubleVector3 &axis) noexcept
	{
		return axis[0] == 0.0 && axis[1] == 0.0 && axis[2] == 0.0;
	}

	static std::pair<double, double> Project(const std::array<DoubleVector3, 3> &vertices,
		const DoubleVector3 &axis) noexcept
	{
		const double first_projection = Dot(vertices[0], axis);
		double minimum = first_projection;
		double maximum = first_projection;
		for (unsigned vertex = 1; vertex < vertices.size(); ++vertex) {
			const double projection = Dot(vertices[vertex], axis);
			if (projection < minimum) minimum = projection;
			if (projection > maximum) maximum = projection;
		}
		return {minimum, maximum};
	}

	static std::array<DoubleVector3, 13> Separating_Axes(
		const std::array<DoubleVector3, 3> &vertices) noexcept
	{
		const auto subtract = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return DoubleVector3{left[0] - right[0], left[1] - right[1], left[2] - right[2]};
		};
		const auto cross = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return DoubleVector3{left[1] * right[2] - left[2] * right[1],
				left[2] * right[0] - left[0] * right[2],
				left[0] * right[1] - left[1] * right[0]};
		};
		const std::array<DoubleVector3, 3> edges{{
			subtract(vertices[1], vertices[0]), subtract(vertices[2], vertices[1]),
			subtract(vertices[0], vertices[2])}};
		const std::array<DoubleVector3, 3> box_axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
		std::array<DoubleVector3, 13> axes{};
		axes[0] = box_axes[0];
		axes[1] = box_axes[1];
		axes[2] = box_axes[2];
		axes[3] = cross(edges[0], subtract(vertices[2], vertices[0]));
		unsigned next_axis = 4;
		for (const auto &edge : edges)
			for (const auto &box_axis : box_axes)
				axes[next_axis++] = cross(edge, box_axis);
		return axes;
	}

	Vector3 Closest_Point(Vector3 point) const noexcept
	{
		const DoubleVector3 a{first.x, first.y, first.z};
		const DoubleVector3 b{second.x, second.y, second.z};
		const DoubleVector3 c{third.x, third.y, third.z};
		const DoubleVector3 p{point.x, point.y, point.z};
		const auto subtract = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return DoubleVector3{left[0] - right[0], left[1] - right[1], left[2] - right[2]};
		};
		const auto add_scaled = [](const DoubleVector3 &origin, const DoubleVector3 &direction, double scale) {
			return DoubleVector3{origin[0] + direction[0] * scale,
				origin[1] + direction[1] * scale, origin[2] + direction[2] * scale};
		};
		const DoubleVector3 ab = subtract(b, a), ac = subtract(c, a), ap = subtract(p, a);
		const DoubleVector3 normal{ab[1] * ac[2] - ab[2] * ac[1],
			ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]};
		DoubleVector3 closest{};
		if (Dot(normal, normal) == 0.0) {
			double best_distance = std::numeric_limits<double>::infinity();
			const std::array<std::pair<DoubleVector3, DoubleVector3>, 3> edges{{{a, b}, {b, c}, {c, a}}};
			for (const auto &edge : edges) {
				const DoubleVector3 direction = subtract(edge.second, edge.first);
				const double length_squared = Dot(direction, direction);
				const double fraction = length_squared == 0.0 ? 0.0
					: std::clamp(Dot(subtract(p, edge.first), direction) / length_squared, 0.0, 1.0);
				const DoubleVector3 candidate = add_scaled(edge.first, direction, fraction);
				const DoubleVector3 delta = subtract(p, candidate);
				const double distance_squared = Dot(delta, delta);
				if (distance_squared < best_distance) {
					best_distance = distance_squared;
					closest = candidate;
				}
			}
		} else {
			const DoubleVector3 bp = subtract(p, b);
			const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
			if (d1 <= 0.0 && d2 <= 0.0) closest = a;
			else {
				const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
				if (d3 >= 0.0 && d4 <= d3) closest = b;
				else {
					const double vc = d1 * d4 - d3 * d2;
					if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
						closest = add_scaled(a, ab, d1 / (d1 - d3));
					else {
						const DoubleVector3 cp = subtract(p, c);
						const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
						if (d6 >= 0.0 && d5 <= d6) closest = c;
						else {
							const double vb = d5 * d2 - d1 * d6;
							if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
								closest = add_scaled(a, ac, d2 / (d2 - d6));
							else {
								const double va = d3 * d6 - d5 * d4;
								if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
									const DoubleVector3 bc = subtract(c, b);
									const double fraction = (d4 - d3) / ((d4 - d3) + (d5 - d6));
									closest = add_scaled(b, bc, fraction);
								} else {
									const double denominator = 1.0 / (va + vb + vc);
									closest = add_scaled(add_scaled(a, ab, vb * denominator), ac, vc * denominator);
								}
							}
						}
					}
				}
			}
		}
		return {static_cast<float>(closest[0]), static_cast<float>(closest[1]), static_cast<float>(closest[2])};
	}

	static bool Intersects_Centered_Box(const std::array<DoubleVector3, 3> &vertices,
		const DoubleVector3 &extent) noexcept
	{
		const auto subtract = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return DoubleVector3{left[0] - right[0], left[1] - right[1], left[2] - right[2]};
		};
		const auto cross = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return DoubleVector3{left[1] * right[2] - left[2] * right[1],
				left[2] * right[0] - left[0] * right[2],
				left[0] * right[1] - left[1] * right[0]};
		};
		const auto dot = [](const DoubleVector3 &left, const DoubleVector3 &right) {
			return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
		};
		const std::array<DoubleVector3, 3> edges{{
			subtract(vertices[1], vertices[0]), subtract(vertices[2], vertices[1]),
			subtract(vertices[0], vertices[2])}};
		const std::array<DoubleVector3, 3> box_axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
		std::array<DoubleVector3, 13> axes{};
		axes[0] = box_axes[0];
		axes[1] = box_axes[1];
		axes[2] = box_axes[2];
		axes[3] = cross(edges[0], subtract(vertices[2], vertices[0]));
		unsigned next_axis = 4;
		for (const auto &edge : edges)
			for (const auto &box_axis : box_axes)
				axes[next_axis++] = cross(edge, box_axis);

		for (const auto &axis : axes) {
			if (axis[0] == 0.0 && axis[1] == 0.0 && axis[2] == 0.0) continue;
			const double first_projection = dot(vertices[0], axis);
			double minimum = first_projection;
			double maximum = first_projection;
			for (unsigned vertex = 1; vertex < vertices.size(); ++vertex) {
				const double projection = dot(vertices[vertex], axis);
				if (projection < minimum) minimum = projection;
				if (projection > maximum) maximum = projection;
			}
			const double radius = extent[0] * std::abs(axis[0])
				+ extent[1] * std::abs(axis[1]) + extent[2] * std::abs(axis[2]);
			if (minimum > radius || maximum < -radius) return false;
		}
		return true;
	}
	public:

	// Returns the first point where the finite segment intersects the triangle.
	// The test is two-sided and includes triangle edges and segment endpoints.
	static std::optional<SegmentTriangleHit3> Intersect_Segment(
		Vector3 segment_start, Vector3 segment_end,
		Vector3 first, Vector3 second, Vector3 third) noexcept
	{
		const Vector3 ab = second - first;
		const Vector3 ac = third - first;
		const Vector3 raw_normal = ab.Cross(ac);
		const Vector3 normal = raw_normal.Normalized();
		if (normal == Vector3{} || !std::isfinite(raw_normal.x)
			|| !std::isfinite(raw_normal.y) || !std::isfinite(raw_normal.z))
			return std::nullopt;

		const Vector3 direction = segment_end - segment_start;
		const float denominator = normal.Dot(direction);
		if (denominator == 0.0f || !std::isfinite(denominator)) return std::nullopt;

		const float fraction = normal.Dot(first - segment_start) / denominator;
		if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f)
			return std::nullopt;

		const Vector3 point = segment_start + direction * fraction;
		const float abs_x = std::abs(normal.x);
		const float abs_y = std::abs(normal.y);
		const float abs_z = std::abs(normal.z);
		unsigned axis1 = 0;
		unsigned axis2 = 1;
		if (abs_y > abs_x && abs_y >= abs_z) {
			axis1 = 0;
			axis2 = 2;
		} else if (abs_z > abs_x && abs_z > abs_y) {
			axis1 = 0;
			axis2 = 1;
		} else if (abs_x >= abs_y && abs_x >= abs_z) {
			axis1 = 1;
			axis2 = 2;
		}
		if (!Contains_Projected_Point(first, second, third, point, axis1, axis2))
			return std::nullopt;

		return SegmentTriangleHit3{fraction, point, normal};
	}

	// Tests the projection onto the plane formed by axis1 and axis2. Boundary
	// points count as inside; hit_edge reports that boundary case when supplied.
	static constexpr bool Contains_Projected_Point(
		Vector3 first, Vector3 second, Vector3 third, Vector3 point,
		unsigned axis1, unsigned axis2, bool *hit_edge = nullptr) noexcept
	{
		if (axis1 > 2 || axis2 > 2 || axis1 == axis2) return false;
		const auto component = [](Vector3 value, unsigned axis) constexpr {
			switch (axis) {
			case 0: return value.x;
			case 1: return value.y;
			default: return value.z;
			}
		};
		const auto project = [axis1, axis2, &component](Vector3 value) constexpr {
			return std::array<float, 2>{component(value, axis1), component(value, axis2)};
		};
		const auto a = project(first);
		const auto b = project(second);
		const auto c = project(third);
		const auto p = project(point);
		const auto cross = [](const auto &origin, const auto &end, const auto &sample) constexpr {
			return (end[0] - origin[0]) * (sample[1] - origin[1])
				- (end[1] - origin[1]) * (sample[0] - origin[0]);
		};
		const float area = cross(a, b, c);
		if (area != 0.0f) {
			const float ab = cross(a, b, p);
			const float bc = cross(b, c, p);
			const float ca = cross(c, a, p);
			const bool inside = area > 0.0f
				? ab >= 0.0f && bc >= 0.0f && ca >= 0.0f
				: ab <= 0.0f && bc <= 0.0f && ca <= 0.0f;
			if (inside && hit_edge != nullptr && (ab == 0.0f || bc == 0.0f || ca == 0.0f))
				*hit_edge = true;
			return inside;
		}

		const auto distance_squared = [](const auto &left, const auto &right) constexpr {
			const float dx = right[0] - left[0];
			const float dy = right[1] - left[1];
			return dx * dx + dy * dy;
		};
		const std::array<std::array<float, 2>, 3> vertices{a, b, c};
		unsigned start = 0;
		unsigned end = 1;
		float longest = distance_squared(a, b);
		for (unsigned candidate = 1; candidate < 3; ++candidate) {
			const unsigned next = (candidate + 1) % 3;
			const float length = distance_squared(vertices[candidate], vertices[next]);
			if (length > longest) {
				start = candidate;
				end = next;
				longest = length;
			}
		}
		if (longest == 0.0f) {
			const bool matches = p == a;
			if (matches && hit_edge != nullptr) *hit_edge = true;
			return matches;
		}
		if (cross(vertices[start], vertices[end], p) != 0.0f) return false;
		const float from_start = distance_squared(vertices[start], p);
		const float from_end = distance_squared(vertices[end], p);
		const bool on_segment = from_start <= longest && from_end <= longest;
		if (on_segment && hit_edge != nullptr) *hit_edge = true;
		return on_segment;
	}

	// Tests a positive or negative axis-aligned semi-infinite ray against a
	// triangle using its plane equation (normal.xyz, distance.w). Axis indices
	// must be a permutation of 0, 1, 2; positive_direction selects +axis.
	static AxisRayHit Intersect_Semi_Infinite_Axis_Ray(Vector3 first, Vector3 second,
		Vector3 third, Vector4 plane, Vector3 ray_start, unsigned ray_axis,
		unsigned axis1, unsigned axis2, bool positive_direction) noexcept
	{
		AxisRayHit result;
		if (ray_axis > 2 || axis1 > 2 || axis2 > 2 || ray_axis == axis1
			|| ray_axis == axis2 || axis1 == axis2
			|| !std::isfinite(plane.x) || !std::isfinite(plane.y)
			|| !std::isfinite(plane.z) || !std::isfinite(plane.w))
			return result;
		bool touches_edge = false;
		if (!Contains_Projected_Point(first, second, third, ray_start, axis1, axis2, &touches_edge))
			return result;
		const float sign = positive_direction ? 1.0f : -1.0f;
		const float signed_plane_distance = plane.x * ray_start.x + plane.y * ray_start.y
			+ plane.z * ray_start.z + plane.w;
		const float side = plane[ray_axis] * sign * signed_plane_distance;
		if (side < 0.0f) {
			result.intersects = true;
			result.touches_edge = touches_edge;
		} else if (side == 0.0f && plane[ray_axis] != 0.0f) {
			result.intersects = true;
			result.touches_edge = touches_edge;
			result.starts_inside = true;
		} else if (side == 0.0f && signed_plane_distance == 0.0f) {
			// Legacy case B: the ray is parallel to the plane and the start is
			// embedded in it. Report no hit; flag the start only when it is in
			// the triangle on the plane's dominant projection (TriClass::Contains_Point).
			if (Legacy_Contains_Point(first, second, third, plane, ray_start))
				result.starts_inside = true;
		}
		return result;
	}

private:
	// Legacy TriClass::Contains_Point: projects onto the plane most
	// perpendicular to the normal and requires all three edge cross products
	// to have the same (>= 0) sign.
	static bool Legacy_Contains_Point(Vector3 first, Vector3 second, Vector3 third,
		Vector4 plane, Vector3 point) noexcept
	{
		unsigned dominant = 0;
		const float x = std::abs(plane.x);
		const float y = std::abs(plane.y);
		const float z = std::abs(plane.z);
		float value = x;
		if (y > value) {
			dominant = 1;
			value = y;
		}
		if (z > value) dominant = 2;
		const unsigned axis1 = dominant == 0 ? 1u : 0u;
		const unsigned axis2 = dominant == 2 ? 1u : 2u;
		const Vector3 vertices[3] = {first, second, third};
		bool side[3];
		for (unsigned vi = 0; vi < 3; ++vi) {
			const Vector3 a = vertices[vi];
			const Vector3 b = vertices[(vi + 1) % 3];
			const float edge_x = b[axis1] - a[axis1];
			const float edge_y = b[axis2] - a[axis2];
			const float dp_x = point[axis1] - a[axis1];
			const float dp_y = point[axis2] - a[axis2];
			const float cross = edge_x * dp_y - edge_y * dp_x;
			side[vi] = cross >= 0.0f;
		}
		return side[0] == side[1] && side[1] == side[2];
	}
};
}
