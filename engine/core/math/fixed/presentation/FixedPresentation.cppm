export module Engine.Core.Math.FixedPresentation;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.Vector2;
export import Engine.Core.Math.Vector3;

// One-way bridge from simulation (fixed point) to client code (float):
// rendering, audio and UI read simulation state through these. There is
// deliberately no float-to-fixed conversion; simulation input comes from
// config (Fixed::ParseDecimal) or binary data (Fixed::FromBinary32Bits).
export namespace Engine::Math
{
inline float ToFloat(Fixed value) noexcept
{
	return static_cast<float>(static_cast<double>(value.Raw()) / static_cast<double>(Fixed::OneRaw));
}

inline float ToRadiansFloat(TurnAngle angle) noexcept { return ToFloat(Radians(angle)); }

inline Vector2 ToFloat(FixedVector2 value) noexcept { return Vector2{ToFloat(value.x), ToFloat(value.y)}; }
inline Vector3 ToFloat(FixedVector3 value) noexcept { return Vector3{ToFloat(value.x), ToFloat(value.y), ToFloat(value.z)}; }

// Interpolation between two simulation ticks, alpha in [0, 1].
inline float Interpolate(Fixed previous, Fixed current, float alpha) noexcept
{
	const float from = ToFloat(previous);
	return from + (ToFloat(current) - from) * alpha;
}
}
