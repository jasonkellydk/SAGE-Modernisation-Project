export module engine.gameplay.rts.movement.algorithms.flight_forces;
import std;

export import engine.gameplay.rts.movement.algorithms.steering;
export import engine.gameplay.common.physics.algorithms.forces;
export import engine.gameplay.rts.movement.algorithms.hover_motion;
export import engine.gameplay.common.spatial.resources.ground_height;

// A unit's flight driven by forces on its body, as the original's Locomotor drives every unit through its
// PhysicsBehavior (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Locomotor.cpp): each tick its locomotor applies
// motive forces (locoUpdate_moveTowardsPosition -> moveTowardsPositionOther / Wings: a push along its facing, as it was
// before it turned, toward the speed it wants; locoUpdate_maintainCurrentPosition: circling (wings) or braking (hover);
// locoUpdate_moveTowardsAngle; handleBehaviorZ: lift toward its preferred height, calcLiftToUseAtPt), then its physics
// steps (PhysicsBehavior::update: gravity, friction, Euler integration). Flown so: HOVER and WINGS locomotors over the
// air holding their height by lift (Z_SURFACE_RELATIVE_HEIGHT, Z_ABSOLUTE_HEIGHT, Z_SMOOTH_RELATIVE_TO_HIGHEST_LAYER) on
// a unit with a body. Fixed point throughout.
export namespace engine::gameplay
{
inline bool FliesByForce(const LocomotorDefinition &locomotor) noexcept
{
	if (locomotor.appearance != LocomotorAppearance::Hover && locomotor.appearance != LocomotorAppearance::Wings)
		return false;
	if ((locomotor.surfaces & 8u) == 0)
		return false;
	return locomotor.height == HeightBehavior::SurfaceRelative || locomotor.height == HeightBehavior::Absolute ||
		locomotor.height == HeightBehavior::RelativeToHighestLayer;
}

// What one force-flown tick needs about where the unit is.
struct FlightStep
{
	PhysicsBody *body{nullptr};
	const PhysicsSettings *settings{nullptr};
	const GroundHeight *ground{nullptr};
	// The height its locomotor holds above (getSurfaceHtAtPt: ground or water; the highest layer's for
	// RELATIVE_TO_HIGHEST_LAYER) and its layer's height under it (getLayerHeight: its height above terrain).
	Engine::Math::Fixed surface;
	Engine::Math::Fixed terrain;
	std::uint64_t tick{0};
	bool aloft{true}; // Thing::isAboveTerrain
};

namespace flight_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline bool Motive(const FlightStep &step) noexcept { return step.tick < step.body->motiveUntil; }

// Locomotor::calcLiftToUseAtPt: the lift (an acceleration, before gravity) that brings it to `preferredZ` with no
// vertical speed left: its full net lift (going down) or gravity (going up) when it needs longer to stop than it has,
// back toward SpeedLimitZ when faster than that, else 2 (dz - vz); clipped to [0, its lift] (ultra-accurate: twice the
// braking, between minus its lift and three times it).
inline Fixed Lift(const PhysicsBody &body, const Locomotion &motion, Fixed currentZ, Fixed preferredZ, Fixed gravity) noexcept
{
	const auto &locomotor = motion.locomotor;
	const bool ultra = motion.ultraAccurate != 0;
	const Fixed maxGross = std::min(locomotor.lift, motion.liftCap); // getMaxLift: no more than m_maxLift
	const Fixed maxNet = std::max(maxGross + gravity, Fixed{});
	const Fixed velocityZ = body.velocity.z;
	const Fixed maxAccel = ultra ? (velocityZ < Fixed{} ? maxNet * Fixed::FromInt(2) : Fixed{} - maxNet * Fixed::FromInt(2))
								 : (velocityZ < Fixed{} ? maxNet : gravity);
	Fixed desired{};
	if (Engine::Math::Abs(maxAccel) > Fixed::FromRatio(1, 1000))
	{
		const Fixed deltaZ = preferredZ - currentZ;
		const Fixed brakeDistance = velocityZ * velocityZ / Engine::Math::Abs(maxAccel);
		if (Engine::Math::Abs(brakeDistance) > Engine::Math::Abs(deltaZ))
			desired = maxAccel;
		else if (Engine::Math::Abs(velocityZ) > locomotor.speedLimitZ)
			desired = locomotor.speedLimitZ - velocityZ; // as the original (not signed by the way it goes)
		else
			desired = Fixed::FromInt(2) * (deltaZ - velocityZ);
	}
	Fixed lift = desired - gravity;
	if (ultra)
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
}

// Locomotor::handleBehaviorZ for the lift-held heights: toward its preferred height over the surface (absolute: over
// zero; PRECISE_Z_POS: `preciseZ`, its goal's height), damped by PreferredHeightDamping, by a motive lift force. Nothing
// while its preferred height is 0 and it is not precise.
inline void FlightHeight(const Transform &transform, const Locomotion &motion, const FlightStep &step, Engine::Math::Fixed preciseZ) noexcept
{
	using Engine::Math::Fixed;
	const auto &locomotor = motion.locomotor;
	if (locomotor.preferredHeight == Fixed{} && motion.preciseZ == 0)
		return;
	const Fixed z = transform.position.z;
	Fixed preferred = locomotor.preferredHeight + (locomotor.height == HeightBehavior::Absolute ? Fixed{} : step.surface);
	if (motion.preciseZ != 0)
		preferred = preciseZ;
	preferred = z + (preferred - z) * locomotor.preferredHeightDamping;
	const Fixed lift = flight_detail::Lift(*step.body, motion, z, preferred, step.settings->gravity);
	if (lift != Fixed{})
		ApplyMotiveForce(*step.body, {Fixed{}, Fixed{}, lift * step.body->mass}, step.tick);
}

// Locomotor::moveTowardsPositionOther (WINGS through moveTowardsPositionWings): ultra-accurate within SlideIntoPlaceTime's
// travel of the goal on both axes it does not turn and is pushed straight at it; else it turns toward it at its turn
// rate (rotateObjAroundLocoPivot) and is pushed along its facing as it was before turning; toward the speed it asks for,
// or its least speed once it needs longer to stop than `onPath`; by its acceleration or braking (times its mass), never
// more than the speed difference needs.
inline void FlightPush(Transform &transform, Locomotion &motion, const FlightStep &step, Engine::Math::FixedVector2 goal, Engine::Math::Fixed onPath,
	Engine::Math::Fixed desiredSpeed) noexcept
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	const auto &locomotor = motion.locomotor;
	PhysicsBody &body = *step.body;
	Fixed goalSpeed = std::min(desiredSpeed, std::min(locomotor.maxSpeed, motion.speedCap)); // getMaxSpeedForCondition
	const Fixed actual = ForwardSpeed2D(body, transform.facing);
	FixedVector2 push{Engine::Math::Cos(transform.facing), Engine::Math::Sin(transform.facing)};
	const FixedVector2 toGoal = goal - transform.position.XY();
	const Fixed reach = goalSpeed * locomotor.slideIntoPlace;
	if (motion.ultraAccurate != 0 && locomotor.slideIntoPlace > Fixed{} && Engine::Math::Abs(toGoal.x) <= reach && Engine::Math::Abs(toGoal.y) <= reach)
	{
		motion.turning = 0;
		const Fixed length = Engine::Math::Length(toGoal);
		push = length > Fixed{} ? FixedVector2{toGoal.x / length, toGoal.y / length} : FixedVector2{};
	}
	else
		ground_detail::TurnAbout(transform, motion, goal, MaxTurnRate(motion));
	if (onPath < ground_detail::SlowDownDistance(actual, locomotor.minSpeed, locomotor.braking))
		goalSpeed = locomotor.minSpeed;
	const Fixed delta = goalSpeed - actual;
	if (delta == Fixed{})
		return;
	Fixed force = body.mass * (delta > Fixed{} ? locomotor.acceleration : Fixed{} - locomotor.braking);
	const Fixed needed = body.mass * delta;
	if (Engine::Math::Abs(force) > Engine::Math::Abs(needed))
		force = needed;
	ApplyMotiveForce(body, {force * push.x, force * push.y, Fixed{}}, step.tick);
}

