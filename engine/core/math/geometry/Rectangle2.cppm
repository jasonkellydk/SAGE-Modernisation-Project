module;

export module Engine.Core.Math.Rectangle2;

import Engine.Core.Math.Vector2;

export namespace Engine::Math
{
struct Rectangle2 final
{
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;

	static constexpr Rectangle2 From_Corners(Vector2 upper_left, Vector2 lower_right) noexcept
	{
		return {upper_left.x, upper_left.y, lower_right.x, lower_right.y};
	}

	constexpr float Width() const noexcept { return right - left; }
	constexpr float Height() const noexcept { return bottom - top; }
	constexpr Vector2 Center() const noexcept { return {(left + right) * 0.5f, (top + bottom) * 0.5f}; }
	constexpr Vector2 Extent() const noexcept { return {Width() * 0.5f, Height() * 0.5f}; }
	constexpr bool Is_Valid() const noexcept { return left <= right && top <= bottom; }
	constexpr bool Contains(Vector2 point) const noexcept
	{
		return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
	}
	constexpr void Include(Rectangle2 other) noexcept
	{
		if (!other.Is_Valid()) return;
		if (!Is_Valid()) { *this = other; return; }
		if (other.left < left) left = other.left;
		if (other.top < top) top = other.top;
		if (other.right > right) right = other.right;
		if (other.bottom > bottom) bottom = other.bottom;
	}
	friend constexpr bool operator==(Rectangle2, Rectangle2) noexcept = default;
};
}
