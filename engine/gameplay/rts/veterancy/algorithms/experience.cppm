export module engine.gameplay.rts.veterancy.algorithms.experience;
import std;

export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.veterancy.definitions.veterancy;

// ExperienceTracker's arithmetic.
export namespace engine::gameplay
{
// The highest level whose ExperienceRequired the points reach (levels past the first need their points).
inline std::uint8_t LevelFor(std::int32_t points, const VeterancyDefinition &definition) noexcept
{
	std::uint8_t level = 0;
	while (level + 1u < VeterancyLevelCount && points >= definition.required[level + 1u])
		++level;
	return level;
}

// ExperienceTracker::addExperiencePoints without a sink: nothing for one that cannot train; else the gain,
// scaled by its scalar when it may be (an int times a float, truncated), levels recomputed. Returns the level it had.
inline std::uint8_t AddExperiencePoints(Experience &experience, const VeterancyDefinition &definition, std::int32_t gain, bool canScale) noexcept
{
	const std::uint8_t old = experience.level;
	if (!experience.trainable)
		return old;
	std::int32_t amount = gain;
	if (canScale)
		amount = static_cast<std::int32_t>((Engine::Math::Fixed::FromInt(gain) * experience.scalar).Raw() / Engine::Math::Fixed::One().Raw());
	experience.points += amount;
	experience.level = LevelFor(experience.points, definition);
	return old;
}

// ExperienceTracker::setVeterancyLevel / setMinVeterancyLevel: the level and the minimum points for it.
inline void SetVeterancyLevel(Experience &experience, const VeterancyDefinition &definition, std::uint8_t level) noexcept
{
	experience.level = level;
	experience.points = definition.required[level];
}

// What the scalar makes of a gain passed to a sink (experienceGain * m_experienceScalar, truncated).
inline std::int32_t ScaledForSink(const Experience &experience, std::int32_t gain) noexcept
{
	return static_cast<std::int32_t>((Engine::Math::Fixed::FromInt(gain) * experience.scalar).Raw() / Engine::Math::Fixed::One().Raw());
}
}
