export module games.generalszh.presentation.animation.tread_roll;
import std;

// A tank's treads rolling (the original's W3DTankDraw), on game time: while
// it pivots (turns slower than `pivot` of its top speed) the two sides roll
// opposite ways, left forward on a left turn; while it drives on at `drive`
// of its top speed or more both roll back; otherwise they stay (so a slow
// tank does not look as if it slides). Offsets stay in [0, 1).
export namespace generalszh::presentation
{
struct TreadMotion
{
	float speedFraction{0}; // of its top speed
	std::int64_t turn{0};   // signed turn since the last tick (positive: left)
	float seconds{0};       // game time this frame
};

// A truck's tires turning (the original's W3DTruckDraw): by `speed` (units a
// tick, negative reversing) times the multiplier each tick of game time
// (30 a second), kept in [-pi, pi).
inline float RollWheels(float angle, float speed, float multiplier, float seconds) noexcept
{
	constexpr float pi = 3.14159265358979f;
	angle += multiplier * speed * seconds * 30.0f;
	return angle - 2.0f * pi * std::floor((angle + pi) / (2.0f * pi));
}

// A truck's steering (the original's W3DTruckDraw with Drawable's wheel
// angle): the front wheels turn toward `wheelTurn` the way it turns (the
// other way when reversing), a tenth of the way each 30th of a second; its
// cab swings `cabFactor` times that and its trailer the other way
// `trailerFactor` times, easing by `damping`.
struct SteerMotion
{
	std::int64_t turn{0};  // signed turn since the last tick (positive: left)
	float along{0.0f};     // distance along its facing since the last tick (negative: reversing)
	float seconds{0.0f};   // game time this frame
};

template<typename Wheels>
inline void SteerWheels(Wheels &wheels, const SteerMotion &motion, float wheelTurn, float cabFactor, float trailerFactor, float damping) noexcept
{
	float target = motion.turn > 0 ? wheelTurn : motion.turn < 0 ? -wheelTurn : 0.0f;
	if (motion.along < 0.0f)
		target = -target;
	const float frames = motion.seconds * 30.0f;
	wheels.steer += (target - wheels.steer) * (1.0f - std::pow(0.9f, frames));
	const float ease = damping > 0.0f ? 1.0f - std::pow(1.0f - std::min(damping, 1.0f), frames) : 0.0f;
	wheels.cab += (wheels.steer * cabFactor - wheels.cab) * ease;
	wheels.trailer += (-wheels.steer * trailerFactor - wheels.trailer) * ease;
}

inline void RollTreads(std::array<float, 2> &treads, const TreadMotion &motion, float rate, float pivot, float drive) noexcept
{
	const float step = rate * motion.seconds;
	if (motion.turn != 0 && motion.speedFraction < pivot)
	{
		const float side = motion.turn > 0 ? step : -step;
		treads[0] += side;
		treads[1] -= side;
	}
	else if (motion.speedFraction >= drive)
	{
		treads[0] -= step;
		treads[1] -= step;
	}
	for (float &offset : treads)
		offset -= std::floor(offset);
}

// How a moving vehicle's emitters scale with its speed (units a tick):
// W3DTankDraw::doDrawModule kicks tread debris higher and thicker the faster
// it goes (velocity x and y by 0.5 * speed + 0.1, z and the burst count by
// speed + 0.1, each at most 1); W3DTruckDraw::doDrawModule grows its dust
// with its speed, capped at 2 (the landing burst aside).
struct MotionScale
{
	std::array<float, 3> debrisVelocity{};
	float debrisCount{0.0f};
	float dustSize{0.0f};
};

inline MotionScale ScaleMotionEmitters(float speed) noexcept
{
	MotionScale scale;
	const float across = std::min(0.5f * speed + 0.1f, 1.0f);
	const float up = std::min(speed + 0.1f, 1.0f);
	scale.debrisVelocity = {across, across, up};
	scale.debrisCount = up;
	scale.dustSize = std::min(speed, 2.0f);
	return scale;
}

// W3DTruckDraw::doDrawModule's wheel emitters for a truck under way (`motive`), given how it moved in the last
// tick: on the ground its dust and dirt run (enableWheelEmitters), else all stop; its dirt only while it speeds up
// (acceleration over 0.01 a tick, not against its velocity); its powerslide spray while it turns. Its dust grows with
// its speed (capped at 2), except on landing after more than 3 ticks in the air: 1 + ticks / 16 (whole ticks, at most
// 2) times 2, in a burst at once (trigger), with its landing sound.
struct TruckEmitters
{
	bool dust{false};
	bool dirt{false};
	bool powerslide{false};
	float dustSize{0.0f};
	bool landing{false};
};

inline TruckEmitters ScaleTruckEmitters(bool motive, bool airborne, float speed, const std::array<float, 2> &velocity,
	const std::array<float, 2> &acceleration, bool turning, std::uint32_t framesAirborne) noexcept
{
	TruckEmitters out;
	if (!motive || airborne)
		return out;
	out.dust = true;
	const bool accelerating = std::sqrt(acceleration[0] * acceleration[0] + acceleration[1] * acceleration[1]) > 0.01f &&
		!(acceleration[0] * velocity[0] + acceleration[1] * velocity[1] < 0.0f);
	out.dirt = accelerating;
	out.powerslide = turning;
	constexpr float SizeCap = 2.0f;
	if (framesAirborne > 3)
	{
		const float factor = std::min(1.0f + static_cast<float>(framesAirborne / 16), 2.0f);
		out.dustSize = factor * SizeCap;
		out.landing = true;
	}
	else
		out.dustSize = std::min(speed, SizeCap);
	return out;
}
}