// Locomotor::locoUpdate_moveTowardsPosition for a flyer (not blocked: airborne flyers do not collide). The place it held
// let go. IS_BRAKING (`braking`) is let go once it has more than a cell and more than its stopping distance at full speed
// left; it brakes when straight at the goal is more than twice what it has left along its path (an explicit goal: none
// left, so always while short of it), and takes the straight distance when that is more; wings never brake. Driven (a
// null motive force); unless it may push while airborne (AllowAirborneMotiveForce) it does nothing in 2D while more than
// three ticks' fall over its terrain; then its height. OBJECT_STATUS_BRAKING (`brakingStatus`) becomes IS_BRAKING; braking
// the tick before, it is set straight toward the goal by its forward speed (at least MIN_VEL, a cell a second; never past
// it): its physics then moves it in z only.
inline void FlightMoveTowards(Transform &transform, Locomotion &motion, const FlightStep &step, Engine::Math::FixedVector2 goal,
	Engine::Math::Fixed onPath, Engine::Math::Fixed desiredSpeed) noexcept
{
	using Engine::Math::Fixed;
	const auto &locomotor = motion.locomotor;
	motion.maintaining = 0;
	const Fixed topSpeed = std::min(locomotor.maxSpeed, motion.speedCap);
	const Fixed stopping = locomotor.braking > Fixed{} ? topSpeed / locomotor.braking * topSpeed / Fixed::FromInt(2) : Fixed{};
	if (onPath > Fixed::FromInt(ground_detail::CellSize) && onPath > stopping)
	{
		motion.braking = 0;
		motion.brakingFactor = Fixed::One();
	}
	const Engine::Math::FixedVector2 toGoal = goal - transform.position.XY();
	const Fixed distance = Engine::Math::Length(toGoal);
	if (distance > onPath)
	{
		if (distance > onPath * Fixed::FromInt(2))
			motion.braking = 1;
		onPath = distance;
	}
	ApplyMotiveForce(*step.body, {}, step.tick);
	if (locomotor.appearance == LocomotorAppearance::Wings)
		motion.braking = 0;
	const bool wasBraking = motion.brakingStatus != 0;
	const bool airborne = transform.position.z - step.terrain > step.settings->SignificantHeight();
	if (locomotor.airborneMotiveForce != 0 || !airborne)
		FlightPush(transform, motion, step, goal, onPath, desiredSpeed);
	FlightHeight(transform, motion, step, motion.preciseHeight);
	motion.brakingStatus = motion.braking;
	if (wasBraking && distance > Fixed::FromRatio(1, 1000))
	{
		Fixed step2D = Engine::Math::Abs(ForwardSpeed2D(*step.body, transform.facing));
		step2D = std::min(std::max(step2D, Fixed::FromRatio(ground_detail::CellSize, 30)), distance); // MIN_VEL
		transform.position.x += toGoal.x / distance * step2D;
		transform.position.y += toGoal.y / distance * step2D;
	}
}

