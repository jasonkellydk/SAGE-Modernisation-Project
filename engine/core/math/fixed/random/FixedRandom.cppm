export module Engine.Core.Math.FixedRandom;
import std;

export import Engine.Core.Math.RandomStream;
export import Engine.Core.Math.Fixed;

// Simulation sampling on the shared deterministic RandomStream. Generators
// are plain values owned and passed explicitly; parallel work never shares
// one but derives an independent stream per key (see Stream).
export namespace Engine::Math
{
// Independent stream for a (seed, keys...) tuple, e.g. (match seed, tick,
// system id, entity). The same keys always give the same sequence, no matter
// which thread or in what order streams are created.
constexpr RandomStream Stream(std::uint64_t seed, std::initializer_list<std::uint64_t> keys) noexcept
{
	std::uint64_t derived = RandomStream::Derive_Seed(seed, 0);
	for (const std::uint64_t key : keys)
		derived = RandomStream::Derive_Seed(derived, key);
	return RandomStream{derived};
}

constexpr std::uint64_t NextUInt64(RandomStream &random) noexcept
{
	const std::uint64_t high = random.NextUInt32();
	return (high << 32) | random.NextUInt32();
}

// Uniform integer in [low, high], unbiased (Lemire's method with rejection).
constexpr std::int64_t UniformInt(RandomStream &random, std::int64_t low, std::int64_t high) noexcept
{
	if (high <= low)
		return low;
	const std::uint64_t span = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low);
	if (span == ~std::uint64_t{0})
		return static_cast<std::int64_t>(NextUInt64(random));
	const std::uint64_t range = span + 1u;
	UInt128 product = UInt128::Multiply(NextUInt64(random), range);
	if (product.lo < range)
	{
		const std::uint64_t threshold = (0u - range) % range;
		while (product.lo < threshold)
			product = UInt128::Multiply(NextUInt64(random), range);
	}
	return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) + product.hi);
}

// Uniform fixed-point value in [low, high] at full 2^-16 resolution.
constexpr Fixed UniformFixed(RandomStream &random, Fixed low, Fixed high) noexcept
{
	return Fixed::FromRaw(UniformInt(random, low.Raw(), high.Raw()));
}

// True with the given probability (clamped to [0, 1]).
constexpr bool Chance(RandomStream &random, Fixed probability) noexcept
{
	return UniformInt(random, 0, Fixed::OneRaw - 1) < probability.Raw();
}
}
