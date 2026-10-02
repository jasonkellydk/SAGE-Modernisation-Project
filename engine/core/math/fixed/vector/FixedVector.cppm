export module Engine.Core.Math.FixedVector;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedAngle;

// Simulation vectors in fixed point (client code uses the float Vector2/3).
export namespace Engine::Math
{
struct FixedVector2
{
	Fixed x;
	Fixed y;

	constexpr bool operator==(const FixedVector2 &) const noexcept = default;
	constexpr FixedVector2 operator+(FixedVector2 o) const noexcept { return {x + o.x, y + o.y}; }
	constexpr FixedVector2 operator-(FixedVector2 o) const noexcept { return {x - o.x, y - o.y}; }
	constexpr FixedVector2 operator-() const noexcept { return {-x, -y}; }
	constexpr FixedVector2 operator*(Fixed s) const noexcept { return {x * s, y * s}; }
	constexpr FixedVector2 operator/(Fixed s) const noexcept { return {x / s, y / s}; }
	constexpr FixedVector2 &operator+=(FixedVector2 o) noexcept { return *this = *this + o; }
	constexpr FixedVector2 &operator-=(FixedVector2 o) noexcept { return *this = *this - o; }
	constexpr FixedVector2 &operator*=(Fixed s) noexcept { return *this = *this * s; }
};

struct FixedVector3
{
	Fixed x;
	Fixed y;
	Fixed z;

	constexpr bool operator==(const FixedVector3 &) const noexcept = default;
	constexpr FixedVector3 operator+(FixedVector3 o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	constexpr FixedVector3 operator-(FixedVector3 o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	constexpr FixedVector3 operator-() const noexcept { return {-x, -y, -z}; }
	constexpr FixedVector3 operator*(Fixed s) const noexcept { return {x * s, y * s, z * s}; }
	constexpr FixedVector3 operator/(Fixed s) const noexcept { return {x / s, y / s, z / s}; }
	constexpr FixedVector3 &operator+=(FixedVector3 o) noexcept { return *this = *this + o; }
	constexpr FixedVector3 &operator-=(FixedVector3 o) noexcept { return *this = *this - o; }
	constexpr FixedVector3 &operator*=(Fixed s) noexcept { return *this = *this * s; }

	constexpr FixedVector2 XY() const noexcept { return {x, y}; }
};

constexpr Fixed Dot(FixedVector2 a, FixedVector2 b) noexcept { return a.x * b.x + a.y * b.y; }
constexpr Fixed Dot(FixedVector3 a, FixedVector3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
// z of the 3D cross product: positive when b is counter-clockwise of a.
constexpr Fixed Cross(FixedVector2 a, FixedVector2 b) noexcept { return a.x * b.y - a.y * b.x; }
constexpr FixedVector3 Cross(FixedVector3 a, FixedVector3 b) noexcept
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr Fixed LengthSquared(FixedVector2 v) noexcept { return Dot(v, v); }
constexpr Fixed LengthSquared(FixedVector3 v) noexcept { return Dot(v, v); }
constexpr Fixed Length(FixedVector2 v) noexcept { return Sqrt(LengthSquared(v)); }
constexpr Fixed Length(FixedVector3 v) noexcept { return Sqrt(LengthSquared(v)); }
constexpr Fixed DistanceSquared(FixedVector2 a, FixedVector2 b) noexcept { return LengthSquared(b - a); }
constexpr Fixed DistanceSquared(FixedVector3 a, FixedVector3 b) noexcept { return LengthSquared(b - a); }
constexpr Fixed Distance(FixedVector2 a, FixedVector2 b) noexcept { return Length(b - a); }
constexpr Fixed Distance(FixedVector3 a, FixedVector3 b) noexcept { return Length(b - a); }

// Unit vector, or zero for a zero-length input.
constexpr FixedVector2 Normalize(FixedVector2 v) noexcept
{
	const Fixed length = Length(v);
	return length == Fixed{} ? FixedVector2{} : v / length;
}

constexpr FixedVector3 Normalize(FixedVector3 v) noexcept
{
	const Fixed length = Length(v);
	return length == Fixed{} ? FixedVector3{} : v / length;
}

constexpr FixedVector2 Direction(TurnAngle angle) noexcept { return {Cos(angle), Sin(angle)}; }
constexpr TurnAngle Heading(FixedVector2 v) noexcept { return Atan2(v.y, v.x); }

constexpr FixedVector2 Rotate(FixedVector2 v, TurnAngle angle) noexcept
{
	const Fixed c = Cos(angle);
	const Fixed s = Sin(angle);
	return {v.x * c - v.y * s, v.x * s + v.y * c};
}
}
