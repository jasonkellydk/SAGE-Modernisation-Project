module;

#include <cmath>
#include <algorithm>
#include <cassert>
#include <cstddef>

export module Engine.Core.Math.Vector4;

export namespace Engine::Math
{
struct Vector4 final
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 0.0f;

	constexpr float &operator[](std::size_t index) noexcept
	{
		assert(index < 4);
		return index == 0 ? x : index == 1 ? y : index == 2 ? z : w;
	}
	constexpr const float &operator[](std::size_t index) const noexcept
	{
		assert(index < 4);
		return index == 0 ? x : index == 1 ? y : index == 2 ? z : w;
	}

	friend constexpr bool operator==(Vector4, Vector4) noexcept = default;
	friend constexpr Vector4 operator+(Vector4 a, Vector4 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
	friend constexpr Vector4 operator-(Vector4 a, Vector4 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
	friend constexpr Vector4 operator*(Vector4 value, float scale) noexcept { return {value.x * scale, value.y * scale, value.z * scale, value.w * scale}; }
	friend constexpr Vector4 operator/(Vector4 value, float scale) noexcept { return {value.x / scale, value.y / scale, value.z / scale, value.w / scale}; }
	constexpr float Dot(Vector4 other) const noexcept { return x * other.x + y * other.y + z * other.z + w * other.w; }
	float Length() const noexcept { return std::sqrt(Dot(*this)); }
	Vector4 Normalized() const noexcept
	{
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) return {};
		const float scale = (std::max)((std::max)((std::max)(std::abs(x), std::abs(y)), std::abs(z)), std::abs(w));
		if (scale == 0.0f) return {};
		const Vector4 scaled = *this / scale;
		return scaled / std::sqrt(scaled.Dot(scaled));
	}
};
}
