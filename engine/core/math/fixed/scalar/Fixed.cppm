export module Engine.Core.Math.Fixed;
import std;

export import Engine.Core.Math.UInt128;

namespace Engine::Math::detail
{
constexpr std::uint64_t Magnitude(std::int64_t value) noexcept
{
	return value < 0 ? 0u - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
}

constexpr std::int64_t kMax = (std::numeric_limits<std::int64_t>::max)();
constexpr std::int64_t kMin = (std::numeric_limits<std::int64_t>::min)();

// Applies a sign to an unsigned magnitude, saturating to the int64 range.
constexpr std::int64_t Signed(UInt128 magnitude, bool negative) noexcept
{
	const std::uint64_t limit = negative ? static_cast<std::uint64_t>(kMax) + 1u : static_cast<std::uint64_t>(kMax);
	if (magnitude.hi != 0 || magnitude.lo > limit)
		return negative ? kMin : kMax;
	return negative ? static_cast<std::int64_t>(0u - magnitude.lo) : static_cast<std::int64_t>(magnitude.lo);
}
}

export namespace Engine::Math
{
// Deterministic Q48.16 fixed-point scalar for all simulation state (the
// float types in this library are for client-side code: rendering, audio, UI).
// Resolution 1/65536; range about +/-1.4e14. Every operation is integer-only
// and bit-identical across compilers and CPUs. Arithmetic saturates on
// overflow and rounds to nearest (halves away from zero).
class Fixed
{
public:
	static constexpr int FractionBits = 16;
	static constexpr std::int64_t OneRaw = std::int64_t{1} << FractionBits;

	constexpr Fixed() noexcept = default;

	static constexpr Fixed FromRaw(std::int64_t raw) noexcept
	{
		Fixed value;
		value.m_raw = raw;
		return value;
	}

	static constexpr Fixed FromInt(std::int64_t value) noexcept
	{
		return FromRaw(detail::Signed(UInt128{0, detail::Magnitude(value)} << FractionBits, value < 0));
	}

	// numerator / denominator, rounded to nearest. Zero denominator saturates.
	static constexpr Fixed FromRatio(std::int64_t numerator, std::int64_t denominator) noexcept
	{
		return FromInt(numerator) / FromInt(denominator);
	}

	static constexpr Fixed Max() noexcept { return FromRaw(detail::kMax); }
	static constexpr Fixed Min() noexcept { return FromRaw(detail::kMin); }
	static constexpr Fixed Epsilon() noexcept { return FromRaw(1); }
	static constexpr Fixed One() noexcept { return FromRaw(OneRaw); }
	static constexpr Fixed Half() noexcept { return FromRaw(OneRaw / 2); }

	// Exact decimal text to fixed point ("-12.375", "3", ".5"), rounded to
	// nearest. Used by config binding; never goes through binary floating point.
	static constexpr std::optional<Fixed> ParseDecimal(std::string_view text) noexcept
	{
		bool negative = false;
		if (!text.empty() && (text.front() == '-' || text.front() == '+'))
		{
			negative = text.front() == '-';
			text.remove_prefix(1);
		}
		std::uint64_t whole = 0;
		std::uint64_t fraction = 0;
		std::uint64_t scale = 1;
		bool digits = false;
		bool point = false;
		for (const char character : text)
		{
			if (character == '.' && !point)
			{
				point = true;
				continue;
			}
			if (character < '0' || character > '9')
				return std::nullopt;
			digits = true;
			const auto digit = static_cast<std::uint64_t>(character - '0');
			if (!point)
			{
				if (whole > (static_cast<std::uint64_t>(detail::kMax) >> FractionBits) / 10u)
					return std::nullopt;
				whole = whole * 10u + digit;
			}
			else if (scale < 1000000000000000000u)
			{
				// Digits beyond 18 decimals are below 2^-16 resolution.
				fraction = fraction * 10u + digit;
				scale *= 10u;
			}
		}
		if (!digits)
			return std::nullopt;
		const UInt128 fractionRaw = ((UInt128{0, fraction} << FractionBits) + UInt128{0, scale / 2u}).DivideBy(scale).quotient;
		const UInt128 magnitude = (UInt128{0, whole} << FractionBits) + fractionRaw;
		return FromRaw(detail::Signed(magnitude, negative));
	}

	// IEEE-754 binary32 bit pattern (as stored in binary game files) to fixed
	// point, rounded to nearest, using integer operations only so that loading
	// is deterministic without touching the FPU. Denormals become zero, NaN is
	// zero, infinities and out-of-range values saturate.
	static constexpr Fixed FromBinary32Bits(std::uint32_t bits) noexcept
	{
		const bool negative = (bits >> 31) != 0;
		const int exponent = static_cast<int>((bits >> 23) & 0xFFu);
		const std::uint64_t fraction = bits & 0x7FFFFFu;
		if (exponent == 0)
			return Fixed{};
		if (exponent == 0xFF)
			return fraction != 0 ? Fixed{} : (negative ? Min() : Max());
		// value = (2^23 + fraction) * 2^(exponent - 150); raw = value * 2^16.
		const std::uint64_t mantissa = (std::uint64_t{1} << 23) | fraction;
		const int shift = exponent - 150 + FractionBits;
		UInt128 magnitude;
		if (shift >= 0)
			magnitude = shift >= 100 ? UInt128{~0ull, ~0ull} : UInt128{0, mantissa} << static_cast<unsigned>(shift);
		else if (shift > -64)
			magnitude = UInt128{0, (mantissa + (std::uint64_t{1} << (-shift - 1))) >> -shift};
		return FromRaw(detail::Signed(magnitude, negative));
	}

