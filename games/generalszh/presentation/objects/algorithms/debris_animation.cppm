export module games.generalszh.presentation.objects.algorithms.debris_animation;
import std;

export import engine.gameplay.common.appearance.components.debris_look;
export import games.generalszh.content.objects.model_draw;
export import games.generalszh.presentation.objects.resources.look_catalog;

// A thrown debris piece's animations, as the original's W3DDebrisDraw::doDrawModule steps them each drawn frame: it
// starts in its initial state; once on the ground (after more than three frames) it goes to its final state at once,
// else it moves on from its initial state when that animation is done (its flying one loops, so never finishes). An
// animation is (re)started when its state asks for one it is not showing, or the state changed; starting the final
// one plays the piece's landing effect. A state without an animation leaves the last one showing.
export namespace generalszh::presentation
{
// Where a debris piece's animations are, per piece (a side table on the piece).
struct DebrisMotion
{
	static constexpr std::uint8_t Initial = 0, Flying = 1, Final = 2;
	std::uint8_t state{Initial};
	std::uint8_t mode{0};       // the animation showing's mode (content::ModelAnimationMode)
	std::uint32_t animation{0}; // the animation showing (a model-name id, 0 none yet)
	float frames{0.0f};         // frames drawn, in thirtieths of a second
	double since{0.0};          // presentation clock seconds its animation started
};

// How each state plays its animation: the initial once, flying looping, the final once, or held at its first frame
// ("STOP": the flying animation, ANIM_MODE_MANUAL).
constexpr content::ModelAnimationMode DebrisAnimationMode(std::size_t state, bool finalStop) noexcept
{
	if (state == DebrisMotion::Flying)
		return content::ModelAnimationMode::Loop;
	if (state == DebrisMotion::Final && finalStop)
		return content::ModelAnimationMode::Manual;
	return content::ModelAnimationMode::Once;
}

// W3DDebrisDraw's isAnimationComplete (Is_Animation_Complete): an animation played once that reached its last frame;
// never one looping, held, or not loaded yet.
inline bool DebrisAnimationComplete(const DebrisMotion &motion, const LookClip &clip, double clock) noexcept
{
	if (motion.animation == 0 || motion.mode != static_cast<std::uint8_t>(content::ModelAnimationMode::Once))
		return false;
	if (clip.frames <= 0.0f || clip.rate <= 0.0f)
		return false;
	return (clock - motion.since) * clip.rate >= static_cast<double>(clip.frames) - 1.0;
}

// One drawn frame of `seconds`: whether its landing effect plays now.
inline bool StepDebris(DebrisMotion &motion, const engine::gameplay::DebrisLook &look, bool aboveTerrain, bool complete, double clock,
	float seconds) noexcept
{
	constexpr float MinFinalFrames = 3.0f;
	const std::uint8_t old = motion.state;
	if (motion.state != DebrisMotion::Final && !aboveTerrain && motion.frames > MinFinalFrames)
		motion.state = DebrisMotion::Final;
	else if (motion.state < DebrisMotion::Final && complete)
		++motion.state;
	bool landed = false;
	const std::uint32_t animation = look.animations[motion.state];
	if (animation != 0 && (animation != motion.animation || old != motion.state))
	{
		motion.animation = animation;
		motion.mode = static_cast<std::uint8_t>(DebrisAnimationMode(motion.state, look.finalStop != 0));
		motion.since = clock;
		landed = motion.state == DebrisMotion::Final;
	}
	motion.frames += seconds * 30.0f;
	return landed;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::DebrisMotion>
{
	static constexpr std::string_view StableName = "generalszh.presentation.debris_motion";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
