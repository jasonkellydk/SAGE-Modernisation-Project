export module engine.gameplay.rts.combat.components.turret;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// A turret aiming its entity's weapon on its own (the original's TurretAI):
// its turn (about the body's up, relative to the body's facing) and pitch,
// how fast they move per tick, where it rests, how it scans while idle, and
// how long it holds its aim after losing its target before swinging back
// there at half speed. Its weapon fires once it is aligned with the target
// (within about 2 degrees), unless it fires while turning.
export namespace engine::gameplay
{
struct TurretDefinition
{
	Engine::Math::TurnAngle turnRate{Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(1, 100))}; // per tick
	Engine::Math::TurnAngle pitchRate{Engine::Math::TurnFromRadians(Engine::Math::Fixed::FromRatio(1, 100))};
	Engine::Math::TurnAngle naturalAngle;
	Engine::Math::TurnAngle naturalPitch;
	Engine::Math::TurnAngle firePitch; // non-zero: always fires at this pitch
	Engine::Math::TurnAngle minPitch;
	// TurretFireAngleSweep per weapon slot (PRIMARY, SECONDARY, TERTIARY): while firing it swings this far either side
	// of the target, and fires anywhere within it.
	std::array<Engine::Math::TurnAngle, 3> sweep{};
	// GroundUnitPitch: extra pitch at full range at ground targets (scaled down as they come closer).
	Engine::Math::TurnAngle groundUnitPitch;
	std::uint64_t recenterTicks{60};
	// Idle scans (MinIdleScanAngle..MaxIdleScanAngle either side of rest,
	// every MinIdleScanInterval..MaxIdleScanInterval ticks); none when both angles are 0.
	// In turn units, unwrapped: a full turn (360 degrees, scanning all round) is 2^32, not 0.
	std::int64_t minScanUnits{0};
	std::int64_t maxScanUnits{0};
	std::uint64_t minScanTicks{299999};
	std::uint64_t maxScanTicks{299999};
	// TurretSweepSpeedModifier per weapon slot: its turn rate while sweeping.
	std::array<Engine::Math::Fixed, 3> sweepSpeed{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(), Engine::Math::Fixed::One()};
	bool allowsPitch{false};
	bool firesWhileTurning{false};
	bool initiallyDisabled{false}; // InitiallyDisabled: off until its AI turns it on (a deploying unit, once deployed)
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};

// The original's turret state machine (TurretAI.cpp): each tick the current
// state acts; a state it hands over to first acts the tick after.
enum class TurretState : std::uint8_t
{
	Idle,     // waiting `until` (a random scan interval) before scanning
	IdleScan, // turning (half speed) to rest + `scanAngle`
	Aim,      // on a target
	Hold,     // lost its target: holding its aim until `until`
	Recenter, // swinging back to rest (half speed)
};

struct Turret
{
	TurretDefinition definition;
	std::uint64_t until{0};            // Idle: the scan tick (0: not yet drawn); Hold: the tick it recenters
	Engine::Math::TurnAngle angle; // relative to the body
	Engine::Math::TurnAngle pitch;
	Engine::Math::TurnAngle scanAngle; // IdleScan: the offset from rest it scans to
	TurretState state{TurretState::Idle};
	bool aligned{false};               // on the current target this tick
	bool fireReady{false};             // aligned last tick: its weapon may fire (AIM -> FIRE a tick on)
	bool rotating{false};              // turning or pitching this tick (TURRET_ROTATE, its rotation sound)
	bool enabled{true};                // TurretAI::m_enabled: off, it stays as it is (unless recentering) and aims at nothing
	bool positiveSweep{true};          // TurretAI::m_positiveSweep: the side it sweeps to next
	bool onTemporary{false};           // aiming at its attack's temporary target this tick (AttackTarget::temporary)
	std::uint8_t reserved[5]{};        // no padding: checkpoints hold its bytes
};

// TurretAI::isTurretInNaturalPosition.
inline bool TurretAtRest(const Turret &turret) noexcept
{
	return turret.angle == turret.definition.naturalAngle && turret.pitch == turret.definition.naturalPitch;
}
}

export namespace engine::gameplay
{
// A second turret (the original's AltTurret, with its own controlled weapon slot); linked
// (TurretsLinked) it aims at whatever the first does.
struct AltTurret
{
	Turret turret;
	bool linked{false};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Turret>
{
	static constexpr std::string_view StableName = "engine.gameplay.turret";
	static constexpr std::uint32_t Version = 6; // 6: onTemporary
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Turret &value, StateHasher &hasher) noexcept
	{
		for (std::size_t slot = 0; slot < 3; ++slot)
			hasher.AppendU64((static_cast<std::uint64_t>(value.definition.sweep[slot].units) << 32) ^ static_cast<std::uint64_t>(value.definition.sweepSpeed[slot].Raw()));
		hasher.AppendU64(value.definition.groundUnitPitch.units | (value.positiveSweep ? 0x100000000ull : 0u));
		const auto &d = value.definition;
		hasher.AppendU64((static_cast<std::uint64_t>(d.turnRate.units) << 32) | d.pitchRate.units);
		hasher.AppendU64((static_cast<std::uint64_t>(d.naturalAngle.units) << 32) | d.naturalPitch.units);
		hasher.AppendU64((static_cast<std::uint64_t>(d.firePitch.units) << 32) | d.minPitch.units);
		hasher.AppendU64(static_cast<std::uint64_t>(d.minScanUnits));
		hasher.AppendU64(static_cast<std::uint64_t>(d.maxScanUnits));
		hasher.AppendU64(d.recenterTicks);
		hasher.AppendU64(d.minScanTicks);
		hasher.AppendU64(d.maxScanTicks);
		hasher.AppendU64((static_cast<std::uint64_t>(value.angle.units) << 32) | value.pitch.units);
		hasher.AppendU64(value.until);
		hasher.AppendU64(value.scanAngle.units);
		hasher.AppendU64(static_cast<std::uint64_t>(value.state) | (d.allowsPitch ? 0x100u : 0u) | (d.firesWhileTurning ? 0x200u : 0u) |
			(value.aligned ? 0x400u : 0u) | (value.rotating ? 0x800u : 0u) | (value.fireReady ? 0x1000u : 0u) | (value.enabled ? 0x2000u : 0u) |
			(d.initiallyDisabled ? 0x4000u : 0u) | (value.onTemporary ? 0x8000u : 0u));
	}
};
template<>
struct ComponentTraits<engine::gameplay::AltTurret>
{
	static constexpr std::string_view StableName = "engine.gameplay.alt_turret";
	static constexpr std::uint32_t Version = 3;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::AltTurret &value, StateHasher &hasher) noexcept
	{
		ComponentTraits<engine::gameplay::Turret>::HashState(value.turret, hasher);
		hasher.AppendU64(value.linked ? 1u : 0u);
	}
};
}
