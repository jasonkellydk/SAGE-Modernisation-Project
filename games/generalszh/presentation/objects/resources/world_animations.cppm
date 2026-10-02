export module games.generalszh.presentation.objects.resources.world_animations;
import std;

import engine.ecs.system.system;

// Flip-book animations shown in the world (InGameUI::addWorldAnimation): a
// promotion's chevrons, a crate's money: which animation (Animation2D.ini), its
// start point, when it started and expires (presentation clock seconds), how
// fast it rises and whether it fades out over its last second
// (WORLD_ANIM_FADE_ON_EXPIRE). The host draws them.
export namespace generalszh::presentation
{
struct WorldAnimation
{
	std::string animation;
	std::array<float, 3> at{};
	double start{0.0};
	double expire{0.0};
	float risePerSecond{0.0f};
	bool fades{true};

	// Where it is at `clock`.
	std::array<float, 3> PositionAt(double clock) const noexcept
	{
		return {at[0], at[1], at[2] + risePerSecond * static_cast<float>(std::max(clock - start, 0.0))};
	}

	// Its client random draw (Anim2D::Anim2D's randomizeCurrentFrame for an Animation2D with RandomizeStartFrame:
	// Anim2DTemplate::StartImage; the original's client stream is never synchronised, so any uniform draw does): a
	// SplitMix64 mix of when and where it began, the same every frame it shows.
	std::uint32_t Roll() const noexcept
	{
		std::uint64_t value = std::bit_cast<std::uint64_t>(start) ^ (std::uint64_t{std::bit_cast<std::uint32_t>(at[0])} << 32 | std::bit_cast<std::uint32_t>(at[1])) ^
			(std::uint64_t{std::bit_cast<std::uint32_t>(at[2])} * 0x9E3779B97F4A7C15ull);
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
		return static_cast<std::uint32_t>((value ^ (value >> 31)) >> 32);
	}

	// How opaque it is at `clock`: fading over its last second to nothing at its expiry.
	float AlphaAt(double clock) const noexcept
	{
		const double left = expire - clock;
		if (!fades || left >= 1.0)
			return 1.0f;
		return static_cast<float>(std::clamp(left, 0.0, 1.0));
	}
};

struct WorldAnimations
{
	std::vector<WorldAnimation> shown;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::WorldAnimations>
{
	static constexpr std::string_view StableName = "generalszh.presentation.world_animations";
};
}
