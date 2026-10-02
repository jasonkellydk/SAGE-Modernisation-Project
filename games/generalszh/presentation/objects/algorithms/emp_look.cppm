export module games.generalszh.presentation.objects.algorithms.emp_look;
import std;

export import games.generalszh.presentation.objects.resources.look_catalog;

export namespace generalszh::presentation
{
// An EMP pulse as drawn (EMPUpdate::update, Drawable::colorTint / colorFlash and TintEnvelope::update), `shown` ticks
// into the game (fractional: between the last two ticks). Each tick after it was made its scale closes 5% of the gap
// from StartScale to its rolled target; until its pulse tick it is tinted its saturated start colour, from then on its
// tint fades in equal steps (one per tick, the first on the pulse tick itself) to its saturated end colour, reaching it
// on the tick it dies. Before its first tick nothing is applied yet.
struct EmpLook
{
	float scale{1.0f};
	std::array<float, 3> tint{};
};

inline EmpLook EmpPulseLook(const DefinitionLooks &looks, float targetScale, std::uint64_t fadeTick, std::uint64_t dieTick, double shown)
{
	const double made = static_cast<double>(fadeTick) - static_cast<double>(looks.empFadeTicks);
	const double updates = shown - made;
	EmpLook look;
	look.scale = looks.empStartScale;
	if (updates <= 0.0)
		return look;
	look.scale = targetScale + (looks.empStartScale - targetScale) * static_cast<float>(std::pow(0.95, updates));
	const double span = static_cast<double>(std::max<std::uint64_t>(dieTick > fadeTick ? dieTick - fadeTick : 0, 1));
	const float fade = static_cast<float>(std::clamp((shown - static_cast<double>(fadeTick) + 1.0) / span, 0.0, 1.0));
	for (std::size_t channel = 0; channel < 3; ++channel)
		look.tint[channel] = looks.empStartTint[channel] + (looks.empEndTint[channel] - looks.empStartTint[channel]) * fade;
	return look;
}
}
