export module engine.gameplay.common.health.algorithms.max_health;
import std;

export import engine.gameplay.common.health.components.health;

// ActiveBody::setMaxHealth: a new maximum, and what happens to the current
// hit points (MaxHealthChangeType), then clipped to the new maximum and to
// zero (internalChangeHealth).
export namespace engine::gameplay
{
enum class MaxHealthChange : std::uint8_t
{
	SameCurrent,    // SAME_CURRENTHEALTH
	PreserveRatio,  // PRESERVE_RATIO: 400/500 + 100 becomes 480/600
	AddCurrentToo,  // ADD_CURRENT_HEALTH_TOO: 400/500 + 100 becomes 500/600
	FullyHeal,      // FULLY_HEAL
};

inline void SetMaxHealth(Health &health, Engine::Math::Fixed maximum, MaxHealthChange change) noexcept
{
	using Engine::Math::Fixed;
	const Fixed previous = health.maximum;
	health.maximum = maximum;
	const auto clip = [&](Fixed value) { return value > maximum ? maximum : value < Fixed{} ? Fixed{} : value; };
	switch (change)
	{
	case MaxHealthChange::PreserveRatio:
		if (previous > Fixed{})
			health.current = clip(maximum * (health.current / previous));
		break;
	case MaxHealthChange::AddCurrentToo:
		health.current = clip(health.current + (maximum - previous));
		break;
	case MaxHealthChange::FullyHeal:
		health.current = maximum;
		break;
	case MaxHealthChange::SameCurrent:
		break;
	}
	if (health.current > maximum)
		health.current = maximum;
}
}
