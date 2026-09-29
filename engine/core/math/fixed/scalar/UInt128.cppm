export module Engine.Core.Math.UInt128;
import std;

export namespace Engine::Math
{
// Portable unsigned 128-bit integer for fixed-point intermediates. Compiler
// __int128 support differs across targets (clang-cl lacks the division
// runtime), so products, quotients and square roots are computed here.
struct UInt128
{
	std::uint64_t hi{0};
	std::uint64_t lo{0};

	constexpr auto operator<=>(const UInt128 &) const noexcept = default;

	static constexpr UInt128 Multiply(std::uint64_t a, std::uint64_t b) noexcept
	{
		const std::uint64_t aLo = a & 0xFFFFFFFFu;
		const std::uint64_t aHi = a >> 32;
		const std::uint64_t bLo = b & 0xFFFFFFFFu;
		const std::uint64_t bHi = b >> 32;
		const std::uint64_t ll = aLo * bLo;
		const std::uint64_t lh = aLo * bHi;
		const std::uint64_t hl = aHi * bLo;
		const std::uint64_t hh = aHi * bHi;
		const std::uint64_t middle = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
		return {hh + (lh >> 32) + (hl >> 32) + (middle >> 32), (middle << 32) | (ll & 0xFFFFFFFFu)};
	}

	constexpr UInt128 operator+(UInt128 other) const noexcept
	{
		const std::uint64_t low = lo + other.lo;
		return {hi + other.hi + (low < lo ? 1u : 0u), low};
	}

	constexpr UInt128 operator-(UInt128 other) const noexcept
	{
		return {hi - other.hi - (lo < other.lo ? 1u : 0u), lo - other.lo};
	}

	constexpr UInt128 operator<<(unsigned shift) const noexcept
	{
		if (shift == 0)
			return *this;
		if (shift >= 64)
			return {lo << (shift - 64), 0};
		return {(hi << shift) | (lo >> (64 - shift)), lo << shift};
	}

	constexpr UInt128 operator>>(unsigned shift) const noexcept
	{
		if (shift == 0)
			return *this;
		if (shift >= 64)
			return {0, hi >> (shift - 64)};
		return {hi >> shift, (lo >> shift) | (hi << (64 - shift))};
	}

	constexpr unsigned BitWidth() const noexcept
	{
		unsigned width = 0;
		for (std::uint64_t word = hi ? hi : lo; word != 0; word >>= 1)
			++width;
		return hi ? width + 64 : width;
	}

	// Quotient and remainder by a non-zero 64-bit divisor (defined below).
	constexpr struct UInt128Division DivideBy(std::uint64_t divisor) const noexcept;

	// floor(sqrt(value)); always fits in 64 bits.
	constexpr std::uint64_t SquareRoot() const noexcept
	{
		UInt128 remainder = *this;
		UInt128 root{};
		unsigned width = BitWidth();
		UInt128 bit = UInt128{0, 1} << (width == 0 ? 0 : ((width - 1) & ~1u));
		while (bit != UInt128{})
		{
			const UInt128 candidate = root + bit;
			if (remainder >= candidate)
			{
				remainder = remainder - candidate;
				root = (root >> 1) + bit;
			}
			else
			{
				root = root >> 1;
			}
			bit = bit >> 2;
		}
		return root.lo;
	}
};

struct UInt128Division
{
	UInt128 quotient;
	std::uint64_t remainder;
};

// Restoring long division: bit-exact on every platform.
constexpr UInt128Division UInt128::DivideBy(std::uint64_t divisor) const noexcept
{
	if (hi == 0)
		return {{0, lo / divisor}, lo % divisor};
	UInt128 quotient{};
	UInt128 remainder{};
	for (int bit = static_cast<int>(BitWidth()) - 1; bit >= 0; --bit)
	{
		remainder = remainder << 1;
		remainder.lo |= ((*this >> static_cast<unsigned>(bit)).lo & 1u);
		quotient = quotient << 1;
		if (remainder >= UInt128{0, divisor})
		{
			remainder = remainder - UInt128{0, divisor};
			quotient.lo |= 1u;
		}
	}
	return {quotient, remainder.lo};
}
}
