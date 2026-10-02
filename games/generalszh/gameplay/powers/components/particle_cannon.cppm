export module games.generalszh.gameplay.powers.components.particle_cannon;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The Particle Cannon's uplink (the original's ParticleUplinkCannonUpdate), its simulation state: where it is in its
// cycle (charging, raising its antenna, almost ready, ready, firing, after the beam, packing up), its beam's life
// (born, decaying, gone), when the attack started and starts to decay, where it was aimed first, where the beam is and
// where it is being driven, how it is driven (swept by itself, by the player's clicks, along a script's waypoints),
// its scorch marks and damage pulses so far and when the next are due, and its beam's width (LaserRadiusUpdate:
// widening over WidthGrowTime as it is born, narrowing as it decays).
export namespace generalszh::gameplay
{
enum class CannonStatus : std::uint8_t
{
	Idle,
	Charging,
	Preparing,
	AlmostReady,
	ReadyToFire,
	PreFire,
	Firing,
	PostFire,
	Packing,
};

enum class BeamStatus : std::uint8_t
{
	None,
	Born,
	Decaying,
	Dead,
};

struct ParticleCannon
{
	Engine::Math::FixedVector3 initialTarget;
	Engine::Math::FixedVector3 currentTarget;
	Engine::Math::FixedVector3 destination; // m_overrideTargetDestination
	std::uint64_t startAttackTick{0};
	std::uint64_t startDecayTick{0};
	std::uint64_t nextScorchTick{0};
	std::uint64_t nextPulseTick{0};
	std::uint64_t nextLaunchFxTick{0};
	std::uint64_t lastClickTick{0};
	std::uint64_t secondLastClickTick{0};
	// The beam's width (LaserRadiusUpdate).
	std::uint64_t widenStart{0};
	std::uint64_t widenFinish{0};
	std::uint64_t decayStart{0};
	std::uint64_t decayFinish{0};
	Engine::Math::Fixed widthScale{Engine::Math::Fixed::One()};
	std::uint32_t scorchMarks{0};
	std::uint32_t pulses{0};
	std::uint32_t nextWaypoint{0xFFFFFFFFu};
	CannonStatus status{CannonStatus::Idle};
	BeamStatus beam{BeamStatus::None};
	std::uint8_t manual{0};    // m_manualTargetMode: driven by the player's clicks
	std::uint8_t scripted{0};  // m_scriptedWaypointMode: driven along a script's waypoints
	std::uint8_t widening{0};
	std::uint8_t decaying{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};

// Per kind of building: its power (a SpecialPowerRules index), times (ticks), the beam's reach, damage and marks, and
// the effects and remnant it leaves (DeathEffectKind::Effect ids, a remnant definition name; none: 0xFFFFFFFF / empty).
struct ParticleCannonConfig
{
	bool present{false};
	std::uint32_t power{0xFFFFFFFFu};
	std::uint64_t beginChargeTicks{0};
	std::uint64_t raiseAntennaTicks{0};
	std::uint64_t readyDelayTicks{0};
	std::uint64_t widthGrowTicks{0};
	std::uint64_t beamTravelTicks{0};
	std::uint64_t totalFiringTicks{0};
	std::uint64_t launchFxTicks{30};
	std::uint64_t doubleClickTicks{500};
	Engine::Math::Fixed swathDistance;
	Engine::Math::Fixed swathAmplitude;
	std::uint32_t totalScorchMarks{0};
	Engine::Math::Fixed scorchScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed damagePerSecond;
	std::uint32_t totalPulses{0};
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
	Engine::Math::Fixed damageRadiusScalar{Engine::Math::Fixed::One()};
	Engine::Math::Fixed beamRadius{Engine::Math::Fixed::FromInt(13)}; // its beam's template radius (OuterBeamWidth / 2)
	Engine::Math::Fixed drivingSpeed;     // ManualDrivingSpeed, per second
	Engine::Math::Fixed fastDrivingSpeed; // ManualFastDrivingSpeed, per second
	std::uint32_t groundHitEffect{0xFFFFFFFFu};
	std::uint32_t launchEffect{0xFFFFFFFFu};
	std::string remnant; // DamagePulseRemnantObjectName
};

// The tick's cannon output, made or played after the tick: scorch marks (addScorch) and effects for the presentation,
// the remnants each damage pulse leaves (on the cannon's team), and each cannon's status changes (setLogicalStatus) and
// its orbital beam's birth (createOrbitToTargetLaser) and end, in the order they happened (the presentation's sound
// loops and client effects follow them). Cleared as each tick starts.
struct ParticleCannonEvents
{
	struct Change
	{
		enum class Kind : std::uint8_t
		{
			Status,
			BeamBorn,
			BeamGone,
		};
		ecs::Entity cannon;
		Kind kind{Kind::Status};
		CannonStatus status{CannonStatus::Idle};
	};
	struct Scorch
	{
		Engine::Math::FixedVector3 at;
		Engine::Math::Fixed radius;
	};
	struct Played
	{
		std::uint32_t effect{0xFFFFFFFFu};
		Engine::Math::FixedVector3 at;
	};
	struct Remnant
	{
		std::uint32_t definition{0}; // the cannon's (its config names the remnant)
		std::uint32_t team{0};
		Engine::Math::FixedVector3 at;
	};
	std::vector<Scorch> scorches;
	std::vector<Played> played;
	std::vector<Remnant> remnants;
	std::vector<Change> changes;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::ParticleCannon>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.particle_cannon";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::ParticleCannonEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.particle_cannon_events";
};
}
