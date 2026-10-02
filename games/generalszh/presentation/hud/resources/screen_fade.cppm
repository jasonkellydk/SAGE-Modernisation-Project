export module games.generalszh.presentation.hud.resources.screen_fade;
import std;

import engine.ecs.system.system;

// The screen fade (the original's ScriptEngine m_fade and its values, CAMERA_FADE_*): how the whole view is blended
// (added to, taken from, saturated or multiplied by the fade value), the value from `minimum` to `maximum` over the
// rising frames, held, and back over the falling ones; a map starts on a fade in from black (multiply, from 0 to 1 over
// 33 frames). Stepped once a logic frame, before that frame's scripts could set a new one (`setTick`: the tick one set
// then is first seen, which does not step it). Presentation state: not saved.
export namespace generalszh::presentation
{
enum class FadeKind : std::uint8_t
{
	None,
	Add,
	Subtract,
	Saturate,
	Multiply,
};

struct ScreenFade
{
	FadeKind kind{FadeKind::None};
	std::int64_t frame{0};
	float value{0.0f};
	float minimum{0.0f};
	float maximum{0.0f};
	std::int64_t rising{0};
	std::int64_t holding{0};
	std::int64_t falling{0};
	std::uint64_t setTick{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::ScreenFade>
{
	static constexpr std::string_view StableName = "generalszh.presentation.screen_fade";
};
}
