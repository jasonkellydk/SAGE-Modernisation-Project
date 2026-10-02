export module games.generalszh.presentation.objects.algorithms.model_state_changes;
import std;

export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// How a drawn object's model state changes, as W3DModelDraw does it (on the
// ShownLook side table, with presentation's own per-object rolls), for any of its
// draw modules (`Draw`: its model states, the look of each, how many animations
// each picks among, whether it is a police car's light bar):
// - setModelState: asked for a new state, it keeps what it shows when that
//   is the state asked for (or already waiting); it lets the state showing
//   finish its animation first when the new one waits for it
//   (WaitForStateToFinishIfPossible = the showing state's TransitionKey);
//   it plays the transition between their keys first when there is one
//   (TransitionState); else it shows the new state at once.
// - doDrawModule, as the animation showing finishes (a clip played once,
//   having reached its end): the waiting state shows; an idle animation
//   gives way to another of its state's; RESTART_ANIM_WHEN_COMPLETE starts
//   it over.
// - adjustAnimation, as a state shows: one of its animations (another than
//   before when it is the same state again), a speed factor from its
//   AnimationSpeedFactorRange, and a start frame: RANDOMSTART, START_FRAME_FIRST,
//   START_FRAME_LAST, else, taken from a state sharing a MAINTAIN_FRAME flag,
//   REAL_TO_INT(fraction * frames - 1) of where that one's animation was.
export namespace generalszh::presentation
{
// The frame (in the clip's own frames, as the renderer plays it) the look showing is at; below zero when its
// clip is not known or it has none.
inline double ShownClipFrame(const ShownLook &shown, const LookClip &clip, content::ModelAnimationMode mode, double clock) noexcept
{
	if (clip.frames <= 0.0f || clip.rate <= 0.0f)
		return -1.0;
	const double count = clip.frames, last = count - 1.0;
	const double frames = (clock - shown.since) * shown.speed * clip.rate + static_cast<double>(shown.start) * last;
	switch (mode)
	{
	case content::ModelAnimationMode::Manual:
		return static_cast<double>(shown.start) * last; // held on its start frame (ANIM_MODE_MANUAL)
	case content::ModelAnimationMode::Loop:
		return std::fmod(frames, count);
	case content::ModelAnimationMode::LoopBackwards:
		return last - std::fmod(frames, count);
	case content::ModelAnimationMode::LoopPingPong: {
		if (last <= 0.0)
			return 0.0;
		const double phase = std::fmod(frames, 2.0 * last);
		return phase <= last ? phase : 2.0 * last - phase;
	}
	case content::ModelAnimationMode::Once:
		return std::min(frames, last);
	case content::ModelAnimationMode::OnceBackwards:
		return std::max(last - frames, 0.0);
	}
	return 0.0;
}

// Is_Animation_Complete: a clip played once (forwards or backwards) that reached its end.
inline bool ShownClipComplete(const ShownLook &shown, const LookClip &clip, content::ModelAnimationMode mode, double clock) noexcept
{
	if (mode != content::ModelAnimationMode::Once && mode != content::ModelAnimationMode::OnceBackwards)
		return false;
	if (clip.frames <= 0.0f || clip.rate <= 0.0f)
		return false;
	const double last = static_cast<double>(clip.frames) - 1.0;
	return (clock - shown.since) * shown.speed * clip.rate + static_cast<double>(shown.start) * last >= last;
}

// What it rolls as a state shows (presentation's own randomness: the object and the frame).
struct ModelStateRolls
{
	std::uint64_t key{0};
	std::uint64_t frame{0};

