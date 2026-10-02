export module engine.gameplay.rts.death.definitions.death_definition;
import std;
export import Engine.Core.Math.FixedVector;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
export import engine.gameplay.rts.movement.algorithms.hover_motion;
import engine.ecs.system.system;

// How a kind of entity dies: its die behaviours, each applying to some death
// types (normal, burned, exploded, ...) and veterancy levels. Some act at the
// moment of death (effects to play, objects to create, being removed at
// once); one slow death is chosen by weighted chance (more likely with big
// overkill when it says so) and runs over time: the body lingers, sinks
// after a delay and is removed later, with effects at its initial, midpoint
// and final phases (or it crashes instead, see CrashDefinition). Effects are ids into the game's tables (FX lists,
// object creation lists, weapons); the engine only reports them.
export namespace engine::gameplay
{
inline constexpr std::uint32_t AllDeathTypes = 0xFFFFFFFFu;
inline constexpr std::uint8_t AllVeterancyLevels = 0xFFu;

struct DeathFilter
{
	std::uint32_t deathTypes{AllDeathTypes}; // bit per death type
	std::uint8_t veterancyLevels{AllVeterancyLevels};
	// DieMuxData's ExemptStatus and RequiredStatus: StatusFlags bits that must all be clear, and all be set.
	std::uint64_t exemptStatus{0};
	std::uint64_t requiredStatus{0};

	// DieMuxData::isDieApplicable.
	bool Applies(std::uint32_t deathType, std::uint32_t veterancy, std::uint64_t status = 0) const noexcept
	{
		return (deathTypes >> (deathType & 31u) & 1u) != 0 && (veterancyLevels >> (veterancy & 7u) & 1u) != 0 && (status & exemptStatus) == 0 &&
			(status & requiredStatus) == requiredStatus;
	}
};

enum class DeathEffectKind : std::uint8_t
{
	Effect,  // presentation: an FX list
	Objects, // an object creation list (debris, hulks, pilots, ...)
	Weapon,  // a weapon fired where the entity dies
	Spawn,   // one object, by name (a crashed helicopter's rubble)
	Sound,   // presentation: a sound where the entity dies
	Loot,    // something the game may leave behind for its killer (a crate), by the game's rules
	Notice,  // something the game is told of the death (a special power completing), by the game's rules
	Replace, // an object that takes its place (a rebuild hole), by name, by the game's rules
	Release, // an upgrade of its own its producer gives up (UpgradeDie), by name, by the game's rules
};
inline constexpr std::size_t DeathEffectKinds = 9;

enum class DeathPhase : std::uint8_t
{
	Initial,
	Midpoint,
	Final,
	// A crash's (see CrashDefinition).
	OnGround,    // died on the ground: played instead of the slow death, removed at once
	Secondary,   // a while after the initial ones, still in the air
	HitGround,   // on hitting the ground
	FinalBlowUp, // a while after hitting the ground, then removed
	Blade,       // a helicopter's blades flying off
	Count,
};

enum class CrashKind : std::uint8_t
{
	None,
	Jet,        // the original's JetSlowDeathBehavior
	Helicopter, // the original's HelicopterSlowDeathBehavior
};

// A crash in place of sinking: a jet shot down in the air keeps flying
// straight on (no more turning), falls with `fallFactor` times gravity and
// spins about its roll axis (the rate scaled by `rollRateDelta` each tick);
// `secondaryDelay` after dying it plays its secondary effects; on hitting
// the ground its hit-ground effects, it starts pitching and slides to a
// halt; `finalDelay` later it blows up and is removed. One that dies on the
// ground just blows up (its on-ground effects) and is removed.
//
// A helicopter spirals down instead: it spins about itself (between its
// least and most spin, changing by `spinStep` every `spinDelay` ticks) and
// orbits, moving on at `spiralSpeed` (damped each tick) along a heading
// turning by `spiralTurnRate`; its blades fly off after `bladeDelayMin` to
// `bladeDelayMax` ticks; it blows up `finalDelay` after hitting the ground.
struct CrashDefinition
{
	CrashKind kind{CrashKind::None};
	std::int32_t rollRate{0};  // turn units per tick
	std::int32_t pitchRate{0}; // turn units per tick, once on the ground
	Engine::Math::Fixed rollRateDelta{Engine::Math::Fixed::One()};
	Engine::Math::Fixed fallFactor{Engine::Math::Fixed::One()};
	std::uint64_t secondaryDelay{0};
	std::uint64_t finalDelay{0};
	// Helicopters.
	std::int32_t spiralTurnRate{0}; // turn units per tick
	std::int32_t minSelfSpin{0};    // turn units per tick
	std::int32_t maxSelfSpin{0};
	std::int32_t spinStep{0};
	std::uint64_t spinDelay{0};
	std::uint64_t bladeDelayMin{0};
	std::uint64_t bladeDelayMax{0};
	Engine::Math::Fixed spiralSpeed; // per tick
	Engine::Math::Fixed spiralDamping{Engine::Math::Fixed::One()};
	// Where its blades come off (BladeBoneName in its model, at rest; none: its origin).
	Engine::Math::FixedVector3 bladeOffset;
	// OCLEjectPilot: as its blades come off a veteran's pilot bails out (EjectPilotDie::ejectPilot: this creation list,
	// with its VoiceEject and SoundEject).
	static constexpr std::uint32_t NoEject = 0xFFFFFFFFu;
	std::uint32_t ejectPilot{NoEject};
	std::array<std::uint32_t, 2> ejectSounds{NoEject, NoEject};
	// Its NORMAL locomotor, which works on while it is dead (LocomotorWorksWhenDead: the spiral is a force on its
	// physics, braked by the locomotor holding its place, lifted short of gravity), and MaxBraking, the braking the
	// spiral lets it use. Without one (`hovering` false) the spiral is flown kinematically.
	HoverLocomotor hover;
	Engine::Math::Fixed maxBraking{Engine::Math::Fixed::FromRatio(99999, 900)};
	bool hovering{false};
};

// One effect of a die behaviour that acts at the moment of death: one of
// the candidates, picked at random.
// An upgrade-switched die module (UpgradeMux: StartsActive, TriggeredBy, ConflictsWith): active from the start
// or once an activating upgrade (any, or every with RequiresAllTriggers) is there; never while an upgrade it
// conflicts with (the object's or its player's) is.
struct DieGate
{
	bool upgradeSwitched{false};
	bool startsActive{false};
	UpgradeMask activation;
	UpgradeMask conflicting;
	bool requiresAll{false};

