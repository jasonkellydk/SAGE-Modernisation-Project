export module engine.gameplay.common.weapons.definitions.weapon;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.common.spatial.components.targetable;

// What a weapon does (bound from config by the game). Distances in world
// units, speed in units per tick (zero: the hit lands the tick it fires),
// delays in ticks. `cue` is the game's handle for its presentation (fire
// and detonation effects); the simulation never looks at it.
export namespace engine::gameplay
{
namespace weapon_anti
{
inline constexpr std::uint32_t Ground = 1u << 0;
inline constexpr std::uint32_t AirborneVehicle = 1u << 1;
inline constexpr std::uint32_t AirborneInfantry = 1u << 2;
inline constexpr std::uint32_t Projectile = 1u << 3;
inline constexpr std::uint32_t SmallMissile = 1u << 4;
inline constexpr std::uint32_t Mine = 1u << 5;
inline constexpr std::uint32_t Parachute = 1u << 6;
inline constexpr std::uint32_t BallisticMissile = 1u << 7;
}

namespace weapon_affects
{
inline constexpr std::uint32_t Self = 1u << 0;
inline constexpr std::uint32_t Allies = 1u << 1;
inline constexpr std::uint32_t Enemies = 1u << 2;
inline constexpr std::uint32_t Neutrals = 1u << 3;
inline constexpr std::uint32_t Suicide = 1u << 4;
inline constexpr std::uint32_t NotSimilar = 1u << 5;
inline constexpr std::uint32_t NotAirborne = 1u << 6;
}

// What a weapon's projectile runs into on its way (ProjectileCollidesWith).
namespace weapon_collides
{
inline constexpr std::uint32_t Allies = 1u << 0;
inline constexpr std::uint32_t Enemies = 1u << 1;
inline constexpr std::uint32_t Structures = 1u << 2;
inline constexpr std::uint32_t Shrubbery = 1u << 3;
inline constexpr std::uint32_t Projectiles = 1u << 4;
inline constexpr std::uint32_t Walls = 1u << 5;
inline constexpr std::uint32_t SmallMissiles = 1u << 6;
inline constexpr std::uint32_t BallisticMissiles = 1u << 7;
inline constexpr std::uint32_t ControlledStructures = 1u << 8;
}

// A lobbed projectile's arc (its projectile object's DumbProjectileBehavior): the inner Bezier
// points `indent` of the way along the line, `height` above the highest terrain between.
struct ProjectileArc
{
	Engine::Math::Fixed firstHeight;
	Engine::Math::Fixed secondHeight;
	Engine::Math::Fixed firstIndent;  // of the way along the line, 0..1
	Engine::Math::Fixed secondIndent;
	bool orientToPath{false};         // OrientToFlightPath: faces along its path
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
	Engine::Math::Fixed followPerTick; // FlightPathAdjustDistPerSecond, per tick (zero: the end stays put)
	std::uint64_t maxLifespan{300};    // MaxLifespan in ticks (DEFAULT_MAX_LIFESPAN: 10 s): it goes off where it is then
	bool tumble{false};                // TumbleRandomly: spins at random rates instead of facing along its path
	std::uint8_t garrisonHitKill{0};   // GarrisonHitKillCount: a garrison it runs into loses up to this many instead
	// DetonateCallsKill (DumbProjectileBehavior::detonate): gone off, it is killed (unresistable, its most health,
	// DEATH_DETONATED, by no one) so its die modules run, instead of being taken away.
	std::uint8_t callsKill{0};
	std::uint8_t reserved2[5]{};
	std::uint32_t garrisonHitRequired{0};  // GarrisonHitKillRequiredKindOf, as target classes
	std::uint32_t garrisonHitForbidden{0}; // GarrisonHitKillForbiddenKindOf, as target classes
};

// A superweapon missile flying itself (NeutronMissileUpdate), see NeutronFlight.
struct NeutronMissileDefinition
{
	Engine::Math::Fixed initialDistance;       // DistanceToTravelBeforeTurning
	Engine::Math::TurnAngle maxTurnRate{Engine::Math::TurnFromDegrees(Engine::Math::Fixed::FromInt(179))}; // per tick
	Engine::Math::Fixed forwardDamping;        // ForwardDamping
	Engine::Math::Fixed relativeSpeed{Engine::Math::Fixed::One()}; // RelativeSpeed (per tick, per tick)
	Engine::Math::Fixed targetFromAbove;       // TargetFromDirectlyAbove
	std::uint64_t specialSpeedTicks{0};        // SpecialSpeedTime
	Engine::Math::Fixed specialSpeedHeight;    // SpecialSpeedHeight
	Engine::Math::Fixed specialAccelFactor{Engine::Math::Fixed::One()}; // SpecialAccelFactor
	Engine::Math::Fixed boundingRadius;        // its bounding sphere (reaching the point above its target)
	std::uint32_t launchEffect{0xFFFFFFFFu};   // LaunchFX
	std::uint32_t ignitionEffect{0xFFFFFFFFu}; // IgnitionFX
};

// A guided missile's flight (its projectile object's MissileAIUpdate, with its THRUST locomotor and
// geometry): per-tick speeds and rates, ticks for the delays.
struct MissileFlightDefinition
{
	bool followsTarget{true};          // TryToFollowTarget
	std::uint64_t fuelTicks{0};        // FuelLifetime (0: never runs out)
	std::uint64_t ignitionTicks{0};    // IgnitionDelay
	Engine::Math::Fixed initialSpeed;  // InitialVelocity
	Engine::Math::Fixed noTurnDistance; // DistanceToTravelBeforeTurning
	Engine::Math::Fixed diveDistance;  // DistanceToTargetBeforeDiving
	Engine::Math::Fixed lockDistance{Engine::Math::Fixed::FromInt(75)}; // DistanceToTargetForLock
	bool useWeaponSpeed{false};
	bool detonateOnNoFuel{false};
	// DetonateCallsKill (MissileAIUpdate::doKillSelfState): at the end of its KILL_SELF hold it is killed (Object::kill:
	// unresistable, its most health, DEATH_NORMAL) so its die modules run, instead of being taken away.
	bool detonateCallsKill{false};
	// GarrisonHitKillCount (MissileAIUpdate::projectileHandleCollision): run into a garrison holding someone, it kills up
	// to this many riders of GarrisonHitKillRequiredKindOf (all of them) and none of GarrisonHitKillForbiddenKindOf, and
	// is gone without detonating; killing none, it detonates as usual.
	std::uint8_t garrisonHitKill{0};
	std::uint32_t garrisonHitRequired{0};
	std::uint32_t garrisonHitForbidden{0};
	std::uint64_t killSelfTicks{3};    // KillSelfDelay
	Engine::Math::Fixed jamScatter{Engine::Math::Fixed::FromInt(75)}; // DistanceScatterWhenJammed
	// Its locomotor.
	Engine::Math::Fixed maxSpeed;
	Engine::Math::Fixed minSpeed;
	Engine::Math::Fixed acceleration;
	Engine::Math::Fixed braking;
	Engine::Math::TurnAngle turnRate;
	Engine::Math::TurnAngle maxThrustAngle;
	Engine::Math::Fixed preferredHeight;
	Engine::Math::Fixed preferredHeightDamping{Engine::Math::Fixed::One()};
	Engine::Math::Fixed radius{Engine::Math::Fixed::One()}; // GeometryMajorRadius
};

// How many ScatterTarget offsets a weapon may have (a 64-bit mask records which a clip has used).
inline constexpr std::uint32_t ScatterTargetMax = 64;

struct WeaponDefinition
{
	Engine::Math::Fixed primaryDamage;
	Engine::Math::Fixed primaryRadius;
	Engine::Math::Fixed secondaryDamage;
	Engine::Math::Fixed secondaryRadius;
	Engine::Math::Fixed attackRange;
	Engine::Math::Fixed minimumRange;
	Engine::Math::Fixed speed;
	Engine::Math::Fixed scatterRadius;
	// ScatterRadiusVsInfantry (m_infantryInaccuracyDist): added to the scatter when the victim is infantry.
	Engine::Math::Fixed infantryScatter;
	// ScatterTarget / ScatterTargetScalar (Weapon::privateFireWeapon): a pattern of offsets (scaled by the scalar) that a
	// clip's shots aim at, each once, in a random order: the pattern's `scatterCount` entries from `scatterFirst` in the
	// catalog's scatterTargets (none: 0). ScatterTargetMax at most (the shipped weapons have up to 20).
	Engine::Math::Fixed scatterTargetScalar;
	std::uint32_t scatterFirst{0};
	std::uint32_t scatterCount{0};
	// ShockWaveAmount, ShockWaveRadius, ShockWaveTaperOff: the push its radius damage gives (none: 0), how far it
	// reaches, and the share left of it at that edge.
	Engine::Math::Fixed shockWaveAmount;
	Engine::Math::Fixed shockWaveRadius;
	Engine::Math::Fixed shockWaveTaperOff;
	Engine::Math::TurnAngle aimDelta;
	// MinTargetPitch / MaxTargetPitch (Weapon::isWithinTargetPitch): the pitches from its firer up or down to a victim it
	// may target, in signed turn units (not given: -PI and PI, no limit).
	std::int32_t minTargetPitch{std::numeric_limits<std::int32_t>::min()};
	std::int32_t maxTargetPitch{std::numeric_limits<std::int32_t>::max()};
	// HistoricBonusTime / Radius / Count / Weapon (WeaponTemplate::processHistoricDamage): when this many of its hits
	// (this one included) land within the time and 2D radius of each other, the bonus weapon fires there (Count 0: none).
	std::uint64_t historicBonusTicks{0};
	Engine::Math::Fixed historicBonusRadius;
	std::uint32_t historicBonusCount{0};
	std::uint32_t historicBonusWeapon{0xFFFFFFFFu};
	std::uint32_t damageType{0};
	// How the victim dies (the game's death type index: normal, burned,
	// exploded, crushed, ...); die behaviours are chosen by it.
	std::uint32_t deathType{0};
	// DamageStatusType: the object status bit STATUS damage gives (None: none).
	std::uint32_t damageStatusType{0xFFFFFFFFu};
	std::uint64_t delayMin{0};
	std::uint64_t delayMax{0};
	std::uint32_t clipSize{0}; // 0: never reloads
	std::uint64_t clipReload{0};
	// FiringTracker's continuous fire: more than ContinuousFireOne consecutive shots at a victim speed it up
	// (CONTINUOUS_FIRE_MEAN), more than ContinuousFireTwo up again (CONTINUOUS_FIRE_FAST); ContinuousFireCoast ticks past
	// its next possible shot without firing it cools down (0: never). FireSoundLoopTime (ticks): its fire sound is a loop
	// kept going while it fires within that long of the last shot (0: a sound a shot).
	std::uint32_t continuousFireOne{0x7FFFFFFFu}; // INT_MAX when not given: never
	std::uint32_t continuousFireTwo{0x7FFFFFFFu};
	std::uint64_t continuousFireCoast{0};
	std::uint64_t fireSoundLoopTicks{0};
	// AutoReloadWhenIdle (ticks): not fired this long, all its weapons load their clips at once (0: never).
	std::uint64_t autoReloadIdleTicks{0};
	// An emptied clip reloads only back at base (jets: the original's AutoReloadsClip = RETURN_TO_BASE).
	bool reloadsAtBase{false};
	// AutoReloadsClip = No (NO_RELOAD): an emptied clip never reloads (OUT_OF_AMMO for good).
	bool noReload{false};
	// LeechRangeWeapon: once it fires (or begins its wind-up) at a victim, it reaches any distance for the rest of that attack.
	bool leechRange{false};
	// AllowAttackGarrisonedBldgs: reckoned to hurt a garrisoned building (one that can be cleared) whatever its armor.
	bool allowAttackGarrisoned{false};
	// ScaleWeaponSpeed / MinWeaponSpeed (DumbProjectileBehavior::projectileFireAtObjectOrPosition): a lobbed projectile's
	// speed scaled with its range, from MinWeaponSpeed at MinimumAttackRange to WeaponSpeed at AttackRange (a tick's travel).
	bool scaleWeaponSpeed{false};
	Engine::Math::Fixed minWeaponSpeed{Engine::Math::Fixed::FromRatio(999999, 30)};
	// SuspendFXDelay (ticks): a weapon shows no FX until this long after it is made (Weapon::m_suspendFXFrame).
	std::uint64_t suspendFxTicks{0};
	// RadiusDamageAngle (Weapon::dealDamageInternal): its radius damage only reaches those within this angle either side
	// of its firer's facing (the cosine of it; `coned` false: every way, the default PI).
	bool coned{false};
	Engine::Math::Fixed coneCosine{-Engine::Math::Fixed::One()};
	// MissileCallsOnDie (MissileAIUpdate::detonate): its missile, having gone off, is killed (DAMAGE_UNRESISTABLE,
	// DEATH_DETONATED, its most health) so its die modules run, rather than just taking itself away.
	bool missileCallsOnDie{false};
	// ContinueAttackRange: its victim gone (or a mine spent), it goes on to the closest of that player's it may attack this
	// near where the victim stood; and until it first fires in an attack, it sees through stealth (IGNORING_STEALTH).
	Engine::Math::Fixed continueAttackRange;
	// ShotsPerBarrel: how many shots each barrel fires before the next takes over.
	std::uint32_t shotsPerBarrel{1};
	// How long it winds up before it fires (PreAttackDelay), and when: before every shot, before the first of each
	// clip, or before the first at each victim (PreAttackType PER_SHOT, PER_CLIP, PER_ATTACK).
	std::uint64_t preAttackDelay{0};
	enum class PreAttack : std::uint8_t
	{
		PerShot,
		PerAttack,
		PerClip,
	};
	PreAttack preAttackType{PreAttack::PerShot};
	// Firing, it asks its firer's own kind within this range to join in (RequestAssistRange; 0: never).
	Engine::Math::Fixed requestAssistRange;
	std::uint32_t anti{weapon_anti::Ground};
	std::uint32_t affects{weapon_affects::Allies | weapon_affects::Enemies | weapon_affects::Neutrals};
	bool damageAtSelf{false};
	bool projectile{false};
	// Its projectile flies as an object along an arc (lands when the arc ends); `projectileDefinition`
	// is the game's definition of that object.
	bool lobbed{false};
	ProjectileArc arc;
	// Its projectile is a guided missile flying as an object (lands where it detonates); `smallMissile`: that object is
	// KINDOF_SMALL_MISSILE (countermeasures see it coming).
	bool guided{false};
	bool smallMissile{false};
	MissileFlightDefinition missile;
	// Its projectile flies itself as a full object (the game makes it and flies it: a superweapon's NeutronMissileUpdate);
	// the shot lands with it (its detonation is the object's death).
	bool objectFlown{false};
	NeutronMissileDefinition neutron;
	std::uint32_t projectileDefinition{0};
	// What its projectile runs into (the original's default: others' structures), and its projectile's radius.
	std::uint32_t collides{weapon_collides::Structures};
	Engine::Math::Fixed projectileRadius{Engine::Math::Fixed::One()};
	std::uint32_t cue{0};
	// Its own weapon bonus table (WeaponBonus lines in its Weapon block), an index into the catalog's; none: 0xFFFFFFFF.
	std::uint32_t extraBonus{0xFFFFFFFFu};
};

// Whether the weapon may aim at a target of these classes (target_class
// bits, with this tick's airborne/ground classification).
inline bool CanTarget(const WeaponDefinition &weapon, std::uint32_t classes) noexcept
{
	// As WeaponSet's getVictimAntiMask: what a projectile is comes before whether it flies.
	if ((classes & target_class::SmallMissile) != 0)
		return (weapon.anti & weapon_anti::SmallMissile) != 0;
	if ((classes & target_class::BallisticMissile) != 0)
		return (weapon.anti & weapon_anti::BallisticMissile) != 0;
	if ((classes & target_class::Projectile) != 0)
		return (weapon.anti & weapon_anti::Projectile) != 0;
	if ((classes & target_class::Mine) != 0)
		return (weapon.anti & (weapon_anti::Mine | weapon_anti::Ground)) != 0;
	if ((classes & target_class::AirborneVehicle) != 0)
		return (weapon.anti & weapon_anti::AirborneVehicle) != 0;
	if ((classes & target_class::AirborneInfantry) != 0)
		return (weapon.anti & weapon_anti::AirborneInfantry) != 0;
	if ((classes & target_class::Ground) != 0)
		return (weapon.anti & weapon_anti::Ground) != 0;
	return false;
}
}
