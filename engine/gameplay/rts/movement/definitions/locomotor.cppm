export module engine.gameplay.rts.movement.definitions.locomotor;
import std;

export import Engine.Core.Math.FixedVector;

// How a kind of unit moves (bound from config by the game). Rates are per
// simulation tick: speed in world units per tick, acceleration and braking
// in units per tick squared, turn rate as an angle per tick.
export namespace engine::gameplay
{
enum class LocomotorAppearance : std::uint8_t
{
	TwoLegs,
	FourWheels,
	Treads,
	Hover,
	Thrust,
	Wings,
	Climber,
	Other,
	Motorcycle,
};

// How a locomotor holds its height (the original's ZAxisBehavior).
enum class HeightBehavior : std::uint8_t
{
	NoMotiveForce, // on the ground
	SeaLevel,      // on the water surface, or the ground above it
	SurfaceRelative,
	Absolute,
	FixedSurfaceRelative,
	FixedAbsolute,
	FixedRelativeToGroundAndBuildings,
	RelativeToHighestLayer,
};

struct LocomotorDefinition
{
	Engine::Math::Fixed maxSpeed;
	Engine::Math::Fixed minSpeed;
	Engine::Math::Fixed acceleration;
	Engine::Math::Fixed braking;
	Engine::Math::TurnAngle turnRate;
	// THRUST: how far off its nose it may push (MaxThrustAngle).
	Engine::Math::TurnAngle maxThrustAngle;
	// Height above ground an airborne unit holds; zero stays on the ground.
	Engine::Math::Fixed preferredHeight;
	// Fraction of the height error closed per tick.
	Engine::Math::Fixed preferredHeightDamping{Engine::Math::Fixed::One()};
	// How close counts as arrived at a final destination.
	Engine::Math::Fixed closeEnough{Engine::Math::Fixed::One()};
	LocomotorAppearance appearance{LocomotorAppearance::Other};
	// The surfaces it moves over (locomotor_surface bits: ground 1, water 2, cliff 4, air 8, rubble 16).
	std::uint8_t surfaces{1};
	HeightBehavior height{HeightBehavior::NoMotiveForce};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
	// Its full and damaged rates (Speed / SpeedDamaged, Acceleration / AccelerationDamaged, TurnRate / TurnRateDamaged;
	// a damaged one not given is the full one): maxSpeed, acceleration and turnRate are whichever apply now.
	Engine::Math::Fixed speedFull;
	Engine::Math::Fixed speedDamaged;
	Engine::Math::Fixed accelerationFull;
	Engine::Math::Fixed accelerationDamaged;
	Engine::Math::TurnAngle turnRateFull;
	Engine::Math::TurnAngle turnRateDamaged;
	std::uint8_t damagedGiven{0}; // bits: 1 speed, 2 acceleration, 4 turn rate (while reading)
	// Wheels: it may back up (CanMoveBackwards), and the speed it turns at (MinTurnSpeed, per tick; at least a quarter
	// of its speed).
	bool canMoveBackward{false};
	std::uint8_t reserved2[6]{};
	Engine::Math::Fixed minTurnSpeed;
	// Weaving from side to side on its way (legs: WanderWidthFactor, how far off its goal it swings, pi/8 each; 0:
	// straight; WanderLengthFactor, how slowly it swings).
	Engine::Math::Fixed wanderWidth;
	Engine::Math::Fixed wanderLength{Engine::Math::Fixed::One()};
	// How far about its point a unit wandering in place goes (WanderAboutPointRadius).
	Engine::Math::Fixed wanderAboutPointRadius;
};

// The rates it starts on (full), its damaged ones completed with the full ones where not given.
inline void SettleLocomotorRates(LocomotorDefinition &definition) noexcept
{
	definition.speedFull = definition.maxSpeed;
	definition.accelerationFull = definition.acceleration;
	definition.turnRateFull = definition.turnRate;
	if ((definition.damagedGiven & 1u) == 0)
		definition.speedDamaged = definition.maxSpeed;
	if ((definition.damagedGiven & 2u) == 0)
		definition.accelerationDamaged = definition.acceleration;
	if ((definition.damagedGiven & 4u) == 0)
		definition.turnRateDamaged = definition.turnRate;
}

inline bool IsAirborne(const LocomotorDefinition &definition) noexcept
{
	return definition.height != HeightBehavior::NoMotiveForce && definition.height != HeightBehavior::SeaLevel;
}
}
