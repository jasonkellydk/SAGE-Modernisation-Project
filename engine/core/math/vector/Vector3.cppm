module;

#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstddef>

export module Engine.Core.Math.Vector3;

export namespace Engine::Math
{
struct Vector3 final
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr float &operator[](std::size_t index) noexcept
	{
		assert(index < 3);
		return index == 0 ? x : index == 1 ? y : z;
	}
	constexpr const float &operator[](std::size_t index) const noexcept
	{
		assert(index < 3);
		return index == 0 ? x : index == 1 ? y : z;
	}

	friend constexpr bool operator==(Vector3, Vector3) noexcept = default;
	friend constexpr Vector3 operator-(Vector3 value) noexcept { return {-value.x, -value.y, -value.z}; }
	friend constexpr Vector3 operator+(Vector3 a, Vector3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
	friend constexpr Vector3 operator-(Vector3 a, Vector3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
	friend constexpr Vector3 operator*(Vector3 value, float scale) noexcept { return {value.x * scale, value.y * scale, value.z * scale}; }
	friend constexpr Vector3 operator*(float scale, Vector3 value) noexcept { return value * scale; }
	friend constexpr Vector3 operator/(Vector3 value, float scale) noexcept { return {value.x / scale, value.y / scale, value.z / scale}; }
	constexpr Vector3 &operator+=(Vector3 other) noexcept { x += other.x; y += other.y; z += other.z; return *this; }
	constexpr Vector3 &operator-=(Vector3 other) noexcept { x -= other.x; y -= other.y; z -= other.z; return *this; }
	constexpr Vector3 &operator*=(float scale) noexcept { x *= scale; y *= scale; z *= scale; return *this; }
	constexpr float Dot(Vector3 other) const noexcept { return x * other.x + y * other.y + z * other.z; }
	constexpr Vector3 Cross(Vector3 other) const noexcept
	{
		return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
	}
	constexpr float Length_Squared() const noexcept { return Dot(*this); }
	float Length() const noexcept { return std::sqrt(Length_Squared()); }
	Vector3 Normalized() const noexcept
	{
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return {};
		const float scale = (std::max)((std::max)(std::abs(x), std::abs(y)), std::abs(z));
		if (scale == 0.0f) return {};
		const Vector3 scaled = *this / scale;
		return scaled / std::sqrt(scaled.Dot(scaled));
	}
	// Legacy bit-compatible WWMath Vector3::Normalize: zero stays zero and
	// non-finite values pass through. Use for simulation parity only.
	Vector3 Normalized_Legacy() const noexcept
	{
		const float len2 = x * x + y * y + z * z;
		if (len2 != 0.0f) {
			const float inv = 1.0f / std::sqrt(len2);
			return {x * inv, y * inv, z * inv};
		}
		return *this;
	}
};
}
