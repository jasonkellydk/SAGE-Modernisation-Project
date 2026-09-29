export module Engine.Core.Math.FixedAngle;
import std;

export import Engine.Core.Math.TurnAngle;
export import Engine.Core.Math.Fixed;
import Engine.Core.Math.FixedAngleTables;

namespace Engine::Math::detail
{
// round(2*pi * 2^32): converts between Q16 radians and 2^32 turn units.
constexpr std::uint64_t kTwoPiQ32 = 26986075409u;

constexpr TurnAngle TurnFraction(std::int64_t raw, std::uint64_t perTurn, unsigned shift) noexcept
{
	const std::uint64_t magnitude = raw < 0 ? 0u - static_cast<std::uint64_t>(raw) : static_cast<std::uint64_t>(raw);
	const UInt128 numerator = (UInt128{0, magnitude} << shift) + UInt128{0, perTurn / 2u};
	// Only the fractional turn matters: reduce modulo 2^32 turn units.
	const auto units = static_cast<std::uint32_t>(numerator.DivideBy(perTurn).quotient.lo);
	return {raw < 0 ? 0u - units : units};
}
}

// Simulation-side angle operations on TurnAngle (2^32 units per turn, exact
// wrap-around). Everything is integer arithmetic and bit-identical across
// platforms; results are fixed point.
export namespace Engine::Math
{
constexpr TurnAngle TurnFromDegrees(Fixed degrees) noexcept
{
	// units = raw * 2^32 / (360 * 2^16), rounded.
	return detail::TurnFraction(degrees.Raw(), std::uint64_t{360} << Fixed::FractionBits, 32);
}

constexpr TurnAngle TurnFromDegrees(std::int64_t degrees) noexcept { return TurnFromDegrees(Fixed::FromInt(degrees)); }

constexpr TurnAngle TurnFromRadians(Fixed radians) noexcept
{
	// units = raw / 2^16 / 2pi * 2^32 = (raw << 48) / (2pi * 2^32).
	return detail::TurnFraction(radians.Raw(), detail::kTwoPiQ32, 48);
}

// Radians stored as an IEEE binary32 bit pattern (binary game files),
// converted with integer maths straight to turn units, without the 2^-16
// radian quantisation of going through Fixed. NaN/inf give zero.
constexpr TurnAngle TurnFromRadiansBinary32Bits(std::uint32_t bits) noexcept
{
	const bool negative = (bits >> 31) != 0;
	const int exponent = static_cast<int>((bits >> 23) & 0xFFu);
	if (exponent == 0 || exponent == 0xFF)
		return {};
	const std::uint64_t mantissa = (std::uint64_t{1} << 23) | (bits & 0x7FFFFFu);
	// units = mantissa * 2^(exponent - 150) * 2^32 / (2pi) = (mantissa << (exponent - 86)) / (2pi * 2^32).
	const int shift = exponent - 86;
	if (shift < -63)
		return {};
	const UInt128 numerator = shift >= 0 ? UInt128{0, mantissa} << static_cast<unsigned>(shift > 100 ? 100 : shift)
										 : UInt128{0, mantissa >> static_cast<unsigned>(-shift)};
	const auto units = static_cast<std::uint32_t>((numerator + UInt128{0, detail::kTwoPiQ32 / 2u}).DivideBy(detail::kTwoPiQ32).quotient.lo);
	return {negative ? 0u - units : units};
}

// [0, 2pi) and [0, 360).
constexpr Fixed Radians(TurnAngle angle) noexcept
{
	const UInt128 scaled = UInt128::Multiply(angle.units, detail::kTwoPiQ32) + (UInt128{0, 1} << 47);
	return Fixed::FromRaw(static_cast<std::int64_t>((scaled >> 48).lo));
}

constexpr Fixed Degrees(TurnAngle angle) noexcept
{
	const UInt128 scaled = UInt128::Multiply(angle.units, std::uint64_t{360} << Fixed::FractionBits) + (UInt128{0, 1} << 31);
	return Fixed::FromRaw(static_cast<std::int64_t>((scaled >> 32).lo));
}

// Shortest signed rotation from `from` to `to`, in turn units.
constexpr std::int32_t DeltaTo(TurnAngle from, TurnAngle to) noexcept
{
	return static_cast<std::int32_t>(to.units - from.units);
}

constexpr TurnAngle operator-(TurnAngle angle) noexcept { return {0u - angle.units}; }
constexpr TurnAngle &operator+=(TurnAngle &angle, TurnAngle delta) noexcept { return angle = angle + delta; }
constexpr TurnAngle &operator-=(TurnAngle &angle, TurnAngle delta) noexcept { return angle = angle - delta; }

// Sine and cosine in fixed point, from the shared integer Sin_Cos (Q1.30),
// rounded to Q16 (sine accurate to ~3.5e-6, under one Q16 unit).
constexpr Fixed Sin(TurnAngle angle) noexcept
{
	const std::int64_t q30 = Sin_Cos(angle).sine;
	return Fixed::FromRaw((q30 + (q30 >= 0 ? (1 << 13) : -(1 << 13))) / (1 << 14));
}

// Cosine via the sine of the quarter-shifted angle: Sin_Cos's cosine series
// is truncated one term earlier (~2.5e-5) than its sine series (~3.5e-6).
constexpr Fixed Cos(TurnAngle angle) noexcept
{
	return Sin(angle + TurnAngle::Quarter_Turn());
}

// Direction of (x, y); zero for (0, 0). Integer CORDIC, about 1 turn unit
// (8.4e-8 degrees) of error before input quantisation; axes are exact.
constexpr TurnAngle Atan2(Fixed y, Fixed x) noexcept
{
	if (y.Raw() == 0)
		return {x.Raw() < 0 ? 0x80000000u : 0u};
	if (x.Raw() == 0)
		return {y.Raw() > 0 ? 0x40000000u : 0xC0000000u};
	std::int64_t cx = x.Raw();
	std::int64_t cy = y.Raw();
	std::uint32_t z = 0;
	// Rotate into the right half-plane; CORDIC converges within +/-99 degrees.
	if (cx < 0)
	{
		cx = cx == (std::int64_t{1} << 63) ? (std::int64_t{1} << 62) : -cx;
		cy = cy == (std::int64_t{1} << 63) ? (std::int64_t{1} << 62) : -cy;
		z = 0x80000000u;
	}
	// Normalise magnitude into [2^29, 2^30) so iterations neither overflow nor lose bits.
	const auto magnitude = [](std::int64_t v) { return v < 0 ? 0u - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v); };
	std::uint64_t largest = magnitude(cx) > magnitude(cy) ? magnitude(cx) : magnitude(cy);
	while (largest >= (std::uint64_t{1} << 30))
	{
		cx >>= 1;
		cy >>= 1;
		largest >>= 1;
	}
	while (largest < (std::uint64_t{1} << 29))
	{
		cx *= 2;
		cy *= 2;
		largest <<= 1;
	}
	for (int step = 0; step < Tables::CordicSteps; ++step)
	{
		const std::int64_t dx = cy >> step;
		const std::int64_t dy = cx >> step;
		if (cy > 0)
		{
			cx += dx;
			cy -= dy;
			z += Tables::CordicAtan[static_cast<std::size_t>(step)];
		}
		else
		{
			cx -= dx;
			cy += dy;
			z -= Tables::CordicAtan[static_cast<std::size_t>(step)];
		}
	}
	return {z};
}
}
