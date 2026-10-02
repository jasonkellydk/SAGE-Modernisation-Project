export module games.generalszh.presentation.hud.algorithms.screen_fade_steps;
import std;

export import games.generalszh.presentation.hud.resources.screen_fade;

// ScriptEngine::updateFades, setFade and the fade a map starts on (ScriptEngine::newMap).
export namespace generalszh::presentation
{
// updateFades: a frame on: rising, the value goes from the minimum toward the maximum (frame / rising frames); holding,
// it is the maximum; falling, back toward the minimum (frames into it / (falling frames + 1)); after that the fade is over.
inline void StepFade(ScreenFade &fade)
{
	++fade.frame;
	std::int64_t at = fade.frame;
	if (at <= fade.rising)
	{
		const float factor = static_cast<float>(fade.frame) / static_cast<float>(fade.rising);
		fade.value = fade.minimum + factor * (fade.maximum - fade.minimum);
		return;
	}
	at -= fade.rising;
	if (at <= fade.holding)
	{
		fade.value = fade.maximum;
		return;
	}
	at -= fade.holding;
	if (at <= fade.falling)
	{
		std::int64_t divisor = fade.falling + 1;
		if (divisor == 0)
			divisor = 1;
		const float factor = static_cast<float>(at) / static_cast<float>(divisor);
		fade.value = fade.maximum + factor * (fade.minimum - fade.maximum);
		return;
	}
	fade.kind = FadeKind::None;
}

// setFade: from the minimum, frame 0 (with nothing rising, at once a frame on). `tick`: the logic frame the script ran
// on (the frame after is the first to step it).
inline void SetFade(ScreenFade &fade, FadeKind kind, float minimum, float maximum, std::int64_t rising, std::int64_t holding, std::int64_t falling,
	std::uint64_t tick)
{
	fade = ScreenFade{kind, 0, minimum, minimum, maximum, rising, holding, falling, tick};
	if (kind == FadeKind::None)
		return;
	if (rising == 0)
		StepFade(fade);
}

// newMap: a fade in from black (multiply from 0 back to 1 over FRAMES_TO_FADE_IN_AT_START, 33, frames).
inline ScreenFade StartFade() { return ScreenFade{FadeKind::Multiply, 0, 0.0f, 1.0f, 0.0f, 0, 0, 33, 0}; }

// Once a logic frame (ScriptEngine::update, ahead of its scripts): a fade on steps, unless it was set this frame.
inline void StepFadeOnTick(ScreenFade &fade, std::uint64_t tick)
{
	if (fade.kind != FadeKind::None && tick > fade.setTick)
		StepFade(fade);
}
}
