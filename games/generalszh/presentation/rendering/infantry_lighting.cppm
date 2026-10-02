export module games.generalszh.presentation.rendering.infantry_lighting;
import std;

// Infantry's own lights (RTS3DScene::updateFixedLightEnvironments and renderOneObject): infantry are lit by copies of
// the scene's global lights with their diffuse (and ambient, which the global lights have none of) scaled by the
// infantry light scale and clamped to -1..1, so they stand out; the scene's ambient is left as it is. The scale is a
// script's (SET_INFANTRY_LIGHTING_OVERRIDE: GlobalData m_scriptOverrideInfantryLightScale; -1: none), else GameData's
// InfantryLight<TimeOfDay>Scale for the map's time of day.
export namespace generalszh::presentation
{
// `timeOfDay`: 0 morning, 1 afternoon, 2 evening, 3 night (TIME_OF_DAY_MORNING - 1 ...).
inline float InfantryLightScale(float scriptOverride, const std::array<float, 4> &scales, std::uint32_t timeOfDay) noexcept
{
	if (scriptOverride != -1.0f)
		return scriptOverride;
	return scales[std::min<std::uint32_t>(timeOfDay, 3)];
}

// A global light's colour as infantry see it.
inline std::array<float, 3> InfantryLightColor(const std::array<float, 3> &color, float scale) noexcept
{
	return {std::clamp(color[0] * scale, -1.0f, 1.0f), std::clamp(color[1] * scale, -1.0f, 1.0f), std::clamp(color[2] * scale, -1.0f, 1.0f)};
}
}
