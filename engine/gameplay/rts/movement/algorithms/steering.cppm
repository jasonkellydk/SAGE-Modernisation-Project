export module engine.gameplay.rts.movement.algorithms.steering;
import std;

export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.movement.components.locomotion;

// One tick of steering toward a goal: turn toward it at most by the turn
// rate, pick a speed that can still stop at a final goal and slows with the
// heading error, accelerate or brake toward it, move forward. Airborne units
// ease toward their preferred height; wings never drop below their minimum
// speed. All fixed point, so every platform steps identically.
export namespace engine::gameplay
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::TurnAngle;

// Radius a unit turns on at its current speed (world units).
inline Fixed TurnRadius(const Locomotion &motion) noexcept
{
	const Fixed perTick = Engine::Math::Radians(motion.locomotor.turnRate);
	return perTick > Fixed{} ? motion.speed / perTick : Fixed{};
}

// How close counts as reaching a goal: final goals use the locomotor's
// close-enough distance (AIInternalMoveToState::update: getLocomotorDistanceToGoal < getCloseEnoughDist, wings
// included); waypoints on the way are passed within a cell or two ticks of travel, wings within their turning circle.
inline Fixed ArrivalDistance(const Locomotion &motion, bool final) noexcept
{
	const auto &locomotor = motion.locomotor;
	Fixed distance = final ? std::max(locomotor.closeEnough, motion.speed) : std::max(Fixed::FromInt(10), motion.speed * 2);
	if (!final && locomotor.appearance == LocomotorAppearance::Wings)
		distance = std::max(distance, TurnRadius(motion));
	return distance;
}

inline Fixed Approach(Fixed value, Fixed target, Fixed up, Fixed down) noexcept
{
	if (value < target)
		return up > Fixed{} ? std::min(target, value + up) : target;
	return down > Fixed{} ? std::max(target, value - down) : target;
}

// Height for this tick from the ground and the surface (the higher of
// ground and water) under the unit.
inline void HoldHeight(Transform &transform, const Locomotion &motion, Fixed ground, Fixed surface) noexcept
{
	const auto &locomotor = motion.locomotor;
	Fixed target;
	switch (locomotor.height)
	{
	case HeightBehavior::NoMotiveForce:
		transform.position.z = ground;
		return;
	case HeightBehavior::SeaLevel:
		transform.position.z = surface;
		return;
	case HeightBehavior::Absolute:
	case HeightBehavior::FixedAbsolute:
		target = locomotor.preferredHeight;
		break;
	default:
		target = surface + locomotor.preferredHeight;
		break;
	}
	transform.position.z += (target - transform.position.z) * std::clamp(locomotor.preferredHeightDamping, Fixed{}, Fixed::One());
}

// Parked: wings on the ground with no order and no speed.
inline bool IsParked(const Locomotion &motion, bool idle) noexcept
{
	return idle && motion.locomotor.appearance == LocomotorAppearance::Wings && motion.speed <= Fixed{};
}

// No goal: slow to a stop (flying wings keep flying and circle).
inline void Coast(Transform &transform, Locomotion &motion) noexcept
{
	const auto &locomotor = motion.locomotor;
	if (locomotor.appearance == LocomotorAppearance::Wings)
	{
		motion.speed = Approach(motion.speed, std::max(locomotor.minSpeed, motion.speed), locomotor.acceleration, locomotor.braking);
		transform.facing += TurnAngle{locomotor.turnRate.units / 2};
	}
	else
		motion.speed = Approach(motion.speed, Fixed{}, locomotor.acceleration, locomotor.braking);
	transform.position += Engine::Math::FixedVector3{Engine::Math::Cos(transform.facing) * motion.speed,
		Engine::Math::Sin(transform.facing) * motion.speed, Fixed{}};
}

