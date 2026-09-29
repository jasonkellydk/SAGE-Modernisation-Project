export module engine.gameplay.rts.aircraft.components.jet;
import std;

export import engine.ecs.core.entity;
export import engine.gameplay.rts.movement.definitions.locomotor;
import engine.ecs.core.component_registry;

// A jet's cycle at its airfield (the original's JetAIUpdate): parked in its
// space; taxiing out to the prep point and the runway start; rolling down
// the runway after a pause at its start (afterburners lit, waiting while
// another jet of its airfield still taxies to take off, then
// `takeoffPauseTicks`), lifting off after `lift` of it; flying; back when out of ammo
// (or idle too long), landing along the runway, taxiing to its space and
// reloading there. It moves on its taxiing locomotor on the ground and its
// flight locomotor in the air.
export namespace engine::gameplay
{
enum class JetState : std::uint32_t
{
	Parked,
	TaxiToPrep,
	AwaitRunway,
	TaxiToStart,
	TakeoffRoll,
	Flying,
	Returning,
	AwaitLanding,
	Landing,
	TaxiToParking,
	Reloading,
	PauseBeforeTakeoff, // lined up at the runway's start (JetPauseBeforeTakeoffState)
	ReturnToDeadAirfield, // out of ammo, its airfield gone: back to where it was (JetOrHeliReturningToDeadAirfieldState)
	CirclingDeadAirfield, // circling there, hurting, until an airfield takes it in (JetOrHeliCirclingDeadAirfieldState)
};

struct Jet
{
	LocomotorDefinition flight;
	LocomotorDefinition taxi;
	ecs::Entity airfield;
	JetState state{JetState::Parked};
	std::uint32_t space{0};
	std::uint64_t since{0};         // tick the state began
	std::uint64_t reloadTicks{0};   // to refill its clip once parked
	std::uint64_t idleReturnTicks{0}; // flying this long with nothing to do: back to base (0: never)
	std::uint64_t idleSince{0};
	Engine::Math::Fixed lift;       // share of the runway rolled before lifting off
	Engine::Math::FixedVector2 goal; // where it is taxiing or flying to in this state
	std::uint32_t leg{0};            // within a state: which stretch of the way
	std::uint32_t reserved{0};
	std::uint64_t takeoffPauseTicks{0}; // TakeoffPause
	std::uint64_t takeoffAt{0};         // the pause's end, once its countdown started
	// The appearance bit shown while its afterburners burn (the game's JETAFTERBURNER; none: not shown).
	static constexpr std::uint32_t NoAfterburner = 0xFFFFFFFFu;
	std::uint32_t afterburnerBit{NoAfterburner};
	std::uint32_t reserved2{0};
	// MinHeight: out of plain flight (or on the ground) it is drawn at least this high above the terrain (its gear).
	Engine::Math::Fixed minHeight;
	// Where its airfield stood (while it had one), and what circling a dead airfield costs it a tick
	// (OutOfAmmoDamagePerSecond over the second's ticks, a share of its maximum health) and how (unresistable, dying
	// normally).
	Engine::Math::FixedVector2 home;
	std::uint32_t hasHome{0};
	std::uint32_t outOfAmmoDamageType{0};
	Engine::Math::Fixed outOfAmmoDamage;
	std::uint32_t outOfAmmoDeathType{0};
	std::uint32_t reserved3{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Jet>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Jet &value, StateHasher &hasher) noexcept
	{
		for (const auto *locomotor : {&value.flight, &value.taxi})
		{
			for (const auto fixed : {locomotor->maxSpeed, locomotor->minSpeed, locomotor->acceleration, locomotor->braking, locomotor->preferredHeight,
					 locomotor->preferredHeightDamping, locomotor->closeEnough})
				hasher.AppendU64(static_cast<std::uint64_t>(fixed.Raw()));
			hasher.AppendU64(locomotor->turnRate.units);
			hasher.AppendU64(static_cast<std::uint64_t>(locomotor->appearance) << 8 | static_cast<std::uint64_t>(locomotor->height));
		}
		hasher.AppendU64(value.airfield.index);
		hasher.AppendU64(value.airfield.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.state));
		hasher.AppendU64(value.space);
		hasher.AppendU64(value.since);
		hasher.AppendU64(value.reloadTicks);
		hasher.AppendU64(value.idleReturnTicks);
		hasher.AppendU64(value.idleSince);
		hasher.AppendU64(static_cast<std::uint64_t>(value.lift.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.goal.y.Raw()));
		hasher.AppendU64(value.leg);
		hasher.AppendU64(value.takeoffPauseTicks);
		hasher.AppendU64(value.takeoffAt);
		hasher.AppendU64(value.afterburnerBit);
		hasher.AppendU64(static_cast<std::uint64_t>(value.minHeight.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.home.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.home.y.Raw()));
		hasher.AppendU64(value.hasHome);
		hasher.AppendU64(value.outOfAmmoDamageType);
		hasher.AppendU64(static_cast<std::uint64_t>(value.outOfAmmoDamage.Raw()));
		hasher.AppendU64(value.outOfAmmoDeathType);
	}
};
}
