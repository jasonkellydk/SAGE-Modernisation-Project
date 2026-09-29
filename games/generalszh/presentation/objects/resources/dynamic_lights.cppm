export module games.generalszh.presentation.objects.resources.dynamic_lights;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.effects.light_pulses;

// Presentation's dynamic lights: the light pulses FX lists lit, ageing on
// frame time until they go out, and every light shown this frame (the
// pulses and the police cars' light bars), for the host to hand to the
// renderer.
export namespace generalszh::presentation
{
struct LightPulses
{
	std::vector<LightPulse> live;
};

struct DynamicLights
{
	std::vector<ShownLight> shown;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::LightPulses>
{
	static constexpr std::string_view StableName = "generalszh.presentation.light_pulses";
};

template<>
struct ResourceTraits<generalszh::presentation::DynamicLights>
{
	static constexpr std::string_view StableName = "generalszh.presentation.dynamic_lights";
};
}
