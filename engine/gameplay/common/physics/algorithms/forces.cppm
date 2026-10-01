export module engine.gameplay.common.physics.algorithms.forces;
import std;

export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.attitude;
export import Engine.Core.Math.FixedAngle;

// Forces on a physics body and one step of its motion, as the original's
// PhysicsBehavior: a force adds F / m to this tick's acceleration; a step
// applies gravity and friction (ground friction split into forward and
// lateral parts along the facing, air friction proportional to velocity),
// integrates velocity and position (Euler), turns the body by its rates,
// keeps it out of the ground and bounces it back off with the ground's
// stiffness, righting it.
export namespace engine::gameplay
{
namespace physics_detail
{
using Engine::Math::Fixed;

constexpr Fixed MinGroundFriction = Fixed::FromRatio(1, 100);
constexpr Fixed MaxFriction = Fixed::FromRatio(99, 100);
constexpr Fixed Tiny = Fixed::FromRatio(1, 1000);

// A turn rate times a factor, in turn units.
constexpr std::int32_t Scale(std::int32_t rate, Fixed factor) noexcept
{
	return static_cast<std::int32_t>((static_cast<std::int64_t>(rate) * factor.Raw()) >> Fixed::FractionBits);
}
}

inline void ApplyForce(PhysicsBody &body, const Engine::Math::FixedVector3 &force) noexcept
{
	const Engine::Math::Fixed mass = body.mass > Engine::Math::Fixed{} ? body.mass : Engine::Math::Fixed::One();
	body.acceleration.x += force.x / mass;
	body.acceleration.y += force.y / mass;
	body.acceleration.z += force.z / mass;
}

// PhysicsBehavior::applyMotiveForce: a locomotor's force, marking the body driven for MOTIVE_FRAMES (10) ticks.
inline void ApplyMotiveForce(PhysicsBody &body, const Engine::Math::FixedVector3 &force, std::uint64_t tick) noexcept
{
	ApplyForce(body, force);
	body.motiveUntil = tick + 10;
}

inline void DampRates(PhysicsBody &body, Engine::Math::Fixed factor) noexcept
{
	body.yawRate = physics_detail::Scale(body.yawRate, factor);
	body.pitchRate = physics_detail::Scale(body.pitchRate, factor);
	body.rollRate = physics_detail::Scale(body.rollRate, factor);
}

// One tick of motion over ground at `groundHeight` (sampled where the body
// ends up). `attitude` may be null (the body then only turns around z).
struct StepResult
{
	bool resting{false};      // now lies still on the ground
	Engine::Math::Fixed fall; // landed this step from a fall steep and fast enough to hurt: by how much
	bool landed{false};       // airborne the step before, not now, not immune to falling (its bounce sound plays)
};

// `held` (DISABLED_HELD): PhysicsBehavior::update skips forces, integration, turning and the ground
// clamp; its acceleration still clears and landing and rest are still judged.
template<typename Ground>
StepResult StepBody(PhysicsBody &body, Transform &transform, Attitude *attitude, const PhysicsSettings &settings, Ground &&groundHeight,
	bool held = false, bool motive = false) noexcept
{
	using namespace physics_detail;
	using Engine::Math::Fixed;
	auto &position = transform.position;
	auto &velocity = body.velocity;
	Fixed bounce{};
	bool bounced = false;
	Fixed activeVelZ = velocity.z;
	if (!held)
	{
	body.acceleration.z += settings.gravity;
	// Friction: along the ground unless really airborne (or its locomotor keeps it in the air); driven by a locomotor
	// (`motive`), no forward friction (PhysicsBehavior::applyFrictionalForces).
	const Fixed startGround = groundHeight(position.XY());
	if (body.Has(physics_flag::AirborneFriction) || position.z - startGround <= settings.SignificantHeight())
	{
		DampRates(body, Fixed::One() - Fixed::FromRatio(15, 100));
		if (velocity.x != Fixed{} || velocity.y != Fixed{})
		{
			const Fixed dx = Engine::Math::Cos(transform.facing), dy = Engine::Math::Sin(transform.facing);
			const Fixed lateralDot = velocity.x * (Fixed{} - dy) + velocity.y * dx;
			const Fixed forwardDot = velocity.x * dx + velocity.y * dy;
			const Fixed lateral = Engine::Math::Clamp(body.lateralFriction + body.extraFriction, MinGroundFriction, MaxFriction);
			const Fixed forward = Engine::Math::Clamp(body.forwardFriction + body.extraFriction, MinGroundFriction, MaxFriction);
			// F = m * friction * v, so a = friction * v.
			const Fixed forwardShare = motive ? Fixed{} : forward;
			body.acceleration.x -= lateral * lateralDot * (Fixed{} - dy) + forwardShare * forwardDot * dx;
			body.acceleration.y -= lateral * lateralDot * dx + forwardShare * forwardDot * dy;
		}
	}
	else
	{
		const Fixed air = Engine::Math::Clamp(body.aerodynamicFriction + body.extraFriction, Fixed{}, MaxFriction);
		body.acceleration.x -= velocity.x * air;
		body.acceleration.y -= velocity.y * air;
		body.acceleration.z -= velocity.z * air;
		DampRates(body, Fixed::One() - air);
	}

	velocity.x += body.acceleration.x;
	velocity.y += body.acceleration.y;
	velocity.z += body.acceleration.z;
	for (Fixed *component : {&velocity.x, &velocity.y, &velocity.z})
		if (Engine::Math::Abs(*component) < Tiny)
			*component = Fixed{};

	activeVelZ = velocity.z;
	const Fixed oldZ = position.z;
	position.x += velocity.x;
	position.y += velocity.y;
	position.z += velocity.z;

	transform.facing = transform.facing + Engine::Math::TurnAngle{static_cast<std::uint32_t>(Scale(body.yawRate, body.rateFactor))};
	if (attitude != nullptr)
	{
		// With a CenterOfMassOffset the pitch rate eases off as it comes to point straight down (a negative offset: up):
		// scaled by sin of the angle left to go, cos(pitch) with its sign (PhysicsBehavior::update).
		std::int32_t pitchStep = Scale(body.pitchRate, body.rateFactor);
		if (body.centerOfMassOffset != Fixed{})
		{
			const Fixed lean = Engine::Math::Cos(attitude->pitch);
			pitchStep = Scale(pitchStep, body.centerOfMassOffset > Fixed{} ? lean : Fixed{} - lean);
		}
		attitude->pitch = attitude->pitch + Engine::Math::TurnAngle{static_cast<std::uint32_t>(pitchStep)};
		attitude->roll = attitude->roll + Engine::Math::TurnAngle{static_cast<std::uint32_t>(Scale(body.rollRate, body.rateFactor))};
	}

	// The ground pushes back: a bounce with its stiffness, then no sinking in.
	const Fixed ground = groundHeight(position.XY());
	if (body.Has(physics_flag::AllowBouncing) && position.z <= ground)
	{
		const Fixed stiffness = Engine::Math::Clamp(settings.groundStiffness, Fixed::FromRatio(1, 100), MaxFriction);
		if (oldZ > ground && velocity.z < Fixed{})
			bounce = Engine::Math::Abs(velocity.z) * stiffness;
		DampRates(body, Fixed::FromRatio(7, 10));
		bounced = bounce > Fixed{};
		if (!bounced)
			body.Set(physics_flag::AllowBouncing, body.Has(physics_flag::AuthoredBouncing));
	}
	if (position.z <= ground)
	{
		velocity.z += ground - position.z;
		if (velocity.z > Fixed{})
			velocity.z = Fixed{};
		position.z = ground;
		body.Set(physics_flag::AllowToFall, false);
	}
	else if (body.Has(physics_flag::StickToGround) && !body.Has(physics_flag::AllowToFall))
		position.z = ground;
	// A bounce rights the body.
	if (bounced && attitude != nullptr)
		*attitude = {};
	}

	const Fixed ground = groundHeight(position.XY());
	body.acceleration = {};
	if (bounced)
		body.acceleration.z += bounce;
	StepResult result;
	// Landing: hurt by a steep fall only (not going down a hill), as the original.
	const bool airborneAtEnd = position.z - ground > Tiny;
	if (body.Has(physics_flag::WasAirborne) && !airborneAtEnd && !body.Has(physics_flag::ImmuneToFalling) && body.fallDamageFactor > Fixed{})
	{
		const Fixed net = (Fixed{} - activeVelZ) - body.minFallSpeed;
		const Fixed drop = Engine::Math::Abs(activeVelZ), steep = Fixed::FromInt(3), tiny = Fixed::FromRatio(1, 100);
		const bool steepX = Engine::Math::Abs(velocity.x) <= tiny || drop >= Engine::Math::Abs(velocity.x) * steep;
		const bool steepY = Engine::Math::Abs(velocity.y) <= tiny || drop >= Engine::Math::Abs(velocity.y) * steep;
		if (net > Fixed{} && steepX && steepY)
			result.fall = net;
	}
	result.landed = body.Has(physics_flag::WasAirborne) && !airborneAtEnd && !body.Has(physics_flag::ImmuneToFalling);
	body.Set(physics_flag::WasAirborne, airborneAtEnd);
	const Fixed still = Fixed::FromRatio(1, 100);
	result.resting = position.z <= ground && Engine::Math::Abs(velocity.x) < still && Engine::Math::Abs(velocity.y) < still &&
		Engine::Math::Abs(velocity.z) < still && !bounced;
	return result;
}
}