	constexpr std::int64_t Raw() const noexcept { return m_raw; }

	// Integer conversions: Floor rounds toward -inf, Round to nearest (halves up).
	constexpr std::int64_t Floor() const noexcept { return m_raw >> FractionBits; }
	constexpr std::int64_t Ceil() const noexcept { return -((-*this).m_raw >> FractionBits); }
	constexpr std::int64_t Round() const noexcept { return (*this + Half()).Floor(); }

	constexpr auto operator<=>(const Fixed &) const noexcept = default;

	constexpr Fixed operator-() const noexcept
	{
		return FromRaw(m_raw == detail::kMin ? detail::kMax : -m_raw);
	}

	constexpr Fixed operator+(Fixed other) const noexcept
	{
		const auto sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(m_raw) + static_cast<std::uint64_t>(other.m_raw));
		if ((m_raw >= 0) == (other.m_raw >= 0) && (sum >= 0) != (m_raw >= 0))
			return m_raw >= 0 ? Max() : Min();
		return FromRaw(sum);
	}

	constexpr Fixed operator-(Fixed other) const noexcept
	{
		const auto difference = static_cast<std::int64_t>(static_cast<std::uint64_t>(m_raw) - static_cast<std::uint64_t>(other.m_raw));
		if ((m_raw >= 0) != (other.m_raw >= 0) && (difference >= 0) != (m_raw >= 0))
			return m_raw >= 0 ? Max() : Min();
		return FromRaw(difference);
	}

	constexpr Fixed operator*(Fixed other) const noexcept
	{
		const bool negative = (m_raw < 0) != (other.m_raw < 0);
		const UInt128 product = UInt128::Multiply(detail::Magnitude(m_raw), detail::Magnitude(other.m_raw));
		const UInt128 rounded = (product + UInt128{0, std::uint64_t{1} << (FractionBits - 1)}) >> FractionBits;
		return FromRaw(detail::Signed(rounded, negative));
	}

	constexpr Fixed operator/(Fixed other) const noexcept
	{
		if (other.m_raw == 0)
			return m_raw == 0 ? Fixed{} : (m_raw > 0 ? Max() : Min());
		const bool negative = (m_raw < 0) != (other.m_raw < 0);
		const std::uint64_t divisor = detail::Magnitude(other.m_raw);
		const UInt128 numerator = (UInt128{0, detail::Magnitude(m_raw)} << FractionBits) + UInt128{0, divisor / 2u};
		return FromRaw(detail::Signed(numerator.DivideBy(divisor).quotient, negative));
	}

	// Scaling by an integer is exact apart from saturation.
	constexpr Fixed operator*(std::int64_t factor) const noexcept
	{
		const bool negative = (m_raw < 0) != (factor < 0);
		return FromRaw(detail::Signed(UInt128::Multiply(detail::Magnitude(m_raw), detail::Magnitude(factor)), negative));
	}

	constexpr Fixed &operator+=(Fixed other) noexcept { return *this = *this + other; }
	constexpr Fixed &operator-=(Fixed other) noexcept { return *this = *this - other; }
	constexpr Fixed &operator*=(Fixed other) noexcept { return *this = *this * other; }
	constexpr Fixed &operator/=(Fixed other) noexcept { return *this = *this / other; }

private:
	std::int64_t m_raw{0};
};

constexpr Fixed Abs(Fixed value) noexcept { return value < Fixed{} ? -value : value; }
constexpr Fixed Min(Fixed a, Fixed b) noexcept { return b < a ? b : a; }
constexpr Fixed Max(Fixed a, Fixed b) noexcept { return a < b ? b : a; }
constexpr Fixed Clamp(Fixed value, Fixed low, Fixed high) noexcept { return Min(Max(value, low), high); }
constexpr Fixed Lerp(Fixed from, Fixed to, Fixed t) noexcept { return from + (to - from) * t; }

// Square root rounded to nearest; negative input returns zero.
constexpr Fixed Sqrt(Fixed value) noexcept
{
	if (value.Raw() <= 0)
		return Fixed{};
	const UInt128 scaled = UInt128{0, static_cast<std::uint64_t>(value.Raw())} << Fixed::FractionBits;
	std::uint64_t root = scaled.SquareRoot();
	// Nearest: round up when scaled - root^2 > root.
	if (scaled - UInt128::Multiply(root, root) > UInt128{0, root})
		++root;
	return Fixed::FromRaw(static_cast<std::int64_t>(root));
}

namespace Literals
{
// 1.5_fx, 30_fx: parsed from the literal's text at compile time, no float.
consteval Fixed operator""_fx(const char *text)
{
	std::string_view view{text};
	const auto value = Fixed::ParseDecimal(view);
	if (!value)
		throw "invalid fixed-point literal";
	return *value;
}
}
}