// AIFaceState::update's REL_THRESH: 0.035 radians (about 2 degrees) off the goal counts as facing it, in turn units
// (0.035 / 2pi of 2^32, rounded).
inline constexpr std::int32_t FacingThreshold = 23924785;

inline bool Steer(Transform &transform, Locomotion &motion, FixedVector2 goal, bool final) noexcept;

// AIFaceState::update, one tick: done (returns true) once facing the goal within REL_THRESH (a unit on the goal
// faces it already: getRelativeAngle2D is 0); else a locomotor that may turn in place (MinSpeed 0) turns toward it by
// its turn rate while it slows (locoUpdate_moveTowardsAngle: rotateTowardsPosition, no motive force), another heads
// for it (setLocomotorGoalPositionExplicit).
inline bool Face(Transform &transform, Locomotion &motion, FixedVector2 goal) noexcept
{
	const auto &locomotor = motion.locomotor;
	const FixedVector2 toGoal = goal - transform.position.XY();
	if (toGoal.x == Fixed{} && toGoal.y == Fixed{})
		return true;
	const std::int32_t wanted = Engine::Math::DeltaTo(transform.facing, Engine::Math::Heading(toGoal));
	if (wanted > -FacingThreshold && wanted < FacingThreshold)
		return true;
	if (locomotor.minSpeed > Fixed{})
	{
		Steer(transform, motion, goal, false);
		return false;
	}
	const auto limit = static_cast<std::int64_t>(locomotor.turnRate.units);
	const std::int64_t turn = std::clamp<std::int64_t>(wanted, -limit, limit);
	motion.turning = static_cast<std::int8_t>(wanted > limit ? 1 : wanted < -limit ? -1 : 0);
	transform.facing += TurnAngle{static_cast<std::uint32_t>(turn)};
	motion.speed = Approach(motion.speed, Fixed{}, locomotor.acceleration, locomotor.braking);
	return false;
}