	bool Active(const UpgradeMask &upgrades) const noexcept
	{
		if (!upgradeSwitched)
			return true;
		if (upgrades.AnyOf(conflicting))
			return false;
		if (startsActive)
			return true;
		if (!activation.Any())
			return false;
		return requiresAll ? upgrades.AllOf(activation) : upgrades.AnyOf(activation);
	}
};

// Where the dying entity must be for an effect (EjectPilotDie: its air or ground creation list, by whether it is
// significantly above the ground).
enum class DieAltitude : std::uint8_t
{
	Any,
	Ground,
	Air,
};

struct DieEffect
{
	DeathFilter filter;
	DeathEffectKind kind{DeathEffectKind::Effect};
	std::vector<std::uint32_t> candidates;
	DieGate gate;
	DieAltitude altitude{DieAltitude::Any};
	// An effect played facing the way the dying entity faced, on it (FXListDie OrientToObject); else unrotated where it was.
	bool orient{true};
	// Objects: what it makes takes over its damage and its attackers (CreateObjectDie TransferPreviousHealth).
	bool transferHealth{false};
};

// Per phase, per kind: the candidates; one of each is picked at random.
struct PhaseEffects
{
	std::array<std::array<std::vector<std::uint32_t>, DeathEffectKinds>, static_cast<std::size_t>(DeathPhase::Count)> candidates{};

