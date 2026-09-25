module;

#include <cmath>
#include <numbers>

export module Engine.Core.Math.Scalar;

export namespace Engine::Math
{
inline constexpr float Pi = std::numbers::pi_v<float>;
inline constexpr float Tau = 2.0f * Pi;
inline constexpr float DefaultTolerance = 0.0001f;

constexpr float DegreesToRadians(float degrees) noexcept { return degrees * (Pi / 180.0f); }
constexpr float DegreesToRadians(int degrees) noexcept { return DegreesToRadians(static_cast<float>(degrees)); }
constexpr double DegreesToRadians(double degrees) noexcept
{
	return degrees * (std::numbers::pi_v<double> / 180.0);
}
constexpr float RadiansToDegrees(float radians) noexcept { return radians * (180.0f / Pi); }
constexpr float RadiansToDegrees(int radians) noexcept { return RadiansToDegrees(static_cast<float>(radians)); }
constexpr double RadiansToDegrees(double radians) noexcept
{
	return radians * (180.0 / std::numbers::pi_v<double>);
}

// Reduces a finite angle to [-Pi, Pi]. Non-finite input is returned unchanged.
inline float WrapRadians(float radians) noexcept
{
	if (!std::isfinite(radians)) return radians;
	float wrapped = std::remainder(radians, Tau);
	if (wrapped == -Pi) wrapped = Pi;
	return wrapped;
}

// Wraps a finite value into [minimum, maximum). Invalid bounds or non-finite
// input are returned unchanged so callers can decide how to handle bad data.
inline float WrapToRange(float value, float minimum, float maximum) noexcept
{
	if (!std::isfinite(value) || !(minimum < maximum)) return value;
	const float range = maximum - minimum;
	if (!std::isfinite(range)) return value;
	float wrapped = std::fmod(value - minimum, range);
	if (wrapped < 0.0f) wrapped += range;
	return minimum + wrapped;
}

inline float ClampFinite(float value, float minimum, float maximum, float fallback = 0.0f) noexcept
{
	if (!(minimum <= maximum) || !std::isfinite(value)) return fallback;
	return value < minimum ? minimum : value > maximum ? maximum : value;
}

constexpr float Lerp(float start, float end, float amount) noexcept
{
	return start + (end - start) * amount;
}

constexpr double Lerp(double start, double end, float amount) noexcept
{
	return start + (end - start) * amount;
}

constexpr float InverseLerp(float start, float end, float value) noexcept
{
	return (value - start) / (end - start);
}

constexpr double InverseLerp(double start, double end, float value) noexcept
{
	return (value - start) / (end - start);
}
}
