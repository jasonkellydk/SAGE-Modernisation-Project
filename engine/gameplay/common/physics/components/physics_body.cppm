export module engine.gameplay.common.physics.components.physics_body;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// A body moved by forces rather than by a locomotor: gravity, friction,
// bouncing off the ground and tumbling (flung debris, wrecks, crates).
// Velocity is per tick and acceleration per tick squared; forces accumulate
// into `acceleration` (a = F / m) and are used up by the next step. Turn
// rates are signed turn units per tick, scaled by `rateFactor` when applied
// (the original's PitchRollYawFactor). Landing from a steep fall faster than
// `minFallSpeed` hurts: (speed - minFallSpeed) * mass * `fallDamageFactor`.
export namespace engine::gameplay
{
namespace physics_flag
{
inline constexpr std::uint32_t AllowBouncing = 1u << 0;
// Bouncing as authored (a bounce that no longer pushes back restores it).
inline constexpr std::uint32_t AuthoredBouncing = 1u << 1;
// May be above the ground without being pulled down onto it at once.
inline constexpr std::uint32_t AllowToFall = 1u << 2;
inline constexpr std::uint32_t StickToGround = 1u << 3;
// Killed once it lies still on the ground (debris, as the original).
inline constexpr std::uint32_t KillWhenResting = 1u << 4;
// Was above the ground at the end of the last step (a landing may hurt).
inline constexpr std::uint32_t WasAirborne = 1u << 5;
inline constexpr std::uint32_t ImmuneToFalling = 1u << 6;
// Moved by its locomotor while its AI runs (the locomotor's forces hold it); physics alone moves it once
// its AI stops (disabled: the original runs AIUpdate only while HELD, PhysicsBehavior always).
inline constexpr std::uint32_t Locomotive = 1u << 7;
// KINDOF_DRONE: at rest on the ground it is killed only when dead or unmanned (PhysicsBehavior::update).
inline constexpr std::uint32_t Drone = 1u << 8;
// Its locomotor's Apply2DFrictionWhenAirborne: ground friction even well off the ground (PhysicsBehavior's
// APPLY_FRICTION2D_WHEN_AIRBORNE).
inline constexpr std::uint32_t AirborneFriction = 1u << 9;
// Falling helplessly (PhysicsBehavior's IS_IN_FREEFALL: a rider whose parachute was lost aloft), until it lands.
inline constexpr std::uint32_t InFreeFall = 1u << 10;
// Pushed off its locomotor's path by a shock wave (Object::attemptDamage's applyShock): stepped by physics, its
// locomotor idle, until it lands again (the original runs physics under every locomotor).
inline constexpr std::uint32_t Pushed = 1u << 11;
}

struct PhysicsBody
{
	Engine::Math::FixedVector3 velocity;
	Engine::Math::FixedVector3 acceleration;
	Engine::Math::Fixed mass{Engine::Math::Fixed::One()};
	// Per tick (the original's defaults: 0.15 forward and lateral, none in the air).
	Engine::Math::Fixed forwardFriction{Engine::Math::Fixed::FromRatio(15, 100)};
	Engine::Math::Fixed lateralFriction{Engine::Math::Fixed::FromRatio(15, 100)};
	Engine::Math::Fixed aerodynamicFriction;
	Engine::Math::Fixed extraFriction;
	Engine::Math::Fixed rateFactor{Engine::Math::Fixed::FromInt(2)};
	Engine::Math::Fixed minFallSpeed;     // per tick
	Engine::Math::Fixed fallDamageFactor; // none: never hurt by falling
	std::int32_t yawRate{0};
	std::int32_t pitchRate{0};
	std::int32_t rollRate{0};
	std::uint32_t flags{0};
	// Driven by a locomotor's force until this tick (PhysicsBehavior::applyMotiveForce: a third of a second); while
	// driven, no forward friction.
	std::uint64_t motiveUntil{0};
	// PhysicsBehavior's ShockResistance (the share of a shock wave it shrugs off) and ShockMaxYaw / Pitch / Roll (the
	// most a shock adds to each turn rate, turn units per tick; the original's defaults 0.05, 0.025 and 0.025 radians).
	Engine::Math::Fixed shockResistance;
	std::int32_t shockMaxYaw{static_cast<std::int32_t>(Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(5, 100)).units)};
	std::int32_t shockMaxPitch{static_cast<std::int32_t>(Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(25, 1000)).units)};
	std::int32_t shockMaxRoll{static_cast<std::int32_t>(Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(25, 1000)).units)};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
	void Set(std::uint32_t flag, bool on) noexcept { flags = on ? (flags | flag) : (flags & ~flag); }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PhysicsBody>
{
	static constexpr std::string_view StableName = "engine.gameplay.physics_body";
	static constexpr std::uint32_t Version = 4;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
