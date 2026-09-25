module;

#include <cstdint>

export module Engine.Core.Math.TurnAngle;

export namespace Engine::Math
{
// One full revolution is represented by all 2^32 values. Addition wraps by
// definition, which makes phase accumulation exact and independent of floats.
struct TurnAngle final
{
	std::uint32_t units = 0;

	static constexpr TurnAngle Quarter_Turn() noexcept { return {0x40000000u}; }
	static constexpr TurnAngle Half_Turn() noexcept { return {0x80000000u}; }
	friend constexpr TurnAngle operator+(TurnAngle left, TurnAngle right) noexcept
	{
		return {left.units + right.units};
	}
	friend constexpr TurnAngle operator-(TurnAngle left, TurnAngle right) noexcept
	{
		return {left.units - right.units};
	}
	friend constexpr bool operator==(TurnAngle, TurnAngle) noexcept = default;
};

struct FixedSinCos final
{
	// Signed Q1.30 values. Conversion to binary float is exact.
	std::int32_t sine = 0;
	std::int32_t cosine = 1 << 30;
	friend constexpr bool operator==(FixedSinCos, FixedSinCos) noexcept = default;
};

// Integer polynomial evaluation gives identical results on every platform
// with standard 32/64-bit integer arithmetic. Accuracy is approximately
// 2e-5 over one revolution; values are intended for deterministic simulation.
inline constexpr FixedSinCos Sin_Cos(TurnAngle angle) noexcept
{
	constexpr std::int64_t one = std::int64_t{1} << 30;
	constexpr std::int64_t quarter = std::uint32_t{1} << 30;
	constexpr std::int64_t tau_q30 = 6746518852LL;
	constexpr auto multiply_q30 = [one](std::int64_t left, std::int64_t right) constexpr {
		return (left * right) / one;
	};

	if ((angle.units & 0x3fffffffu) == 0) {
		switch (angle.units >> 30) {
		case 0: return {0, static_cast<std::int32_t>(one)};
		case 1: return {static_cast<std::int32_t>(one), 0};
		case 2: return {0, static_cast<std::int32_t>(-one)};
		default: return {static_cast<std::int32_t>(-one), 0};
		}
	}

	const std::uint32_t quadrant = angle.units >> 30;
	const std::uint32_t within_quadrant = angle.units & 0x3fffffffu;
	const std::uint32_t phase = (quadrant == 1 || quadrant == 3)
		? static_cast<std::uint32_t>(quarter) - within_quadrant
		: within_quadrant;
	const std::int64_t x = (static_cast<std::int64_t>(phase) * tau_q30) / (std::int64_t{1} << 32);
	const std::int64_t x2 = multiply_q30(x, x);
	const std::int64_t x3 = multiply_q30(x2, x);
	const std::int64_t x4 = multiply_q30(x2, x2);
	const std::int64_t x5 = multiply_q30(x4, x);
	const std::int64_t x6 = multiply_q30(x4, x2);
	const std::int64_t x7 = multiply_q30(x6, x);
	const std::int64_t x8 = multiply_q30(x4, x4);
	const std::int64_t x9 = multiply_q30(x8, x);

	const std::int64_t sine = x - multiply_q30(x3, 178956970)
		+ multiply_q30(x5, 8947849) - multiply_q30(x7, 213044)
		+ multiply_q30(x9, 2957);
	const std::int64_t cosine = one - multiply_q30(x2, 536870912)
		+ multiply_q30(x4, 44739242) - multiply_q30(x6, 1491308)
		+ multiply_q30(x8, 26631);

	const std::int64_t signed_sine = quadrant >= 2 ? -sine : sine;
	const std::int64_t signed_cosine = (quadrant == 1 || quadrant == 2) ? -cosine : cosine;
	return {static_cast<std::int32_t>(signed_sine), static_cast<std::int32_t>(signed_cosine)};
}
}