// AIUpdateInterface::getLocomotorDistanceToGoal for a flyer's leg (computeFlightDistToGoal over its path from where it
// was when the goal was set to the goal: what is left along that line, 0 once past; for aircraft no more than the
// straight distance). The leg starts afresh when the goal changes.
struct FlightLeg
{
	Engine::Math::Fixed along; // computeFlightDistToGoal
	Engine::Math::Fixed left;  // getLocomotorDistanceToGoal
};

inline FlightLeg TrackFlightLeg(const Transform &transform, Locomotion &motion, Engine::Math::FixedVector2 goal) noexcept
{
	using Engine::Math::Fixed;
	if (motion.tracking == 0 || motion.flightGoal != goal)
	{
		motion.flightFrom = transform.position.XY();
		motion.flightGoal = goal;
		motion.tracking = 1;
	}
	const Engine::Math::FixedVector2 path = goal - motion.flightFrom;
	const Fixed length = Engine::Math::Length(path);
	const Engine::Math::FixedVector2 toGoal = goal - transform.position.XY();
	FlightLeg leg;
	if (length > Fixed{})
	{
		const Fixed dot = (toGoal.x * path.x + toGoal.y * path.y) / length;
		leg.along = dot >= Fixed{} ? dot : Fixed{};
	}
	const Fixed straight = Engine::Math::Length(toGoal);
	leg.left = leg.along > straight ? straight : leg.along;
	return leg;
}