	// splitmix64 of the object and the frame (every bit of both matters).
	static std::uint32_t Mix(std::uint64_t value) noexcept
	{
		value += 0x9E3779B97F4A7C15ull;
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
		return static_cast<std::uint32_t>((value ^ (value >> 31)) >> 32);
	}
	std::uint32_t Pick() const noexcept { return Mix(key ^ (frame * 0xD6E8FEB86659FD93ull)); }
	std::uint32_t Roll() const noexcept { return Mix(~key ^ (frame * 0xA0761D6478BD642Full)); }
};

namespace model_state_detail
{
template <class Draw>
inline content::ModelAnimationMode ModeOf(const Draw &looks, std::uint32_t state) noexcept
{
	return state < looks.states.states.size() ? looks.states.states[state].animationMode : content::ModelAnimationMode::Once;
}
}

// Whether the look showing has finished its animation.
template <class Draw>
inline bool ShownStateComplete(const ShownLook &shown, const Draw &looks, const LookClips &clips, double clock) noexcept
{
	return ShownClipComplete(shown, clips.At(shown.look), model_state_detail::ModeOf(looks, shown.shown), clock);
}

// adjustAnimation: `state` shows from `clock`, after what showed until now (`hadShown`: something did).
template <class Draw>
inline void ShowModelState(ShownLook &shown, const Draw &looks, const LookClips &clips, std::uint32_t state, double clock,
	ModelStateRolls rolls, bool hadShown = true)
{
	using Start = content::ModelState::StartFrame;
	const auto &states = looks.states.states;
	if (state >= states.size() || state >= looks.stateLooks.size())
		return;
	const content::ModelState &next = states[state];
	const std::uint32_t base = looks.stateLooks[state];
	const std::uint32_t variants = state < looks.stateVariants.size() ? std::max(looks.stateVariants[state], 1u) : 1u;
	const bool same = hadShown && shown.shown == state;
	// getCurrentAnimFraction: where the showing animation is, when its state keeps frames across states.
	double previousFraction = -1.0;
	std::uint8_t previousMaintain = 0;
	if (hadShown && shown.shown < states.size() && states[shown.shown].maintainFrame != 0)
	{
		previousMaintain = states[shown.shown].maintainFrame;
		const LookClip clip = clips.At(shown.look);
		const double frame = ShownClipFrame(shown, clip, states[shown.shown].animationMode, clock);
		if (frame >= 0.0 && clip.frames > 1.0f)
			previousFraction = frame < 0.0 ? 0.0 : frame >= clip.frames ? 1.0 : frame / (static_cast<double>(clip.frames) - 1.0);
	}
	// One of its animations: another than the one showing when it is the same state again.
	std::uint32_t variant = 0;
	if (variants > 1)
	{
		const std::uint32_t pick = rolls.Pick();
		if (same && shown.look >= base && shown.look < base + variants)
			variant = (shown.look - base + 1 + pick % (variants - 1)) % variants;
		else
			variant = pick % variants;
	}
	const std::uint32_t look = base + variant;
	const std::uint32_t roll = rolls.Roll();
	const float low = Engine::Math::ToFloat(next.speedMin), high = Engine::Math::ToFloat(next.speedMax);
	const bool backwards = next.animationMode == content::ModelAnimationMode::OnceBackwards || next.animationMode == content::ModelAnimationMode::LoopBackwards;
	// The start frame as a share of the clip counted forwards (backwards clips naturally start at their last).
	float share = backwards ? 1.0f : 0.0f;
	if (next.startFrame == Start::Random)
		share = static_cast<float>((roll >> 10) % 1000u) / 999.0f;
	else if (next.startFrame == Start::First)
		share = 0.0f;
	else if (next.startFrame == Start::Last)
		share = 1.0f;
	else if (next.maintainFrame != 0 && hadShown && !same && (next.maintainFrame & previousMaintain) != 0 && previousFraction >= 0.0)
	{
		const LookClip clip = clips.At(look);
		if (clip.frames > 1.0f)
		{
			const int frame = static_cast<int>(previousFraction * static_cast<double>(static_cast<int>(clip.frames)) - 1.0);
			share = static_cast<float>(std::max(frame, 0)) / (clip.frames - 1.0f);
		}
		else
			share = static_cast<float>(previousFraction);
	}
	shown.since = clock;
	shown.look = look;
	shown.speed = low + (high - low) * static_cast<float>(roll % 1000u) / 999.0f;
	// The renderer plays backwards clips from their last frame on: its start is how far that is done.
	shown.start = backwards ? 1.0f - share : share;
	shown.held = -1.0;
	shown.shown = state;
	// W3DPoliceCarDraw::doDrawModule: its light bar's clip steps a quarter frame each 1/30 s (7.5 frames a second),
	// from a random frame between 0 and 10.
	if (looks.policeLights)
		if (const LookClip clip = clips.At(look); clip.rate > 0.0f && clip.frames > 1.0f)
		{
			shown.speed = 7.5f / clip.rate;
			shown.start = std::min(10.0f * static_cast<float>((roll >> 20) % 1001u) / 1000.0f, clip.frames - 1.0f) / (clip.frames - 1.0f);
		}
}

// W3DPoliceCarDraw's light bar on its clip's own rate: its quarter frame each 1/30 s once its clip is known (a state
// shown before its model loaded took the state's speed; the model draws only once loaded, so nothing jumps), from a
// random frame between 0 and 10. Returns whether it settled it now.
template <class Draw>
inline bool SettlePoliceLights(ShownLook &shown, const Draw &looks, const LookClips &clips, double clock, ModelStateRolls rolls)
{
	if (!looks.policeLights)
		return false;
	const LookClip clip = clips.At(shown.look);
	if (clip.rate <= 0.0f || clip.frames <= 1.0f || shown.speed == 7.5f / clip.rate)
		return false;
	const std::uint32_t roll = rolls.Roll();
	shown.since = clock;
	shown.speed = 7.5f / clip.rate;
	shown.start = std::min(10.0f * static_cast<float>((roll >> 20) % 1001u) / 1000.0f, clip.frames - 1.0f) / (clip.frames - 1.0f);
	return true;
}

// setModelState: its conditions ask for `state`.
template <class Draw>
inline void RequestModelState(ShownLook &shown, const Draw &looks, const LookClips &clips, std::uint32_t state, double clock,
	ModelStateRolls rolls)
{
	const auto &states = looks.states.states;
	shown.state = state;
	if (state >= states.size() || shown.shown >= states.size())
	{
		ShowModelState(shown, looks, clips, state, clock, rolls, shown.shown < states.size());
		shown.pending = ShownLook::NoState;
		return;
	}
	if ((shown.shown == state && shown.pending == ShownLook::NoState) || shown.pending == state)
		return;
	const content::ModelState &current = states[shown.shown], &next = states[state];
	if (state != shown.shown && !next.waitKey.empty() && next.waitKey == current.transitionKey && !ShownStateComplete(shown, looks, clips, clock))
	{
		shown.pending = state;
		return;
	}
	if (state != shown.shown)
		if (const auto transition = content::FindTransition(looks.states, shown.shown, state))
		{
			ShowModelState(shown, looks, clips, static_cast<std::uint32_t>(*transition), clock, rolls);
			shown.pending = state;
			return;
		}
	ShowModelState(shown, looks, clips, state, clock, rolls);
	shown.pending = ShownLook::NoState;
}

// Set_Animation_Frame_Rate_Multiplier on the clip showing: its speed changes from here on (where it is stays).
inline void SetShownSpeed(ShownLook &shown, const LookClip &clip, content::ModelAnimationMode mode, double clock, float speed) noexcept
{
	if (clip.frames > 1.0f && clip.rate > 0.0f && shown.held < 0.0)
	{
		const double count = clip.frames, last = count - 1.0;
		double frames = (clock - shown.since) * shown.speed * clip.rate + static_cast<double>(shown.start) * last;
		switch (mode)
		{
		case content::ModelAnimationMode::Loop:
		case content::ModelAnimationMode::LoopBackwards:
			frames = std::fmod(frames, count);
			break;
		case content::ModelAnimationMode::LoopPingPong:
			frames = std::fmod(frames, 2.0 * last);
			break;
		case content::ModelAnimationMode::Once:
		case content::ModelAnimationMode::OnceBackwards:
			frames = std::min(frames, last);
			break;
		case content::ModelAnimationMode::Manual:
			break;
		}
		shown.since = clock;
		shown.start = static_cast<float>(frames / last);
	}
	shown.speed = speed;
}

// adjustAnimSpeedToMovementSpeed: an animation covering a distance (distanceCovered), moving `perTick` a logic
// frame, plays its whole clip in the time that takes (dist / speed frames of 1/30 s).
template <class Draw>
inline void MatchMovementSpeed(ShownLook &shown, const Draw &looks, const LookClips &clips, float perTick, double clock) noexcept
{
	constexpr float LogicFramesPerSecond = 30.0f;
	const auto &states = looks.states.states;
	if (perTick <= 0.0f || shown.shown >= states.size() || shown.shown >= looks.stateLooks.size())
		return;
	const content::ModelState &state = states[shown.shown];
	const std::uint32_t variant = shown.look - looks.stateLooks[shown.shown];
	if (variant >= state.distances.size() || state.distances[variant] <= Engine::Math::Fixed{})
		return;
	const LookClip clip = clips.At(shown.look);
	if (clip.frames <= 0.0f || clip.rate <= 0.0f)
		return;
	// natural / desired: (frames / rate) s over (distance / perTick) logic frames.
	const float distance = Engine::Math::ToFloat(state.distances[variant]);
	const float multiplier = clip.frames * perTick * LogicFramesPerSecond / (clip.rate * distance);
	if (std::fabs(multiplier - shown.speed) > 1e-4f)
		SetShownSpeed(shown, clip, state.animationMode, clock, multiplier);
}

// W3DModelDraw::setAnimationLoopDuration: the animation showing plays its whole clip in `frames` logic frames
// (setCurAnimDurationInMsec: its natural length over ceil(frames * 1000 / 30) ms as its speed), until the next state
// (setModelState gives a new animation its own speed).
template <class Draw>
inline void StretchToFrames(ShownLook &shown, const Draw &looks, const LookClips &clips, std::uint64_t frames, double clock) noexcept
{
	if (frames == 0)
		return;
	const LookClip clip = clips.At(shown.look);
	if (clip.frames <= 0.0f || clip.rate <= 0.0f)
		return;
	// In the original's floats (MSEC_PER_LOGICFRAME_REAL: 15 frames ceil to 500 ms, not 501).
	const float natural = clip.frames * 1000.0f / clip.rate;
	const float desired = std::ceil(static_cast<float>(frames) * (1000.0f / 30.0f));
	const auto &states = looks.states.states;
	const content::ModelAnimationMode mode = shown.shown < states.size() ? states[shown.shown].animationMode : content::ModelAnimationMode::Loop;
	SetShownSpeed(shown, clip, mode, clock, natural / desired);
}

// Once a frame: the state its conditions pick now, then what finishing its animation does (doDrawModule).
template <class Draw>
inline void StepModelState(ShownLook &shown, const Draw &looks, const LookClips &clips, std::uint32_t state, double clock,
	ModelStateRolls rolls)
{
	if (state != shown.state)
		RequestModelState(shown, looks, clips, state, clock, rolls);
	const auto &states = looks.states.states;
	if (shown.shown >= states.size() || !ShownStateComplete(shown, looks, clips, clock))
		return;
	if (shown.pending != ShownLook::NoState)
	{
		const std::uint32_t pending = shown.pending;
		shown.pending = ShownLook::NoState;
		ShowModelState(shown, looks, clips, pending, clock, rolls);
		rolls.frame += 1;
	}
	if (states[shown.shown].idleAnimation || states[shown.shown].restartWhenComplete)
		ShowModelState(shown, looks, clips, shown.shown, clock, rolls);
}
}