	std::vector<std::uint32_t> &Of(DeathPhase phase, DeathEffectKind kind)
	{
		return candidates[static_cast<std::size_t>(phase)][static_cast<std::size_t>(kind)];
	}
	const std::vector<std::uint32_t> &Of(DeathPhase phase, DeathEffectKind kind) const
	{
		return candidates[static_cast<std::size_t>(phase)][static_cast<std::size_t>(kind)];
	}
};

// SlowDeathBehavior's fling: a body with physics is thrown (a force of FlingForce to FlingForce + FlingForceVariance,
// raised FlingPitch to FlingPitch + FlingPitchVariance, in any direction) as it dies.
struct FlingDefinition
{
	Engine::Math::Fixed force;
	Engine::Math::Fixed forceVariance;
	Engine::Math::TurnAngle pitch;
	Engine::Math::TurnAngle pitchVariance;
};

// A missile's blast waves (the original's NeutronMissileSlowDeathBehavior): as it starts dying, `effect` plays on the
// ground under it; then each blast goes off on the first tick more than its delay after it died, and each scorch wave
// on the first tick more than its scorch delay after (blastAfter / scorchAfter: those ticks, counted from its death).
// A blast pushes over everything that topples within its outer radius of the missile (centre to centre, flat) at its
// topple speed, not bouncing, and hurts what it reaches (damageType, deathType, from the missile): its max damage
// within the inner radius (measured in 3D), falling off beyond to no less than its min damage; the first blast that
// hurts anything leaves a scorch mark `scorchSize` across where the missile is. A scorch wave burns the look of
// everything within its outer radius (BURNED).
struct BlastDefinition
{
	std::uint64_t blastAfter{1};
	std::uint64_t scorchAfter{1};
	Engine::Math::Fixed innerRadius;
	Engine::Math::Fixed outerRadius;
	Engine::Math::Fixed maxDamage;
	Engine::Math::Fixed minDamage;
	Engine::Math::Fixed toppleSpeed;
};

struct BlastWaveDefinition
{
	static constexpr std::uint32_t NoEffect = 0xFFFFFFFFu;
	std::vector<BlastDefinition> blasts; // the enabled ones, in order (at most 16)
	std::uint32_t effect{NoEffect};      // a DeathEffectKind::Effect id
	Engine::Math::Fixed scorchSize;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
};

struct SlowDeathDefinition
{
	DeathFilter filter;
	std::uint32_t probability{10};
	// Times the overkill share (of maximum health), added to probability
	// (a share too, as the original parses it: "20%" is 0.2).
	Engine::Math::Fixed overkillBonus;
	std::uint64_t sinkDelay{0};
	std::uint64_t sinkDelayVariance{0};
	Engine::Math::Fixed sinkRate; // per tick
	std::uint64_t destructionDelay{0};
	std::uint64_t destructionDelayVariance{0};
	PhaseEffects effects;
	CrashDefinition crash;
	FlingDefinition fling;
	BlastWaveDefinition wave;
};

// A structure coming down when it dies (the original's StructureCollapseUpdate): it shudders (up to `maxShudder`
// either way, drawn only) for between the least and most collapse delay, then sinks into the ground, its fall sped
// up by gravity (less its damping share) each tick, until it is `height` down (its geometry's top). Its effects, one
// pick of each list per phase: Initial as it dies, Burst as it starts falling, then every burst delay (least to most)
// a Burst one time in `bigBurstFrequency` and a Delay one otherwise, and Final once down (the game then shows its
// post-collapse look).
enum class CollapsePhase : std::uint8_t
{
	Initial,
	Delay,
	Burst,
	Final,
	Count,
};

struct CollapseDefinition
{
	DeathFilter filter;
	std::uint64_t minCollapseDelay{0};
	std::uint64_t maxCollapseDelay{0};
	std::uint64_t minBurstDelay{9999};
	std::uint64_t maxBurstDelay{0};
	std::uint32_t bigBurstFrequency{0};
	Engine::Math::Fixed damping;
	Engine::Math::Fixed maxShudder;
	Engine::Math::Fixed height;
	std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(CollapsePhase::Count)> effects; // FX lists
	std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(CollapsePhase::Count)> objects; // creation lists
};

// A structure toppling over when it dies (the original's StructureToppleUpdate): after between the least and most
// topple delay it tips over away from its killer (or any way, killed by nothing), faster as it goes, until it lies
// flat; then it stands again, turned to where it fell, showing its post-collapse look. Late in the fall its crushing
// weapon goes off along the ground it lands on. Its effects play only when the killing blow's damage type is among
// `damageFxTypes` (DamageFXTypes); its creation lists, one pick per phase, always.
enum class TopplePhase : std::uint8_t
{
	Initial, // as it dies, where it stands
	Delay,   // with each burst, at its burst point
	Final,   // along each crushed line, and where it stands once flat
	Count,
};

struct ToppleAngleEffect
{
	std::int64_t angle{0}; // radians, Q32 (the fall passing it plays the effect)
	std::uint32_t effect{0};
	std::uint32_t reserved{0};
};

struct StructureToppleDefinition
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	static constexpr std::int64_t One = std::int64_t{1} << 32; // Q32
	DeathFilter filter;
	std::uint64_t minToppleDelay{0};
	std::uint64_t maxToppleDelay{0};
	std::uint64_t minBurstDelay{0};
	std::uint64_t maxBurstDelay{0};
	std::int64_t integrity{One / 10}; // StructuralIntegrity, Q32
	std::int64_t decay{0};            // StructuralDecay, Q32
	std::uint64_t damageFxTypes{~std::uint64_t{0}};
	std::uint32_t startEffect{None};    // ToppleStartFX, where it stands
	std::uint32_t delayEffect{None};    // ToppleDelayFX, at its burst point
	std::uint32_t doneEffect{None};     // ToppleDoneFX, on it once flat
	std::uint32_t crushingEffect{None}; // CrushingFX, where each crushing shot lands
	std::uint32_t crushingWeapon{None}; // CrushingWeaponName (a DeathEffectKind::Weapon id)
	std::uint32_t reserved{0};
	std::vector<ToppleAngleEffect> angleEffects;
	std::array<std::vector<std::uint32_t>, static_cast<std::size_t>(TopplePhase::Count)> objects; // creation lists
	// Its geometry: the height of its top above its position (taken standing) and its radii.
	Engine::Math::Fixed height;
	Engine::Math::Fixed majorRadius;
	Engine::Math::Fixed minorRadius;
};