// Locomotor::locoUpdate_maintainCurrentPosition for a flyer: the place it holds taken; not braking; wings above their
// terrain, while driven, aim for the far side of their circle at their least speed (maintainCurrentPositionWings); hover,
// while driven, brakes its forward speed toward its least (maintainCurrentPositionHover); then its height.
inline void FlightMaintain(Transform &transform, Locomotion &motion, const FlightStep &step) noexcept
{
	using Engine::Math::Fixed;
	const auto &locomotor = motion.locomotor;
	if (motion.maintaining == 0)
	{
		motion.maintainPos = transform.position.XY();
		motion.maintaining = 1;
	}
	motion.donutTimer = step.tick + 75; // DONUT_TIME_DELAY_SECONDS x LOGICFRAMES_PER_SECOND
	motion.braking = 0;
	motion.tracking = 0; // no goal: the next is a new leg
	motion.turning = 0;
	if (locomotor.appearance == LocomotorAppearance::Wings)
	{
		if (flight_detail::Motive(step) && step.aloft)
			FlightPush(transform, motion, step, CirclingAim(transform, motion), Fixed{}, locomotor.minSpeed);
	}
	else if (flight_detail::Motive(step))
	{
		PhysicsBody &body = *step.body;
		const Fixed actual = ForwardSpeed2D(body, transform.facing);
		const Fixed least = std::max(Fixed::FromRaw(1), locomotor.minSpeed);
		const Fixed delta = least - actual;
		if (Engine::Math::Abs(delta) > least)
		{
			Fixed force = body.mass * (delta > Fixed{} ? locomotor.acceleration : Fixed{} - locomotor.braking);
			const Fixed needed = body.mass * delta;
			if (Engine::Math::Abs(force) > Engine::Math::Abs(needed))
				force = needed;
			ApplyMotiveForce(body, {force * Engine::Math::Cos(transform.facing), force * Engine::Math::Sin(transform.facing), Fixed{}}, step.tick);
		}
	}
	FlightHeight(transform, motion, step, motion.preciseHeight);
}

// One tick toward `goal` (the movement's Steer for a flyer); true once there.
// - An explicit goal (setLocomotorGoalPositionExplicit): toward it with nothing left to go; never there (its state judges).
// - A final goal: AIInternalMoveToState::update's test, what is left of its leg (TrackFlightLeg; with CloseEnoughDist3D
//   the straight 3D distance to the goal on the ground under it) under CloseEnoughDist; there, it holds its place this
//   tick (its goal gone: maintain); else on, with what is left along its leg plus the rest of its path (flightExtra).
// - A goal on the way (a waypoint): within the port's waypoint distance (ArrivalDistance), pushing on for it.
inline bool FlightSteer(Transform &transform, Locomotion &motion, const FlightStep &step, Engine::Math::FixedVector2 goal, bool final,
	Engine::Math::Fixed onPath, Engine::Math::Fixed desiredSpeed, bool explicitGoal = false) noexcept
{
	using Engine::Math::Fixed;
	if (explicitGoal)
	{
		motion.tracking = 0;
		FlightMoveTowards(transform, motion, step, goal, Fixed{}, desiredSpeed);
		return false;
	}
	if (final)
	{
		const FlightLeg leg = TrackFlightLeg(transform, motion, goal);
		Fixed left = leg.left;
		if (motion.locomotor.closeEnough3D != 0 && step.ground != nullptr)
		{
			const Fixed flat = Engine::Math::Distance(transform.position.XY(), goal);
			const Fixed dz = transform.position.z - step.ground->At(goal);
			left = Engine::Math::Sqrt(flat * flat + dz * dz);
		}
		if (left < motion.locomotor.closeEnough)
		{
			FlightMaintain(transform, motion, step);
			return true;
		}
		FlightMoveTowards(transform, motion, step, goal, leg.along + motion.flightExtra, desiredSpeed);
		return false;
	}
	const bool reached = Engine::Math::Distance(transform.position.XY(), goal) <= ArrivalDistance(motion, false);
	FlightMoveTowards(transform, motion, step, goal, onPath, desiredSpeed);
	return reached;
}

