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
// A helicopter (NeedsRunway No: the original's HeliAIStateMachine, its PRODUCED_AT_HELIPAD use) holds no parking space:
// it flies from where it is made, and comes down only when sent to its airfield (doLandingCommand: to a landing spot
// by it), straight down from ApproachHeight over the spot and, on the ground, healed by its airfield; whole again (or
// given an order) it lifts straight up to ApproachHeight and flies on (HeliTakeoffOrLandingState).
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
	HeliReturning,  // a helicopter flying to its landing spot (JetOrHeliReturnForLandingState)
	HeliLanding,    // coming down over it (HeliTakeoffOrLandingState, landing)
	HeliReloading,  // down, refilling its clips (JetOrHeliReloadAmmoState)
	HeliParked,     // down, idle (healed by its airfield)
	HeliTakingOff,  // lifting off (HeliTakeoffOrLandingState, taking off)
	OrientForParking, // in its space, turning to its parking turn (JetOrHeliParkOrientState)
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
	std::uint32_t route{0};          // TaxiToParking on a flight deck: 0 from landing (its taxi points), 1 from the hangar (its creation points)
	std::uint64_t takeoffPauseTicks{0}; // TakeoffPause
	std::uint64_t takeoffAt{0};         // the pause's end, once its countdown started
	// The appearance bit shown while its afterburners burn (the game's JETAFTERBURNER; none: not shown).
	static constexpr std::uint32_t NoAfterburner = 0xFFFFFFFFu;
	std::uint32_t afterburnerBit{NoAfterburner};
	// An order its airfield passed it (a flight deck's launch or recall): Scramble, off the ground as soon as it may;
	// Recall, back to land (aiEnter its airfield).
	static constexpr std::uint32_t NoOrder = 0, Scramble = 1, Recall = 2;
	std::uint32_t order{NoOrder};
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
	std::uint32_t clipSize{0}; // its weapon's ClipSize (0: none): how empty its clip is, for its reload
	// A helicopter (NeedsRunway No): `space` is NoSpace; its landing spot by its airfield (m_landingPosForHelipadStuff),
	// the two points it comes down or lifts off through (HeliTakeoffOrLandingState::m_path; `leg` the one it makes for),
	// and an order given while it took off, landed or stood (HAS_PENDING_COMMAND: `pending`, its move's destination):
	// carried out once flying.
	static constexpr std::uint32_t NoSpace = 0xFFFFFFFFu;
	std::uint32_t helicopter{0};
	std::uint32_t pending{0};
	Engine::Math::FixedVector3 landingSpot;
	std::array<Engine::Math::FixedVector3, 2> heliPath{};
	Engine::Math::FixedVector2 pendingGoal;
	// An airfield jet's runway (ParkingPlaceBehavior's RunwayInfo, kept by the jets: `runwayHold` 1 it is m_inUseBy, 2
	// m_nextInLineForTakeoff; `wasInLine`: it took the runway from the line, so it starts its roll at its prep point), the
	// way it taxies, rolls or lands (AIFollowPathState's path: `path`, `pathCount`, `leg` the point it makes for), and its
	// pause before takeoff (JetPauseBeforeTakeoffState: `takeoffAt`, `transferAt`, `waitedFor`; `paused` 1 once timed).
	static constexpr std::uint32_t MaxPath = 5;
	std::array<Engine::Math::FixedVector3, MaxPath> path{};
	std::uint32_t pathCount{0};
	std::uint8_t runwayHold{0};
	std::uint8_t wasInLine{0};
	std::uint8_t paused{0};
	std::uint8_t reserved4{0};
	std::uint64_t transferAt{0};
	ecs::Entity waitedFor;
	Engine::Math::Fixed parkingOffset; // ParkingOffset: its parking spot moved this far along its space's apron turn
	// Its attack run (the Aurora's): the locomotor it flies on while attacking and AttackLocomotorPersistTime after
	// (AttackLocomotorType: m_attackLocoExpireFrame, `attackLocoUntil`), and home out of ammo (ReturnForAmmoLocomotorType:
	// USE_SPECIAL_RETURN_LOCO, `returnLoco`); its attackers miss it, aiming SneakyOffsetWhenAttacking along its facing, while
	// attacking and AttackersMissPersistTime after (m_attackersMissExpireFrame, `missUntil`). `locoSet`: the set it flies on
	// (0 normal, 1 attacking, 2 returning).
	LocomotorDefinition attack;
	LocomotorDefinition returning;
	std::uint64_t attackPersistTicks{0};
	std::uint64_t missPersistTicks{0};
	std::uint64_t attackLocoUntil{0};
	std::uint64_t missUntil{0};
	Engine::Math::Fixed sneakyOffset;
	std::uint8_t returnLoco{0};
	std::uint8_t locoSet{0};
	// The appearance bit shown while its engines leave their exhaust (the game's JETEXHAUST: its contrails; NoExhaust: not
	// shown).
	static constexpr std::uint8_t NoExhaust = 0xFF;
	std::uint8_t exhaustBit{NoExhaust};
	std::uint8_t reserved5[5]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Jet>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet";
	static constexpr std::uint32_t Version = 5; // 5: exhaustBit
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Jet &value, StateHasher &hasher) noexcept
	{
		for (const auto *locomotor : {&value.flight, &value.taxi, &value.attack, &value.returning})
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
		hasher.AppendU64(value.route);
		hasher.AppendU64(value.takeoffPauseTicks);
		hasher.AppendU64(value.takeoffAt);
		hasher.AppendU64(value.afterburnerBit);
		hasher.AppendU64(value.order);
		hasher.AppendU64(static_cast<std::uint64_t>(value.minHeight.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.home.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.home.y.Raw()));
		hasher.AppendU64(value.hasHome);
		hasher.AppendU64(value.outOfAmmoDamageType);
		hasher.AppendU64(static_cast<std::uint64_t>(value.outOfAmmoDamage.Raw()));
		hasher.AppendU64(value.outOfAmmoDeathType);
		hasher.AppendU64((static_cast<std::uint64_t>(value.helicopter) << 32) | value.pending);
		hasher.AppendU64(value.clipSize);
		for (std::uint32_t point = 0; point < value.pathCount && point < engine::gameplay::Jet::MaxPath; ++point)
			for (const auto fixed : {value.path[point].x, value.path[point].y, value.path[point].z})
				hasher.AppendU64(static_cast<std::uint64_t>(fixed.Raw()));
		hasher.AppendU64((static_cast<std::uint64_t>(value.pathCount) << 32) | (static_cast<std::uint64_t>(value.runwayHold) << 16) |
			(static_cast<std::uint64_t>(value.wasInLine) << 8) | value.paused);
		hasher.AppendU64(value.transferAt);
		hasher.AppendU64(static_cast<std::uint64_t>(value.parkingOffset.Raw()));
		hasher.AppendU64(value.attackPersistTicks);
		hasher.AppendU64(value.missPersistTicks);
		hasher.AppendU64(value.attackLocoUntil);
		hasher.AppendU64(value.missUntil);
		hasher.AppendU64(static_cast<std::uint64_t>(value.sneakyOffset.Raw()));
		hasher.AppendU64((static_cast<std::uint64_t>(value.exhaustBit) << 16) | (static_cast<std::uint64_t>(value.returnLoco) << 8) | value.locoSet);
		hasher.AppendU64((static_cast<std::uint64_t>(value.waitedFor.index) << 32) | value.waitedFor.generation);
		for (const auto &point : {value.landingSpot, value.heliPath[0], value.heliPath[1]})
			for (const auto fixed : {point.x, point.y, point.z})
				hasher.AppendU64(static_cast<std::uint64_t>(fixed.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.pendingGoal.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.pendingGoal.y.Raw()));
	}
};
}
