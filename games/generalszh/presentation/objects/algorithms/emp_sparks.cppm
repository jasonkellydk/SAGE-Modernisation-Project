export module games.generalszh.presentation.objects.algorithms.emp_sparks;
import std;

export import games.generalszh.presentation.objects.resources.look_catalog;

// An EMP victim's sparks (EMPUpdate::doDisableAttack): how many emitters it gets and where each sits in its frame.
export namespace generalszh::presentation
{
// GeometryInfo::getFootprintArea: a circle of its radius (sphere, cylinder) or its box.
inline float FootprintArea(const DefinitionLooks &victim)
{
	return victim.boxFootprint ? 4.0f * victim.majorRadius * victim.minorRadius : std::numbers::pi_v<float> * victim.majorRadius * victim.majorRadius;
}

// At least 15; else SparksPerCubicFoot of its footprint times its height (at most 10) above it, rounded up.
inline std::uint32_t EmpSparkCount(float sparksPerCubicFoot, const DefinitionLooks &victim)
{
	const float volume = FootprintArea(victim) * std::min(victim.constructionHeight, 10.0f);
	return std::max<std::uint32_t>(15u, static_cast<std::uint32_t>(std::max(0.0f, std::ceil(sparksPerCubicFoot * volume))));
}

// Somewhere over its footprint (GeometryInfo::makeRandomOffsetWithinFootprint: a point in its circle, tried again until
// inside, or in its box), at a whole height from 3 to its height (GameClientRandomValue(3, height): its height when no
// more than 3), kept inside a rectangular dome: one farther from its centre than its height has its height scaled by
// the height over that distance.
inline std::array<float, 3> EmpSparkOffset(const DefinitionLooks &victim, std::mt19937 &random)
{
	const auto uniform = [&](float low, float high) { return high <= low ? low : std::uniform_real_distribution<float>(low, high)(random); };
	const float major = victim.majorRadius, minor = victim.minorRadius;
	std::array<float, 3> offset{};
	if (victim.boxFootprint)
		offset = {uniform(-major, major), uniform(-minor, minor), 0.0f};
	else
		do
			offset = {uniform(-major, major), uniform(-major, major), 0.0f};
		while (offset[0] * offset[0] + offset[1] * offset[1] > major * major);
	const float height = victim.constructionHeight;
	const int top = static_cast<int>(height);
	offset[2] = static_cast<float>(top <= 3 ? top : std::uniform_int_distribution<int>(3, top)(random));
	const float length = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
	if (length > height && length > 0.0f)
		offset[2] = offset[2] / length * height;
	return offset;
}
}