// Locomotor::locoUpdate_moveTowardsAngle (AIFaceState) for a flyer, after the face test (as Face): with a least speed
// it heads for a point two ticks' least travel along the goal's bearing (no slowing down); without, it turns where it is
// toward a point 1000 off along it, and holds its own height. True once facing the goal (as Face).
inline bool FlightFace(Transform &transform, Locomotion &motion, const FlightStep &step, Engine::Math::FixedVector2 goal) noexcept
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector2;
	const FixedVector2 toGoal = goal - transform.position.XY();
	if (toGoal.x == Fixed{} && toGoal.y == Fixed{})
		return true;
	const Engine::Math::TurnAngle bearing = Engine::Math::Heading(toGoal);
	const std::int32_t wanted = Engine::Math::DeltaTo(transform.facing, bearing);
	if (wanted > -FacingThreshold && wanted < FacingThreshold)
		return true;
	motion.maintaining = 0;
	const auto &locomotor = motion.locomotor;
	const Fixed bx = Engine::Math::Cos(bearing), by = Engine::Math::Sin(bearing);
	if (locomotor.minSpeed > Fixed{})
	{
		const Fixed ahead = locomotor.minSpeed * Fixed::FromInt(2);
		FlightMoveTowards(transform, motion, step, {transform.position.x + bx * ahead, transform.position.y + by * ahead}, Fixed::FromInt(99999),
			locomotor.minSpeed);
		return false;
	}
	ground_detail::TurnAbout(transform, motion, {transform.position.x + bx * Fixed::FromInt(1000), transform.position.y + by * Fixed::FromInt(1000)},
		MaxTurnRate(motion));
	FlightHeight(transform, motion, step, transform.position.z);
	return false;
}

// Straight up or down where it is (ChinookTakeoffOrLandingState: scrubVelocity2D, then its locomotor's precise height).
inline void FlightVertical(Transform &transform, Locomotion &motion, const FlightStep &step) noexcept
{
	step.body->velocity.x = {};
	step.body->velocity.y = {};
	ApplyMotiveForce(*step.body, {}, step.tick);
	FlightHeight(transform, motion, step, motion.preciseHeight);
}

// Before its locomotor acts: a body new to force flight (its last tick kinematic) takes its motion along its facing,
// level; one whose speed another rule set since its last flown tick (the original's scrubVelocity2D / setVelocity) takes
// that speed along its facing. Otherwise its velocity is its own (physics may have moved it meanwhile: disabled).
inline void BeginFlight(const Transform &transform, Locomotion &motion, PhysicsBody &body) noexcept
{
	if (motion.forced != 0 && motion.speed == motion.flownSpeed)
		return;
	body.velocity.x = Engine::Math::Cos(transform.facing) * motion.speed;
	body.velocity.y = Engine::Math::Sin(transform.facing) * motion.speed;
	if (motion.forced == 0)
		body.velocity.z = {};
}

// PhysicsBehavior::update after the locomotor (its AI update comes first): the body steps over `ground` (the height under
// it), turning its attitude by its pitch and roll rates when it has one; braking (OBJECT_STATUS_BRAKING) only its height
// moves. Its motion is its forward speed (getForwardSpeed2D). Returns the step (its landing, a hard fall).
template<typename Ground>
StepResult EndFlight(Transform &transform, Locomotion &motion, const FlightStep &step, Ground &&ground, Attitude *attitude = nullptr) noexcept
{
	const StepResult result =
		StepBody(*step.body, transform, attitude, *step.settings, ground, false, flight_detail::Motive(step), motion.brakingStatus != 0);
	motion.speed = ForwardSpeed2D(*step.body, transform.facing);
	motion.flownSpeed = motion.speed;
	motion.forced = 1;
	return result;
}
}
