export module games.generalszh.presentation.objects.resources.breeze;
import std;

import engine.ecs.system.system;

// The breeze trees sway in (ScriptEngine's BreezeInfo, its defaults until a
// script sets it): the direction it blows (radians; its vector sin, cos), how
// far trees sway and lean (radians), how long a sway takes (logic frames), how
// much each tree varies (randomness), and a version bumped at each change so
// trees roll their sway again.
export namespace generalszh::presentation
{
struct Breeze
{
	float direction{std::numbers::pi_v<float> / 3.0f};
	float directionX{std::sin(std::numbers::pi_v<float> / 3.0f)};
	float directionY{std::cos(std::numbers::pi_v<float> / 3.0f)};
	float intensity{0.07f * std::numbers::pi_v<float> / 4.0f};
	float lean{0.07f * std::numbers::pi_v<float> / 4.0f};
	float periodFrames{30.0f * 5.0f};
	float randomness{0.2f};
	std::int32_t version{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::Breeze>
{
	static constexpr std::string_view StableName = "generalszh.presentation.breeze";
};
}