// CrushDie: crushed to death (the catalog's crush damage), the body is crushed at the end nearest the crusher
// (the front or back crush point, half its major radius along its facing, or its centre: all of it), and plays that
// crush's sound (Total, Back, FrontEndCrushSound) that percent of the time.
enum class CrushLocation : std::uint8_t
{
	Total,
	BackEnd,
	FrontEnd,
};

struct CrushDieDefinition
{
	static constexpr std::uint32_t NoSound = 0xFFFFFFFFu;
	DeathFilter filter;
	std::array<std::uint32_t, 3> sound{NoSound, NoSound, NoSound}; // by CrushLocation (DeathEffectKind::Sound ids)
	std::array<std::uint8_t, 3> percent{};
};

// As the original: only a behaviour that destroys at once, or a slow death
// running out, removes the body; when neither applies the body lingers,
// dead (building rubble, wrecks that stay).
struct DeathDefinition
{
	std::vector<DeathFilter> destroyedAtOnce; // DestroyDie-like behaviours
	std::vector<DieEffect> atDeath;           // effects at the moment of death
	std::vector<SlowDeathDefinition> slow;    // one is chosen
	std::vector<CrushDieDefinition> crushed;  // CrushDie
	std::vector<CollapseDefinition> collapses; // the first that applies runs
	std::vector<StructureToppleDefinition> topples; // the first that applies runs
	Engine::Math::Fixed majorRadius;          // its geometry (where its crush points are)
	bool hulk{false};                         // KINDOF_HULK: a script's hulk lifetime applies to it
};

// Every kind's death, by index (entities hold it in Mortality).
class DeathCatalog
{
public:
	// 0: removed at once, nothing played.
	DeathCatalog() { m_definitions.push_back({{DeathFilter{}}, {}, {}}); }

	std::uint32_t Add(DeathDefinition definition)
	{
		m_definitions.push_back(std::move(definition));
		return static_cast<std::uint32_t>(m_definitions.size() - 1);
	}
	const DeathDefinition &At(std::uint32_t index) const { return m_definitions.at(index < m_definitions.size() ? index : 0); }
	std::size_t Size() const noexcept { return m_definitions.size(); }

	// The damage type that crushes (CrushDie applies only to it).
	std::uint32_t crushDamageType{0xFFFFFFFFu};
	// The game's status bit(s) for being built (UNDER_CONSTRUCTION), which die filters see while UnderConstruction is on
	// it (Object::getStatusBits: the original keeps it among the status bits).
	std::uint64_t underConstructionStatus{0};

private:
	std::vector<DeathDefinition> m_definitions;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DeathCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.death_catalog";
};
}
