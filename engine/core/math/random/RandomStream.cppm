module;

#include <bit>
#include <cmath>
#include <cstdint>

export module Engine.Core.Math.RandomStream;

export namespace Engine::Math
{
// PCG-XSH-RR with a specified integer transition. Its output stream is the
// same on every platform. Float generation uses the top 23 random bits and a
// bit cast, producing exactly representable values in [0, 1).
class RandomStream final
{
public:
	explicit constexpr RandomStream(std::uint64_t seed) noexcept : state_(seed + 0x853c49e6748fea9bull) {}

	static constexpr std::uint64_t Derive_Seed(std::uint64_t seed, std::uint64_t sequence) noexcept
	{
		std::uint64_t value = seed + 0x9e3779b97f4a7c15ull * (sequence + 1);
		value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
		value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
		return value ^ (value >> 31u);
	}

	constexpr std::uint32_t NextUInt32() noexcept
	{
		const std::uint64_t previous = state_;
		state_ = previous * 6364136223846793005ull + 1442695040888963407ull;
		const std::uint32_t shifted = static_cast<std::uint32_t>(((previous >> 18u) ^ previous) >> 27u);
		const std::uint32_t rotation = static_cast<std::uint32_t>(previous >> 59u);
		return std::rotr(shifted, static_cast<int>(rotation));
	}

	float NextUnitFloat() noexcept
	{
		const std::uint32_t bits = 0x3f800000u | (NextUInt32() >> 9u);
		return std::bit_cast<float>(bits) - 1.0f;
	}

	float NextFloat(float minimum, float maximum) noexcept
	{
		if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) return minimum;
		if (minimum == maximum) return minimum;
		const double sample = static_cast<double>(NextUnitFloat());
		const double scaled = static_cast<double>(minimum)
			+ (static_cast<double>(maximum) - static_cast<double>(minimum)) * sample;
		const float result = static_cast<float>(scaled);
		if (result < minimum) return minimum;
		return result < maximum ? result : std::nextafter(maximum, minimum);
	}

private:
	std::uint64_t state_;
};
}
