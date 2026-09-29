export module engine.gameplay.rts.movement.algorithms.hover_motion;
import std;

export import engine.gameplay.common.physics.algorithms.forces;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// A force-driven HOVER locomotor, as the original's Locomotor drives a body through its PhysicsBehavior (the physics
// step integrates what it applies): turning toward its goal at its turn rate and pushing along its facing toward its
// speed (moveTowardsPositionOther), braking to a stop when holding a place (maintainCurrentPositionHover), and lifting
// against gravity toward a height relative to the surface (handleBehaviorZ, Z_SURFACE_RELATIVE_HEIGHT;
// calcLiftToUseAtPt). Rates are per tick, accelerations per tick squared. ULTRA_ACCURATE (a precise landing) adds
// friction and lets the lift work harder.
export namespace engine::gameplay
{
struct HoverLocomotor
{
	Engine::Math::Fixed maxSpeed;
	Engine::Math::Fixed minSpeed;
	Engine::Math::Fixed acceleration;
	Engine::Math::Fixed braking;
	Engine::Math::Fixed lift;
	// Once damaged to the game's movement penalty state (and dead): AccelerationDamaged, LiftDamaged (unset: as whole).
	Engine::Math::Fixed accelerationDamaged;
	Engine::Math::Fixed liftDamaged;
	Engine::Math::Fixed speedLimitZ{Engine::Math::Fixed::FromInt(999999)};
	Engine::Math::Fixed extraFriction; // Extra2DFriction, per tick
	Engine::Math::Fixed preferredHeight;
	Engine::Math::Fixed heightDamping{Engine::Math::Fixed::One()};
	Engine::Math::Fixed closeEnough;
	Engine::Math::Fixed pitchStiffness;
	Engine::Math::Fixed rollStiffness;
	Engine::Math::Fixed pitchDamping;
	Engine::Math::Fixed rollDamping;
	Engine::Math::TurnAngle turnRate; // per tick
	bool airborneFriction{false};     // Apply2DFrictionWhenAirborne
	bool closeEnough3D{false};        // CloseEnoughDist3D
};

// Locomotor::setPhysicsOptions: its friction (half more when ultra-accurate) and airborne friction on its body.
inline void SetHoverPhysics(PhysicsBody &body, const HoverLocomotor &locomotor, bool ultraAccurate) noexcept
{
	body.extraFriction = locomotor.extraFriction + (ultraAccurate ? Engine::Math::Fixed::FromRatio(1, 2) : Engine::Math::Fixed{});
	body.Set(physics_flag::AirborneFriction, locomotor.airborneFriction);
}

// PhysicsBehavior::getForwardSpeed2D: sqrt((vx dx)^2 + (vy dy)^2), signed by the dot of velocity and facing.
inline Engine::Math::Fixed ForwardSpeed2D(const PhysicsBody &body, Engine::Math::TurnAngle facing) noexcept
{
	using Engine::Math::Fixed;
	const Fixed vx = body.velocity.x * Engine::Math::Cos(facing), vy = body.velocity.y * Engine::Math::Sin(facing);
	const Fixed speed = Engine::Math::Sqrt(vx * vx + vy * vy);
	return vx + vy >= Fixed{} ? speed : Fixed{} - speed;
}

// calcSlowDownDist: (speed over the goal)^2 / braking / 2, with a 5% fudge.
inline Engine::Math::Fixed SlowDownDistance(Engine::Math::Fixed current, Engine::Math::Fixed desired, Engine::Math::Fixed braking) noexcept
{
	using Engine::Math::Fixed;
	const Fixed delta = current - desired;
	if (delta <= Fixed{} || braking == Fixed{})
		return {};
	return delta * delta / Engine::Math::Abs(braking) * Fixed::FromRatio(1, 2) * Fixed::FromRatio(105, 100);
}

// calcLiftToUseAtPt: the lift (acceleration, before gravity) that brings it to `preferredZ` with no speed left.
inline Engine::Math::Fixed HoverLift(const PhysicsBody &body, const HoverLocomotor &locomotor, bool ultraAccurate, Engine::Math::Fixed currentZ,
	Engine::Math::Fixed preferredZ, Engine::Math::Fixed gravity) noexcept
{
	using Engine::Math::Fixed;
	using Engine::Math::Abs;
	const Fixed maxGross = locomotor.lift;
	const Fixed maxNet = std::max(maxGross + gravity, Fixed{});
	const Fixed velocityZ = body.velocity.z;
	const Fixed maxAccel = ultraAccurate ? (velocityZ < Fixed{} ? maxNet * Fixed::FromInt(2) : Fixed{} - maxNet * Fixed::FromInt(2))
										 : (velocityZ < Fixed{} ? maxNet : gravity);
	Fixed desired{};
	if (Abs(maxAccel) > Fixed::FromRatio(1, 1000))
	{
		const Fixed deltaZ = preferredZ - currentZ;
		const Fixed brakeDistance = velocityZ * velocityZ / Abs(maxAccel);
		if (Abs(brakeDistance) > Abs(deltaZ))
			desired = maxAccel;
		else if (Abs(velocityZ) > locomotor.speedLimitZ)
			desired = locomotor.speedLimitZ - velocityZ; // as the original (not signed by the direction of travel)
		else
			desired = Fixed::FromInt(2) * (deltaZ - velocityZ);
	}
	Fixed lift = desired - gravity;
	if (ultraAccurate)
	{
		if (lift > maxGross * Fixed::FromInt(3))
			lift = maxGross * Fixed::FromInt(3);
		else if (lift < Fixed{} - maxGross)
			lift = Fixed{} - maxGross;
	}
	else
		lift = std::clamp(lift, Fixed{}, std::max(maxGross, Fixed{}));
	return lift;
}

// handleBehaviorZ (Z_SURFACE_RELATIVE_HEIGHT): toward its preferred height over the surface below it (damped), as
// a motive force. Nothing while its preferred height is zero.
inline void HoverHeight(PhysicsBody &body, const Engine::Math::FixedVector3 &position, const HoverLocomotor &locomotor, bool ultraAccurate,
	Engine::Math::Fixed surface, Engine::Math::Fixed gravity, std::uint64_t tick) noexcept
{
	using Engine::Math::Fixed;
	if (locomotor.preferredHeight == Fixed{})
		return;
	const Fixed preferred = position.z + (locomotor.preferredHeight + surface - position.z) * locomotor.heightDamping;
	const Fixed lift = HoverLift(body, locomotor, ultraAccurate, position.z, preferred, gravity);
	if (lift != Fixed{})
		ApplyMotiveForce(body, {Fixed{}, Fixed{}, lift * body.mass}, tick);
}

// Locomotor::locoUpdate_moveTowardsPosition for HOVER (airborne, never blocked or braking): driven (a null motive
// force), turned toward the goal at its turn rate, pushed along its facing (as it was before turning) toward its
// speed, or its least speed within its slowing distance; then its height.
inline void HoverMoveTowards(PhysicsBody &body, Transform &transform, const Engine::Math::FixedVector3 &goal, Engine::Math::Fixed onPathDistance,
	Engine::Math::Fixed desiredSpeed, const HoverLocomotor &locomotor, bool ultraAccurate, Engine::Math::Fixed surface, Engine::Math::Fixed gravity,
	std::uint64_t tick) noexcept
{
	using Engine::Math::Fixed;
	const Fixed dx = goal.x - transform.position.x, dy = goal.y - transform.position.y;
	onPathDistance = std::max(onPathDistance, Engine::Math::Sqrt(dx * dx + dy * dy));
	ApplyMotiveForce(body, {}, tick);
	desiredSpeed = std::min(desiredSpeed, locomotor.maxSpeed);
	Fixed goalSpeed = desiredSpeed;
	const Fixed actualSpeed = ForwardSpeed2D(body, transform.facing);
	const Engine::Math::TurnAngle pushing = transform.facing;
	// rotateObjAroundLocoPivot (no pivot offset): turned toward the goal, at most its turn rate.
	const auto amount = static_cast<std::int32_t>((Engine::Math::Atan2(dy, dx) - transform.facing).units);
	const auto limit = static_cast<std::int32_t>(std::min<std::uint32_t>(locomotor.turnRate.units, 0x7FFFFFFFu));
	transform.facing = transform.facing + Engine::Math::TurnAngle{static_cast<std::uint32_t>(std::clamp(amount, -limit, limit))};
	if (onPathDistance < SlowDownDistance(actualSpeed, locomotor.minSpeed, locomotor.braking))
		goalSpeed = locomotor.minSpeed;
	const Fixed delta = goalSpeed - actualSpeed;
	if (delta != Fixed{})
	{
		Fixed force = body.mass * (delta > Fixed{} ? locomotor.acceleration : Fixed{} - locomotor.braking);
		const Fixed needed = body.mass * delta;
		if (Engine::Math::Abs(force) > Engine::Math::Abs(needed))
			force = needed;
		ApplyMotiveForce(body, {force * Engine::Math::Cos(pushing), force * Engine::Math::Sin(pushing), Fixed{}}, tick);
	}
	HoverHeight(body, transform.position, locomotor, ultraAccurate, surface, gravity, tick);
}

// Locomotor::locoUpdate_maintainCurrentPosition for HOVER: while still driven, braked to (nearly) no forward speed;
// then its height.
inline void HoverMaintain(PhysicsBody &body, const Transform &transform, const HoverLocomotor &locomotor, bool ultraAccurate, Engine::Math::Fixed surface,
	Engine::Math::Fixed gravity, std::uint64_t tick) noexcept
{
	using Engine::Math::Fixed;
	if (tick < body.motiveUntil)
	{
		const Fixed actualSpeed = ForwardSpeed2D(body, transform.facing);
		const Fixed minSpeed = std::max(Fixed::FromRaw(1), locomotor.minSpeed);
		const Fixed delta = minSpeed - actualSpeed;
		if (Engine::Math::Abs(delta) > minSpeed)
		{
			Fixed force = body.mass * (delta > Fixed{} ? locomotor.acceleration : Fixed{} - locomotor.braking);
			const Fixed needed = body.mass * delta;
			if (Engine::Math::Abs(force) > Engine::Math::Abs(needed))
				force = needed;
			ApplyMotiveForce(body, {force * Engine::Math::Cos(transform.facing), force * Engine::Math::Sin(transform.facing), Fixed{}}, tick);
		}
	}
	HoverHeight(body, transform.position, locomotor, ultraAccurate, surface, gravity, tick);
}
}
