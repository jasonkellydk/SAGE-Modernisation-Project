module;

#include <cmath>
#include <algorithm>

export module Engine.Core.Math.Vector2;

export namespace Engine::Math
{
struct Vector2 final
{
	float x = 0.0f;
	float y = 0.0f;

	friend constexpr bool operator==(Vector2, Vector2) noexcept = default;
	friend constexpr Vector2 operator-(Vector2 value) noexcept { return {-value.x, -value.y}; }
	friend constexpr Vector2 operator+(Vector2 a, Vector2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
	friend constexpr Vector2 operator-(Vector2 a, Vector2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
	friend constexpr Vector2 operator*(Vector2 value, float scale) noexcept { return {value.x * scale, value.y * scale}; }
	friend constexpr Vector2 operator*(float scale, Vector2 value) noexcept { return value * scale; }
	friend constexpr Vector2 operator/(Vector2 value, float scale) noexcept { return {value.x / scale, value.y / scale}; }
	constexpr Vector2& operator+=(Vector2 other) noexcept { x += other.x; y += other.y; return *this; }
	constexpr Vector2& operator-=(Vector2 other) noexcept { x -= other.x; y -= other.y; return *this; }
	constexpr Vector2& operator*=(float scale) noexcept { x *= scale; y *= scale; return *this; }
	constexpr Vector2& operator/=(float scale) noexcept { x /= scale; y /= scale; return *this; }
	constexpr float& operator[](unsigned index) noexcept { return index == 0 ? x : y; }
	constexpr const float& operator[](unsigned index) const noexcept { return index == 0 ? x : y; }
	constexpr float Dot(Vector2 other) const noexcept { return x * other.x + y * other.y; }
	Vector2 Rotated(float radians) const noexcept
	{
		const float cosine = std::cos(radians);
		const float sine = std::sin(radians);
		return {x * cosine - y * sine, x * sine + y * cosine};
	}
	float Length() const noexcept { return std::sqrt(Dot(*this)); }
	Vector2 Normalized() const noexcept
	{
		if (!std::isfinite(x) || !std::isfinite(y)) return {};
		const float scale = (std::max)(std::abs(x), std::abs(y));
		if (scale == 0.0f) return {};
		const Vector2 scaled = *this / scale;
		return scaled / std::sqrt(scaled.Dot(scaled));
	}
	// Legacy bit-compatible WWMath Vector2::Normalize: zero stays zero and
	// non-finite values pass through. Use for simulation parity only.
	Vector2 Normalized_Legacy() const noexcept
	{
		const float len2 = x * x + y * y;
		if (len2 != 0.0f) {
			const float inv = 1.0f / std::sqrt(len2);
			return {x * inv, y * inv};
		}
		return *this;
	}
};
}