// Returns true once the goal is reached.
inline bool Steer(Transform &transform, Locomotion &motion, FixedVector2 goal, bool final) noexcept
{
	const auto &locomotor = motion.locomotor;
	const FixedVector2 toGoal = goal - transform.position.XY();
	const Fixed distance = Engine::Math::Length(toGoal);
	if (distance <= ArrivalDistance(motion, final))
	{
		if (final && locomotor.appearance != LocomotorAppearance::Wings)
			motion.speed = Approach(motion.speed, Fixed{}, locomotor.acceleration, locomotor.braking);
		return true;
	}

	// Turn toward the goal, limited by the turn rate; a weaving walker aims off it, its offset swinging with how fast it
	// goes between pi/8 x WanderWidthFactor either side (Locomotor::moveTowardsPositionLegs).
	TurnAngle heading = Engine::Math::Heading(toGoal);
	if (locomotor.wanderWidth != Fixed{})
	{
		const std::int64_t limit = locomotor.wanderWidth.Raw() << 12; // pi/8 (2^32 / 16 turn units) x width
		const std::int64_t swing = (motion.wanderStep * motion.speed.Raw()) >> Fixed::FractionBits;
		std::int64_t offset = motion.wanderOffset;
		if (motion.wanderRising != 0)
		{
			offset += swing;
			if (offset > limit)
				motion.wanderRising = 0;
		}
		else
		{
			offset -= swing;
			if (offset < -limit)
				motion.wanderRising = 1;
		}
		motion.wanderOffset = static_cast<std::int32_t>(std::clamp<std::int64_t>(offset, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
		heading += TurnAngle{static_cast<std::uint32_t>(motion.wanderOffset)};
	}
	const std::int32_t wanted = Engine::Math::DeltaTo(transform.facing, heading);
	const auto limit = static_cast<std::int64_t>(locomotor.turnRate.units);
	const std::int64_t turn = std::clamp<std::int64_t>(wanted, -limit, limit);
	// Locomotor::rotateObjAroundLocoPivot: turning when it needs more than its rate allows.
	motion.turning = static_cast<std::int8_t>(wanted > limit ? 1 : wanted < -limit ? -1 : 0);
	transform.facing += TurnAngle{static_cast<std::uint32_t>(turn)};
	const TurnAngle error{static_cast<std::uint32_t>(static_cast<std::int64_t>(wanted) - turn)};

	// Speed: as fast as allowed, but able to stop at a final goal and slowed
	// by how far off the heading still is.
	Fixed target = locomotor.maxSpeed;
	if (final && locomotor.braking > Fixed{})
		target = std::min(target, Engine::Math::Sqrt(locomotor.braking * distance * 2));
	target = target * std::max(Fixed{}, Engine::Math::Cos(error));
	if (locomotor.appearance == LocomotorAppearance::Wings)
		target = std::max(target, locomotor.minSpeed);
	motion.speed = Approach(motion.speed, target, locomotor.acceleration, locomotor.braking);

	// Move forward; the last step onto a final goal lands on it.
	if (final && motion.speed >= distance && Engine::Math::Cos(error) > Fixed::FromRatio(9, 10))
	{
		transform.position.x = goal.x;
		transform.position.y = goal.y;
		return true;
	}
	transform.position.x += Engine::Math::Cos(transform.facing) * motion.speed;
	transform.position.y += Engine::Math::Sin(transform.facing) * motion.speed;
	return false;
}

// The original's ground locomotors (Locomotor::locoUpdate_moveTowardsPosition and its moveTowardsPosition* by
// appearance), one tick toward `goal` with `onPath` left along its route to where it is going (never less than the
// straight distance): treads, wheels (and motorcycles), legs and the rest ("other", hover, climber). Each turns and
// picks a goal speed its own way, accelerating toward it or braking (by its braking factor); then, as the original's
// physics: braking (IS_BRAKING, OBJECT_STATUS_BRAKING) it does not roll along its facing; having been braking its last
// tick, it slides straight at the goal by its speed (at least a third of a cell a second, never past it). Returns
// true once there (within its arrival distance).
namespace ground_detail
{
inline constexpr std::int64_t QuarterTurn = std::int64_t{1} << 30; // pi/2
inline constexpr std::int64_t EighthTurn = std::int64_t{1} << 29;  // pi/4
inline constexpr std::int64_t Twentieth = 107374182;               // pi/20 in turn units
inline constexpr std::int64_t CellSize = 10;                       // PATHFIND_CELL_SIZE_F
inline constexpr std::int64_t DonutDistance = 4 * CellSize;        // DONUT_DISTANCE
inline constexpr std::int64_t MaxBrakingFactor = 5;                // MAX_BRAKING_FACTOR

inline std::int64_t Magnitude(std::int64_t angle) noexcept { return angle < 0 ? -angle : angle; }

// |angle| / (pi/4), at most 1.
inline Fixed AngleCoefficient(std::int64_t angle) noexcept
{
	const std::int64_t magnitude = Magnitude(angle);
	return magnitude >= EighthTurn ? Fixed::One() : Fixed::FromRaw((magnitude << Fixed::FractionBits) / EighthTurn);
}

// Turn toward a heading by at most `rate` (rotateObjAroundLocoPivot); returns the wanted turn (before turning).
inline std::int32_t TurnToward(Transform &transform, Locomotion &motion, TurnAngle heading, std::int64_t rate) noexcept
{
	const std::int32_t wanted = Engine::Math::DeltaTo(transform.facing, heading);
	const std::int64_t turn = std::clamp<std::int64_t>(wanted, -rate, rate);
	motion.turning = static_cast<std::int8_t>(wanted > rate ? 1 : wanted < -rate ? -1 : 0);
	transform.facing += TurnAngle{static_cast<std::uint32_t>(turn)};
	return wanted;
}

// calcSlowDownDist: ((speed - desired)^2 / braking) / 2, with 5% to spare; none when not faster.
inline Fixed SlowDownDistance(Fixed speed, Fixed desired, Fixed braking) noexcept
{
	const Fixed delta = speed - desired;
	if (delta <= Fixed{} || braking <= Fixed{})
		return {};
	return delta * delta / braking / Fixed::FromInt(2) * Fixed::FromRatio(105, 100);
}

// Maintain goal speed: accelerate by at most its acceleration, brake by at most its braking (times `factor`), never
// past the goal speed (the force needed, no more).
inline void Maintain(Locomotion &motion, Fixed goal, Fixed factor) noexcept
{
	const Fixed delta = goal - motion.speed;
	if (delta == Fixed{})
		return;
	const Fixed step = delta > Fixed{} ? motion.locomotor.acceleration : Fixed{} - factor * motion.locomotor.braking;
	motion.speed += Abs(step) > Abs(delta) ? delta : step;
}

// The braking treads and wheels share: the factor from how far it needs to stop against how far it has left, and
// slowing by its braking (or half of it) when that is more than (three quarters of) what is left.
inline Fixed BrakingGoal(Locomotion &motion, Fixed actual, Fixed slowDownDistance, Fixed onPath) noexcept
{
	if (onPath > Fixed{})
	{
		const Fixed ratio = slowDownDistance / onPath;
		motion.brakingFactor = std::min(ratio * ratio, Fixed::FromInt(MaxBrakingFactor));
	}
	if (slowDownDistance > onPath)
		return std::max(Fixed{}, actual - motion.locomotor.braking);
	if (slowDownDistance > onPath * Fixed::FromRatio(3, 4))
		return std::max(Fixed{}, actual - motion.locomotor.braking / Fixed::FromInt(2));
	return actual;
}
}

inline bool IsGroundLocomotor(const LocomotorDefinition &locomotor) noexcept
{
	switch (locomotor.appearance)
	{
	case LocomotorAppearance::TwoLegs:
	case LocomotorAppearance::FourWheels:
	case LocomotorAppearance::Motorcycle:
	case LocomotorAppearance::Treads:
	case LocomotorAppearance::Other:
	case LocomotorAppearance::Hover:
	case LocomotorAppearance::Climber:
		return true;
	default:
		return false;
	}
}

inline bool SteerGround(Transform &transform, Locomotion &motion, FixedVector2 goal, bool final, Fixed onPath, std::uint64_t tick) noexcept
{
	using namespace ground_detail;
	const auto &locomotor = motion.locomotor;
	const FixedVector2 toGoal = goal - transform.position.XY();
	const Fixed distance = Engine::Math::Length(toGoal);
	if (distance <= ArrivalDistance(motion, final))
	{
		if (final)
		{
			motion.speed = Approach(motion.speed, Fixed{}, locomotor.acceleration, locomotor.braking);
			motion.braking = 0;
			motion.brakingStatus = 0;
		}
		return true;
	}
	onPath = std::max(onPath, distance);
	const bool wasBraking = motion.brakingStatus != 0;
	motion.turning = 0;
	const Fixed desired = locomotor.maxSpeed;
	const TurnAngle heading = Engine::Math::Heading(toGoal);
	const auto rate = static_cast<std::int64_t>(locomotor.turnRate.units);
	switch (locomotor.appearance)
	{
	case LocomotorAppearance::Treads:
	{
		// moveTowardsPositionTreads: turn toward it; the more it has to turn the slower (at pi/4 off, none); within two
		// cells and not lined up, slowing to 0.6 of its speed; braking once it needs longer to stop than it has left.
		const Fixed coefficient = AngleCoefficient(TurnToward(transform, motion, heading, rate));
		Fixed goalSpeed = (Fixed::One() - coefficient) * desired;
		const Fixed actual = motion.speed;
		const Fixed slowDownTime = locomotor.braking > Fixed{} ? actual / locomotor.braking : Fixed{};
		const Fixed slowDownDistance = actual / Fixed::FromRatio(3, 2) * slowDownTime;
		if (distance * distance < Fixed::FromInt(4 * CellSize * CellSize) && coefficient > Fixed::FromRatio(5, 100))
			goalSpeed = actual * Fixed::FromRatio(6, 10);
		if (onPath < slowDownDistance && motion.braking == 0)
		{
			motion.braking = 1;
			motion.brakingFactor = Fixed::FromRatio(11, 10);
		}
		if (onPath > Fixed::FromInt(CellSize) && onPath > slowDownDistance * Fixed::FromInt(2))
			motion.braking = 0;
		if (motion.braking != 0)
			goalSpeed = BrakingGoal(motion, actual, slowDownDistance, onPath);
		Maintain(motion, goalSpeed, motion.braking != 0 ? motion.brakingFactor : Fixed::One());
		break;
	}
	case LocomotorAppearance::FourWheels:
	case LocomotorAppearance::Motorcycle:
	{
		// moveTowardsPositionWheels: it turns only while moving (fully at its turn speed, at least a quarter of its
		// speed); turning more than pi/20 it goes no faster than that; standing, one that may back up with its goal behind
		// it backs up (a three point turn when its goal is more than five radii away, else straight back at it); braking
		// as it nears; and within four cells of its goal for more than 2.5 seconds, it brakes (no driving in circles).
		const Fixed turnSpeed = std::max(locomotor.minTurnSpeed, locomotor.maxSpeed / Fixed::FromInt(4));
		TurnAngle wantedHeading = heading;
		std::int64_t relative = Engine::Math::DeltaTo(transform.facing, wantedHeading);
		bool moveBackwards = false;
		if (motion.speed == Fixed{})
		{
			motion.backwards = 0;
			if (locomotor.canMoveBackward && Magnitude(relative) > QuarterTurn)
			{
				motion.backwards = 1;
				motion.threePointTurn = onPath > motion.majorRadius * Fixed::FromInt(5) ? 1 : 0;
			}
		}
		if (motion.backwards != 0)
		{
			if (Magnitude(relative) < QuarterTurn)
				motion.backwards = 0;
			else
			{
				moveBackwards = true;
				motion.threePointTurn = onPath > motion.majorRadius * Fixed::FromInt(5) ? 1 : 0;
				if (motion.threePointTurn == 0)
				{
					wantedHeading += TurnAngle{0x80000000u};
					relative = Engine::Math::DeltaTo(transform.facing, wantedHeading);
				}
			}
		}
		Fixed goalSpeed = desired;
		if (Magnitude(relative) > Twentieth)
			goalSpeed = std::min(goalSpeed, turnSpeed);
		const Fixed actual = moveBackwards ? Fixed{} - motion.speed : motion.speed;
		const Fixed slowDownTime = (locomotor.braking > Fixed{} ? actual / locomotor.braking : Fixed{}) + Fixed::One();
		const Fixed slowDownDistance = actual / Fixed::FromRatio(3, 2) * slowDownTime + actual;
		const Fixed effective = std::max(slowDownDistance, Fixed::FromInt(CellSize));
		// (Its look ahead for impassable ground while turning, validMovementTerrain, is not ported.)
		if (onPath < effective && motion.braking == 0)
		{
			motion.braking = 1;
			motion.brakingFactor = Fixed::FromRatio(11, 10);
		}
		if (onPath > Fixed::FromInt(CellSize) && onPath > slowDownDistance * Fixed::FromInt(2))
			motion.braking = 0;
		if (onPath > Fixed::FromInt(DonutDistance))
			motion.donutTimer = tick + 75; // DONUT_TIME_DELAY_SECONDS (2.5) x LOGICFRAMES_PER_SECOND
		else if (motion.donutTimer < tick)
			motion.braking = 1;
		if (motion.braking != 0)
		{
			goalSpeed = BrakingGoal(motion, actual, slowDownDistance, onPath);
			motion.brakingFactor = Fixed::One();
		}
		// Its turn: its turn rate by how much of its turn speed it has.
		const Fixed turnFactor = turnSpeed > Fixed{} ? std::min(Fixed::One(), Abs(actual) / turnSpeed) : Fixed::One();
		const auto turnAmount = static_cast<std::int64_t>((turnFactor * Fixed::FromInt(rate)).Floor());
		TurnToward(transform, motion, wantedHeading, turnAmount);
		const Fixed factor = motion.braking != 0 ? motion.brakingFactor : Fixed::One();
		if (moveBackwards)
		{
			// Backing up: faster backward is more negative speed.
			const Fixed delta = actual - goalSpeed;
			if (delta != Fixed{})
			{
				const Fixed step = delta < Fixed{} ? Fixed{} - locomotor.acceleration : factor * locomotor.braking;
				motion.speed += Abs(step) > Abs(delta) ? (step < Fixed{} ? Fixed{} - Abs(delta) : Abs(delta)) : step;
			}
		}
		else
			Maintain(motion, goalSpeed, factor);
		break;
	}
	case LocomotorAppearance::TwoLegs:
	{
		// moveTowardsPositionLegs: turn toward it (weaving off it), then the slower the more it is still off (none at
		// pi/4); down to its least speed when it needs longer to stop than it has left.
		TurnAngle aim = heading;
		if (locomotor.wanderWidth != Fixed{})
		{
			const std::int64_t limit = locomotor.wanderWidth.Raw() << 12;
			const std::int64_t swing = (motion.wanderStep * motion.speed.Raw()) >> Fixed::FractionBits;
			std::int64_t offset = motion.wanderOffset;
			if (motion.wanderRising != 0)
			{
				offset += swing;
				if (offset > limit)
					motion.wanderRising = 0;
			}
			else
			{
				offset -= swing;
				if (offset < -limit)
					motion.wanderRising = 1;
			}
			motion.wanderOffset = static_cast<std::int32_t>(std::clamp<std::int64_t>(offset, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
			aim += TurnAngle{static_cast<std::uint32_t>(motion.wanderOffset)};
		}
		TurnToward(transform, motion, aim, rate);
		const Fixed coefficient = AngleCoefficient(Engine::Math::DeltaTo(transform.facing, aim));
		Fixed goalSpeed = (Fixed::One() - coefficient) * desired;
		if (onPath < SlowDownDistance(motion.speed, locomotor.minSpeed, locomotor.braking))
			goalSpeed = locomotor.minSpeed;
		Maintain(motion, goalSpeed, Fixed::One());
		break;
	}
	default:
	{
		// moveTowardsPositionOther (hover and climbers too, as yet): turn toward it; its full speed, down to its least
		// when it needs longer to stop than it has left.
		TurnToward(transform, motion, heading, rate);
		Fixed goalSpeed = desired;
		if (onPath < SlowDownDistance(motion.speed, locomotor.minSpeed, locomotor.braking))
			goalSpeed = locomotor.minSpeed;
		Maintain(motion, goalSpeed, Fixed::One());
		break;
	}
	}
	motion.brakingStatus = motion.braking;
	// Its last tick braking: straight at the goal by its speed (MIN_VEL: a cell a second at least), never past it.
	if (wasBraking && distance > Fixed{})
	{
		const Fixed step = std::min(std::max(Abs(motion.speed), Fixed::FromRatio(CellSize, 30)), distance);
		transform.position.x += toGoal.x * step / distance;
		transform.position.y += toGoal.y * step / distance;
	}
	// PhysicsBehavior: not braking, it rolls along its facing.
	if (motion.braking == 0)
	{
		transform.position.x += Engine::Math::Cos(transform.facing) * motion.speed;
		transform.position.y += Engine::Math::Sin(transform.facing) * motion.speed;
	}
	return false;
}
}
