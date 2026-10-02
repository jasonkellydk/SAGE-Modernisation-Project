export module games.generalszh.presentation.objects.algorithms.tree_breeze_sway;
import std;

export import games.generalszh.presentation.objects.resources.tree_breeze;
export import games.generalszh.presentation.objects.resources.breeze;

// W3DTreeBuffer's sway, for the trees drawn by W3DTreeDraw:
//   RollTreeBreeze (updateSway): at a new breeze, the offsets round one sway
//   (entry i at lean + intensity * cos(i * 2pi / 101): x, y the breeze
//   direction times its sine, z its cosine less one), and each sway type a
//   step of 100 / period entries a frame and a share of the breeze, both
//   times a random 1 +- randomness / 2, starting at its first entry;
//   StepTreeBreeze (prepareFrame): each type steps by its step times the
//   logic frames this frame, wrapping past entry 99 back by 99, and shows
//   the blend of the two entries it lies between times its share;
//   TreeSwayType (addTree): each tree one of the ten types at random;
//   ShearTree: the tree bent as the tree shader bends it (each point moved
//   by the sway times its height above the tree's base).
export namespace generalszh::presentation
{
namespace tree_breeze_sway_detail
{
// A value in [0, 1) from a key (the client's random, as presentation rolls it: hashed, so frames replay alike).
inline double Unit(std::uint64_t value) noexcept
{
	value += 0x9E3779B97F4A7C15ull;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
	value ^= value >> 31;
	return static_cast<double>(value >> 11) / 9007199254740992.0;
}
}

// `random(type, which)`: a value in [0, 1) (the client's random).
template<typename Random>
void RollTreeBreeze(TreeBreeze &trees, const Breeze &breeze, Random &&random)
{
	for (std::size_t i = 0; i < TreeSwayEntries; ++i)
	{
		const float factor = std::cos(static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / (static_cast<float>(TreeSwayEntries) + 1.0f));
		const float angle = breeze.lean + breeze.intensity * factor;
		const float s = std::sin(angle), c = std::cos(angle);
		trees.offsets[i] = {breeze.directionX * s, breeze.directionY * s, c - 1.0f};
	}
	const float delta = breeze.randomness * 0.5f;
	const auto between = [&](std::size_t type, std::uint32_t which) {
		return (1.0f - delta) + 2.0f * delta * static_cast<float>(random(type, which));
	};
	for (std::size_t type = 0; type < TreeSwayTypes; ++type)
	{
		trees.step[type] = static_cast<float>(TreeSwayEntries) / breeze.periodFrames;
		trees.step[type] *= between(type, 0);
		if (trees.step[type] < 0.0f)
			trees.step[type] = 0.0f;
		trees.offset[type] = 0.0f;
		trees.factor[type] = between(type, 1);
	}
	trees.version = breeze.version;
}

inline void StepTreeBreeze(TreeBreeze &trees, float logicFrames)
{
	for (std::size_t type = 0; type < TreeSwayTypes; ++type)
	{
		std::array<float, 3> sway{};
		trees.offset[type] += trees.step[type] * logicFrames;
		if (trees.offset[type] > static_cast<float>(TreeSwayEntries - 1))
			trees.offset[type] -= static_cast<float>(TreeSwayEntries - 1);
		const int low = static_cast<int>(std::floor(trees.offset[type]));
		if (low >= 0 && low + 1 < static_cast<int>(TreeSwayEntries))
		{
			const float f2 = trees.offset[type] - static_cast<float>(low);
			const float f1 = 1.0f - f2;
			const auto &from = trees.offsets[static_cast<std::size_t>(low)];
			const auto &to = trees.offsets[static_cast<std::size_t>(low) + 1];
			for (std::size_t axis = 0; axis < 3; ++axis)
				sway[axis] = (f1 * from[axis] + f2 * to[axis]) * trees.factor[type];
		}
		trees.current[type] = sway;
	}
}

// 0..9 (the original's 1..10, less one) from a random value in [0, 1).
inline std::size_t TreeSwayType(double random) noexcept
{
	return std::min(static_cast<std::size_t>(random * static_cast<double>(TreeSwayTypes)), TreeSwayTypes - 1);
}

// `world` (row-major 4x4) bent by `sway` about the height `base`: each point p to p + sway * (p.z - base).
inline void ShearTree(std::array<float, 16> &world, const std::array<float, 3> &sway, float base) noexcept
{
	const std::array<float, 4> height{world[8], world[9], world[10], world[11] - base};
	for (std::size_t axis = 0; axis < 3; ++axis)
		for (std::size_t column = 0; column < 4; ++column)
			world[axis * 4 + column] += sway[axis] * height[column];
}
}
