export module engine.gameplay.rts.combat.algorithms.thrust;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// A THRUST locomotor's steering in three dimensions (the original
// Locomotor.cpp's statics, in fixed point): turning one direction toward
// another by at most an angle, the direction to push so the velocity swings
// toward a goal, and the odd "forward speed" physics reports.
export namespace engine::gameplay
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;
using Engine::Math::TurnAngle;

// Locomotor.cpp isNearlyZero: under 0.001.
inline bool NearlyZero(Fixed value) noexcept { return Engine::Math::Abs(value) < Fixed::FromRatio(1, 1000); }

using Engine::Math::Cross;
using Engine::Math::Dot;

// Normalized_Legacy: a zero vector stays zero.
inline FixedVector3 Normalized(const FixedVector3 &v) noexcept
{
	const Fixed length = Engine::Math::Length(v);
	return length > Fixed{} ? v / length : FixedVector3{};
}

// The angle between two directions, 0 to half a turn.
inline std::int64_t AngleBetween(const FixedVector3 &a, const FixedVector3 &b) noexcept
{
	const TurnAngle angle = Engine::Math::Atan2(Engine::Math::Length(Cross(a, b)), Dot(a, b));
	return static_cast<std::int64_t>(static_cast<std::int32_t>(angle.units)) < 0 ? -static_cast<std::int64_t>(static_cast<std::int32_t>(angle.units))
		: static_cast<std::int64_t>(angle.units);
}

// Rodrigues: `v` turned about the unit `axis` by `angle`.
inline FixedVector3 RotateAbout(const FixedVector3 &v, const FixedVector3 &axis, TurnAngle angle) noexcept
{
	const Fixed c = Engine::Math::Cos(angle), s = Engine::Math::Sin(angle);
	return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (Fixed::One() - c));
}

inline constexpr std::int64_t Unlimited = std::int64_t{1} << 40; // BIGNUM: no cap on a turn

struct Turned
{
	FixedVector3 direction;
	std::int64_t angle{0}; // what it turned (the whole angle when it got there), turn units
};

// tryToRotateVector3D: `current` toward `goal` by at most `maxAngle` (turn units); none turns nothing.
inline Turned RotateToward(std::int64_t maxAngle, const FixedVector3 &current, const FixedVector3 &goal) noexcept
{
	if (maxAngle <= 0)
		return {current, 0};
	const FixedVector3 from = Normalized(current), to = Normalized(goal);
	const std::int64_t between = AngleBetween(from, to);
	if (between <= maxAngle)
		return {to, between};
	const FixedVector3 axis = Normalized(Cross(from, to));
	return {RotateAbout(from, axis, TurnAngle{static_cast<std::uint32_t>(maxAngle)}), maxAngle};
}

// calcDirectionToApplyThrust: the push that best swings the velocity (with this tick's gravity) toward the goal.
inline FixedVector3 ThrustDirection(const FixedVector3 &position, const FixedVector3 &velocity, const FixedVector3 &forward,
	const FixedVector3 &goal, Fixed maxAccel, Fixed gravity) noexcept
{
	const FixedVector3 toGoal = goal - position;
	if (NearlyZero(Engine::Math::LengthSquared(toGoal)))
		return forward;
	FixedVector3 vel = velocity;
	vel.z += gravity;
	const Fixed distance = Engine::Math::Length(toGoal);
	const Fixed speed = Engine::Math::Length(vel);
	const Fixed denominator = speed * speed - maxAccel * maxAccel;
	if (!NearlyZero(denominator))
	{
		Fixed t = distance * (speed + maxAccel) / denominator;
		const Fixed t2 = distance * (speed - maxAccel) / denominator;
		if (t >= Fixed{} || t2 >= Fixed{})
		{
			if (t < Fixed{} || (t2 >= Fixed{} && t2 < t))
				t = t2;
			if (!NearlyZero(t))
				return Normalized(toGoal / t - vel);
		}
	}
	return Normalized(toGoal);
}

// PhysicsBehavior::getForwardSpeed3D: the length of the velocity scaled by the forward axis component by
// component, negative when moving backwards (as the original computes it).
inline Fixed ForwardSpeed3D(const FixedVector3 &velocity, const FixedVector3 &forward) noexcept
{
	const FixedVector3 scaled{velocity.x * forward.x, velocity.y * forward.y, velocity.z * forward.z};
	const Fixed speed = Engine::Math::Length(scaled);
	return scaled.x + scaled.y + scaled.z >= Fixed{} ? speed : -speed;
}
}
