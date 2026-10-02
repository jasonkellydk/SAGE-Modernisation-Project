export module games.generalszh.presentation.effects.light_pulses;
import std;

// Zero Hour's dynamic lights (the original's W3DDynamicLight), on frame time:
//   a light pulse (an FX list's LightPulse: W3DDisplay::createLightPulse) is
//   a point light at the spot, lit from 1 unit out to 1 + its radius, that
//   grows over its increase frames (a share of its colour and range each
//   frame), then fades over its decay frames and goes out (one without decay
//   frames stays); too small a one (1 + radius under two pathfinding cells
//   and a unit: 21) is never lit;
//   a police car's light bar (W3DPoliceCarDraw) lights 8 above it, out to 3
//   .. 20, orange, red, then fading to blue and white as its light-bar clip
//   runs.
export namespace generalszh::presentation
{
// A light as shown this frame.
struct ShownLight
{
	std::array<float, 3> at{};
	std::array<float, 3> diffuse{};
	std::array<float, 3> ambient{};
	float nearRange{0.0f}; // full brightness this close
	float farRange{0.0f};  // none past this
};

struct LightPulse
{
	std::array<float, 3> at{};
	std::array<float, 3> color{};
	float inner{1.0f};
	float outer{0.0f};
	float increaseFrames{0.0f};
	float decayFrames{0.0f};
	float age{0.0f}; // frames of 1/30 s since it was lit
};

// createLightPulse's size floor: 2 * PATHFIND_CELL_SIZE_F + 1.
inline constexpr float SmallestPulse = 21.0f;

// The pulse `age` frames on (W3DDynamicLight::On_Frame_Update run that many times); none once it has gone out.
inline std::optional<ShownLight> ShowPulse(const LightPulse &pulse)
{
	float factor = 1.0f;
	if (pulse.increaseFrames > 0.0f && pulse.age <= pulse.increaseFrames)
		factor = std::max(pulse.age, 0.0f) / pulse.increaseFrames;
	else if (pulse.decayFrames > 0.0f)
	{
		const float decayed = pulse.age - std::max(pulse.increaseFrames, 0.0f);
		if (decayed >= pulse.decayFrames)
			return std::nullopt;
		factor = (pulse.decayFrames - decayed) / pulse.decayFrames;
	}
	ShownLight light;
	light.at = pulse.at;
	for (std::size_t channel = 0; channel < 3; ++channel)
		light.diffuse[channel] = light.ambient[channel] = pulse.color[channel] * factor;
	light.nearRange = pulse.inner;
	light.farRange = std::max(pulse.inner, pulse.outer * factor);
	return light;
}

// W3DPoliceCarDraw::doDrawModule's colour for its light-bar clip at `frame`.
inline std::array<float, 3> PoliceLightColor(float frame) noexcept
{
	float red = 0.0f, green = 0.0f, blue = 0.0f;
	if (frame < 3.0f)
	{
		red = 1.0f;
		green = 0.5f;
	}
	else if (frame < 6.0f)
		red = 1.0f;
	else if (frame < 7.0f)
	{
		red = 1.0f;
		green = 0.5f;
	}
	else if (frame < 9.0f)
	{
		red = 0.5f + (9.0f - frame) / 4.0f;
		blue = (frame - 5.0f) / 6.0f;
	}
	else if (frame < 12.0f)
		blue = 1.0f;
	else if (frame <= 14.0f)
	{
		green = (frame - 11.0f) / 3.0f;
		blue = (14.0f - frame) / 2.0f;
		red = (frame - 11.0f) / 3.0f;
	}
	return {red, green, blue};
}

// A police car at `position` (where it is drawn) with its light-bar clip at `frame`.
inline ShownLight PoliceLight(const std::array<float, 3> &position, float frame) noexcept
{
	const auto color = PoliceLightColor(frame);
	return {{position[0], position[1], position[2] + 8.0f}, color, {color[0] / 2.0f, color[1] / 2.0f, color[2] / 2.0f}, 3.0f, 20.0f};
}

// W3DScene's dynamic-light cull: a point light lights an object only when its sphere (its position, out to its far
// range) meets the object's (`sphere`: centre, radius).
inline bool LightReaches(const ShownLight &light, const std::array<float, 4> &sphere) noexcept
{
	const float dx = light.at[0] - sphere[0], dy = light.at[1] - sphere[1], dz = light.at[2] - sphere[2];
	const float reach = light.farRange + sphere[3];
	return dx * dx + dy * dy + dz * dz <= reach * reach;
}
}
