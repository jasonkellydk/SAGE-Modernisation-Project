module;

#include <algorithm>
#include <cmath>

export module Engine.Core.Math.Quaternion;

export import Engine.Core.Math.AffineTransform3;
export import Engine.Core.Math.Vector2;

export namespace Engine::Math
{
// Hamilton quaternion stored as (x, y, z, w). Unit values represent rotations.
struct Quaternion final
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;

	static Quaternion Trackball_Drag(Vector2 start, Vector2 end, float sphere_radius) noexcept
	{
		if (start == end || !(sphere_radius > 0.0f) || !std::isfinite(sphere_radius)
			|| !std::isfinite(start.x) || !std::isfinite(start.y)
			|| !std::isfinite(end.x) || !std::isfinite(end.y))
			return {};
		if (sphere_radius * 0.70710678f == 0.0f)
			return {};
		const auto project = [sphere_radius](Vector2 point) {
			const float distance = std::hypot(point.x, point.y);
			const float half_sphere = sphere_radius * 0.70710678f;
			const float z = distance < half_sphere
				? sphere_radius * std::sqrt((std::max)(1.0f - (distance / sphere_radius) * (distance / sphere_radius), 0.0f))
				: (half_sphere / distance) * half_sphere;
			return Vector3{point.x, point.y, z};
		};
		const Vector3 first = project(start);
		const Vector3 second = project(end);
		const float first_scale = (std::max)({std::abs(first.x), std::abs(first.y), std::abs(first.z)});
		const float second_scale = (std::max)({std::abs(second.x), std::abs(second.y), std::abs(second.z)});
		const Vector3 axis = (second / second_scale).Cross(first / first_scale).Normalized();
		const float distance = std::hypot(first.x - second.x, first.y - second.y, first.z - second.z);
		const float radius_ratio = distance / sphere_radius;
		const float half_chord = radius_ratio >= 2.0f ? 1.0f : radius_ratio * 0.5f;
		const float half_angle = std::asin(half_chord);
		const float sine = std::sin(half_angle);
		return Quaternion{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half_angle)}.Normalized();
	}

	friend constexpr bool operator==(Quaternion, Quaternion) noexcept = default;
	static Quaternion From_Axis_Angle(Vector3 axis, float radians) noexcept
	{
		if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z)
			|| !std::isfinite(radians))
			return {};
		axis = axis.Normalized();
		if (axis == Vector3{}) return {};
		const float half_angle = radians * 0.5f;
		const float sine = std::sin(half_angle);
		return {axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half_angle)};
	}

	static Quaternion From_Rotation(const AffineTransform3 &transform) noexcept
	{
		const auto &m = transform.elements;
		const float trace = m[0] + m[5] + m[10];
		Quaternion result;
		if (trace > 0.0f) {
			float scale = std::sqrt(trace + 1.0f);
			result.w = scale * 0.5f;
			scale = 0.5f / scale;
			result.x = (m[9] - m[6]) * scale;
			result.y = (m[2] - m[8]) * scale;
			result.z = (m[4] - m[1]) * scale;
		} else {
			unsigned axis = 0;
			if (m[5] > m[0]) axis = 1;
			if (m[10] > m[axis * 5]) axis = 2;
			const unsigned next = (axis + 1) % 3;
			const unsigned last = (next + 1) % 3;
			float scale = std::sqrt((m[axis * 5] - (m[next * 5] + m[last * 5])) + 1.0f);
			const float component = scale * 0.5f;
			if (axis == 0) result.x = component;
			else if (axis == 1) result.y = component;
			else result.z = component;
			if (scale != 0.0f) scale = 0.5f / scale;
			const auto at = [&m](unsigned row, unsigned column) { return m[row * 4 + column]; };
			result.w = (at(last, next) - at(next, last)) * scale;
			const float next_part = (at(next, axis) + at(axis, next)) * scale;
			const float last_part = (at(last, axis) + at(axis, last)) * scale;
			if (next == 0) result.x = next_part;
			else if (next == 1) result.y = next_part;
			else result.z = next_part;
			if (last == 0) result.x = last_part;
			else if (last == 1) result.y = last_part;
			else result.z = last_part;
		}
		return result.Normalized();
	}
	constexpr Quaternion Conjugate() const noexcept { return {-x, -y, -z, w}; }
	constexpr float Dot(Quaternion other) const noexcept { return x * other.x + y * other.y + z * other.z + w * other.w; }
	float Length() const noexcept { return std::sqrt(Dot(*this)); }
	Quaternion Normalized() const noexcept
	{
		const float length = Length();
		if (!(length > 0.0f) || !std::isfinite(length)) return {};
		return {x / length, y / length, z / length, w / length};
	}
	AffineTransform3 To_Rotation_Transform() const noexcept
	{
		const float x2 = x * x, y2 = y * y, z2 = z * z;
		const float xy = x * y, xz = x * z, yz = y * z;
		const float wx = w * x, wy = w * y, wz = w * z;
		AffineTransform3 result;
		result.elements = {
			1.0f - 2.0f * (y2 + z2), 2.0f * (xy - wz), 2.0f * (xz + wy), 0.0f,
			2.0f * (xy + wz), 1.0f - 2.0f * (x2 + z2), 2.0f * (yz - wx), 0.0f,
			2.0f * (xz - wy), 2.0f * (yz + wx), 1.0f - 2.0f * (x2 + y2), 0.0f};
		return result;
	}
	constexpr Vector3 Rotate_Vector(Vector3 vector) const noexcept
	{
		const float first_x = w * vector.x + (y * vector.z - vector.y * z);
		const float first_y = w * vector.y - (x * vector.z - vector.x * z);
		const float first_z = w * vector.z + (x * vector.y - vector.x * y);
		const float scalar = -(x * vector.x + y * vector.y + z * vector.z);
		return {
			scalar * (-x) + w * first_x + (first_y * (-z) - (-y) * first_z),
			scalar * (-y) + w * first_y - (first_x * (-z) - (-x) * first_z),
			scalar * (-z) + w * first_z + (first_x * (-y) - (-x) * first_y)};
	}
	friend constexpr Quaternion operator*(Quaternion a, Quaternion b) noexcept
	{
		return {
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
	}
};
}
