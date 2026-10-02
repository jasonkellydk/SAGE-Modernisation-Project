export module engine.gameplay.rts.aircraft.algorithms.jet_runway_cycle;
import std;

export import engine.ecs.core.entity;
export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.algorithms.flight_forces;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.rts.combat.components.countermeasures;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// An airfield jet's cycle (JetAIUpdate's JetAIStateMachine at a ParkingPlaceBehavior with runways), a free function the
// jet system calls for each one, on the port's movement (its turns, speeds and heights; the original's lift physics
// aside, see TakeoffRoll):
// - parked in its space; any order but to idle (aiDoCommand while it may not fly: HAS_PENDING_COMMAND, then
//   TAKING_OFF_AWAIT_CLEARANCE), or an attack or hunt, sends it to
// - AwaitRunway where it stands (JetAwaitingRunwayState): reserveRunway for takeoff; a runway taken, the first to ask is
//   next in line;
// - TaxiToStart (JetOrHeliTaxiState FROM_PARKING): its taxiing locomotor along [the turn point], its prep point and the
//   runway's start (its prep point again when it took the runway from the line: calcPPInfo's m_wasInLine);
// - PauseBeforeTakeoff (JetPauseBeforeTakeoffState, retail): turning to face the runway's end (AIFaceState: within
//   0.035); while another jet holding a runway of its airfield still taxies to take off, it waits; then afterburners lit,
//   TakeoffPause frames on (the transfer one frame on, two when it had waited) it gives its runway to the jet next in line
//   (transferRunwayReservationToNextInLineForTakeoff) and, facing, rolls:
// - TakeoffRoll (JetTakeoffOrLandingState, takeoff): its flight locomotor along the runway to its end at ApproachHeight
//   and on to the exit (0.75 of the runway past its end); its lift grows as the square of how much of the runway it has
//   covered (1 - its distance to the end over the runway's length): the port lifts it toward ApproachHeight in that
//   share (the original's maxLift x ratio on its physics); at the exit, flying, its runway released;
// - Flying: idle and out of its return-to-base ammo (or idle ReturnToBaseIdleTime, or recalled), back to land:
// - Returning (JetOrHeliReturnForLandingState): to the runway approach (0.75 of the runway past its end, at
//   ApproachHeight), then AwaitLanding (reserveRunway for landing: no line; circling meanwhile);
// - Landing (JetTakeoffOrLandingState, landing: its speed held at its least) over the approach, down onto the runway's end
//   and along it to its start ("land the same way we took off but in reverse"), its runway then released;
// - TaxiToParking (TO_PARKING; its flares reloaded at once) along its prep point, [the turn point] and its space;
// - OrientForParking (JetOrHeliParkOrientState): set on its space, turning to the space's turn (within 0.001);
// - Reloading (JetOrHeliReloadAmmoState: its clip reload scaled by how empty the clip is, at least a frame), then parked;
//   an order given meanwhile is carried out then.
// Its airfield gone: as the dead-airfield states. Returns whether it is in its airfield's cycle on the ground or
// rolling or landing (it fires at nothing then).
export namespace engine::gameplay
{
// ParkingPlaceBehavior's RunwayInfo for the tick: who uses each runway and who is next in line for takeoff, rebuilt
// from the jets each tick (they keep their own holds) and written back once all have moved.
struct RunwayLane
{
	ecs::Entity airfield;
	std::uint32_t runway{0};
	ecs::Entity inUse;
	ecs::Entity next;
	std::uint8_t wasInLine{0};
};

struct RunwayTable
{
	std::vector<RunwayLane> lanes;

	RunwayLane &Lane(ecs::Entity airfield, std::uint32_t runway)
	{
		for (RunwayLane &lane : lanes)
			if (lane.airfield == airfield && lane.runway == runway)
				return lane;
		lanes.push_back({airfield, runway, {}, {}, 0});
		return lanes.back();
	}
	const RunwayLane *Holding(ecs::Entity jet) const
	{
		for (const RunwayLane &lane : lanes)
			if (lane.inUse == jet)
				return &lane;
		return nullptr;
	}
};

// reserveRunway.
inline bool ReserveRunway(RunwayTable &table, ecs::Entity jet, ecs::Entity airfield, std::uint32_t runway, bool landing)
{
	RunwayLane &lane = table.Lane(airfield, runway);
	if (lane.inUse == jet)
		return true;
	if (lane.inUse == ecs::Entity{})
	{
		lane.inUse = jet;
		if (lane.next == jet)
		{
			lane.next = {};
			lane.wasInLine = 1;
		}
		else
			lane.wasInLine = 0;
		return true;
	}
	if (!landing && lane.next == ecs::Entity{})
		lane.next = jet;
	return false;
}

// transferRunwayReservationToNextInLineForTakeoff.
inline void TransferRunway(RunwayTable &table, ecs::Entity jet)
{
	for (RunwayLane &lane : table.lanes)
		if (lane.inUse == jet)
		{
			if (lane.next != ecs::Entity{})
			{
				lane.inUse = lane.next;
				lane.wasInLine = 1;
				lane.next = {};
			}
			break;
		}
}

// releaseRunway.
inline void ReleaseRunway(RunwayTable &table, ecs::Entity jet)
{
	for (RunwayLane &lane : table.lanes)
	{
		if (lane.inUse == jet)
		{
			lane.inUse = {};
			lane.wasInLine = 0;
		}
		if (lane.next == jet)
			lane.next = {};
	}
}

namespace runway_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;
using Engine::Math::TurnAngle;

inline constexpr std::uint32_t FaceThreshold = 23924785;  // AIFaceState REL_THRESH 0.035
inline constexpr std::uint32_t OrientThreshold = 683565;  // JetOrHeliParkOrientState THRESH 0.001
inline constexpr std::uint32_t TurnPointThreshold = 16777216; // JetOrHeliTaxiState: PI/128

inline std::int32_t Diff(TurnAngle a, TurnAngle b) noexcept { return static_cast<std::int32_t>((a - b).units); }

// intersectInfiniteLine2D(parking, its turn; prep, its turn + PI/2): where a line through the space along its turn
// meets one through the prep point across it. None when parallel.
inline std::optional<FixedVector2> TurnPoint(FixedVector2 parking, FixedVector2 prep, TurnAngle facing) noexcept
{
	const Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	// a + r (c, s) = prep + t (-s, c): r = (prep - a) . (c, s)
	const Fixed r = (prep.x - parking.x) * c + (prep.y - parking.y) * s;
	return FixedVector2{parking.x + c * r, parking.y + s * r};
}

// calcPPInfo for an airfield jet's space and runway.
struct Places
{
	FixedVector3 parking;
	TurnAngle parkingFacing;
	FixedVector3 prep;
	FixedVector3 start; // its prep point when it took the runway from the line
	FixedVector3 end;
	FixedVector3 approach; // runwayApproach (= runwayExit): 0.75 of the runway past its end, at ApproachHeight
	Fixed takeoffDistance; // runwayTakeoffDist: the runway's length
	std::optional<FixedVector2> turnPoint;
};

inline Places PlacesOf(const Airfield &field, std::uint32_t space, bool wasInLine, Fixed parkingOffset = {}) noexcept
{
	const ParkingSpace &spot = field.spaces[space];
	const RunwayPath &runway = field.runways[std::min<std::uint32_t>(spot.runway, Airfield::MaxRunways - 1)];
	Places places;
	places.parking = ParkingSpot(spot, parkingOffset);
	places.parkingFacing = spot.parkingFacing;
	places.prep = spot.prep;
	places.end = runway.end;
	const FixedVector3 along = runway.end - runway.start;
	places.approach = {runway.end.x + along.x * Fixed::FromRatio(3, 4), runway.end.y + along.y * Fixed::FromRatio(3, 4),
		runway.end.z + field.approachHeight + field.deckHeight};
	places.takeoffDistance = Engine::Math::Sqrt(along.x * along.x + along.y * along.y + along.z * along.z);
	places.start = wasInLine ? spot.prep : runway.start;
	const auto toPrep = spot.prep.XY() - places.parking.XY();
	if (static_cast<std::uint32_t>(std::abs(Diff(Engine::Math::Atan2(toPrep.y, toPrep.x), spot.parkingFacing))) > TurnPointThreshold)
		places.turnPoint = TurnPoint(places.parking.XY(), spot.prep.XY(), spot.parkingFacing);
	return places;
}

// FlightDeckBehavior::calcPPInfo for a flight deck's runway: runwayExit 0.75 of the runway past its end, runwayApproach
// 0.75 of its landing strip short of the strip's start, each ApproachHeight + LandingDeckHeightOffset over the end / the
// strip's start; runwayTakeoffDist the runway's length (start to end).
struct DeckPlaces
{
	FixedVector3 exit;
	FixedVector3 approach;
	FixedVector3 landStart;
	FixedVector3 landEnd;
	Fixed takeoffDistance;
};

inline DeckPlaces DeckPlacesOf(const Airfield &field, const RunwayPath &runway) noexcept
{
	DeckPlaces places;
	const Fixed rise = field.approachHeight + field.deckHeight;
	const FixedVector3 along = runway.end - runway.start;
	places.exit = {runway.end.x + along.x * Fixed::FromRatio(3, 4), runway.end.y + along.y * Fixed::FromRatio(3, 4), runway.end.z + rise};
	places.landStart = runway.landing != 0 ? runway.landStart : runway.start;
	places.landEnd = runway.landing != 0 ? runway.landEnd : runway.end;
	const FixedVector3 back = places.landStart - places.landEnd;
	places.approach = {places.landStart.x + back.x * Fixed::FromRatio(3, 4), places.landStart.y + back.y * Fixed::FromRatio(3, 4),
		places.landStart.z + rise};
	places.takeoffDistance = Engine::Math::Sqrt(along.x * along.x + along.y * along.y + along.z * along.z);
	return places;
}

// JetTakeoffOrLandingState::update (takeoff): setMaxLift(m_maxLift x ratio), ratio = (1 - |runwayEnd - its position| /
// runwayTakeoffDist) squared, clipped to [0, 1].
inline Fixed TakeoffLift(Fixed lift, const FixedVector3 &toEnd, Fixed takeoffDistance) noexcept
{
	const Fixed distance = Engine::Math::Sqrt(toEnd.x * toEnd.x + toEnd.y * toEnd.y + toEnd.z * toEnd.z);
	Fixed ratio = takeoffDistance > Fixed{} ? Fixed::One() - distance / takeoffDistance : Fixed::One();
	ratio = std::clamp(ratio * ratio, Fixed{}, Fixed::One());
	return lift * ratio;
}

inline MoveOrder Explicit(FixedVector2 point) noexcept
{
	MoveOrder order{point, 0xFFFFFFFFu, MoveMode::Direct};
	order.claim = GoalClaim::None;
	return order;
}

inline void Turn(Transform &transform, TurnAngle goal, TurnAngle rate) noexcept
{
	const std::int32_t amount = Diff(goal, transform.facing);
	const auto limit = static_cast<std::int32_t>(std::min<std::uint32_t>(rate.units, 0x7FFFFFFFu));
	transform.facing = transform.facing + TurnAngle{static_cast<std::uint32_t>(std::clamp(amount, -limit, limit))};
}
}

// JetAIUpdate's ALLOW_AIR_LOCO in `state` (friend_setAllowAirLoco): off from its making (onObjectCreated) and whenever it
// taxis (JetOrHeliTaxiState::onEnter) or has set down (HeliTakeoffOrLandingState::onExit, landing); on from a takeoff
// roll, a landing or a helicopter's lift-off or descent (JetTakeoffOrLandingState / HeliTakeoffOrLandingState::onEnter)
// through its flight.
inline constexpr bool AllowsAirLocomotion(JetState state) noexcept
{
	switch (state)
	{
	case JetState::Parked:
	case JetState::TaxiToPrep:
	case JetState::AwaitRunway:
	case JetState::TaxiToStart:
	case JetState::PauseBeforeTakeoff:
	case JetState::TaxiToParking:
	case JetState::OrientForParking:
	case JetState::Reloading:
	case JetState::HeliReloading:
	case JetState::HeliParked:
		return false;
	default:
		return true;
	}
}

struct RunwayJetContext
{
	ecs::Entity self;
	const Airfield *field{nullptr}; // its airfield (alive), none: gone
	RunwayTable *table{nullptr};
	// The jets taxiing to take off as the tick began (airfield, jet): JetPauseBeforeTakeoffState::findWaiter.
	std::span<const std::pair<ecs::Entity, ecs::Entity>> taxiing;
	bool wantsToFly{false}; // an attack or a hunt given it
	bool huntOrGuard{false}; // ALLOW_INTERRUPT_AND_RESUME_OF_CUR_STATE_FOR_RELOAD
	std::uint64_t tick{0};
};

inline bool StepRunwayJet(const RunwayJetContext &context, Jet &jet, Locomotion &motion, MoveOrder &order, Transform &transform, AttackTarget *target,
	Armament *armament, const Health *health, Countermeasures *countermeasures, std::vector<DamageRecord> &damage)
{
	using namespace runway_detail;
	const std::uint64_t tick = context.tick;
	const Airfield *field = context.field;
	const bool hasSpace = field != nullptr && jet.space < field->spaceCount && field->runwayCount > 0;
	const Places places = hasSpace ? PlacesOf(*field, jet.space, jet.wasInLine != 0, jet.parkingOffset) : Places{};
	const bool loaded = armament == nullptr || armament->readyTick != OutOfAmmo;
	const auto enter = [&](JetState state) {
		jet.state = state;
		jet.since = tick;
		jet.leg = 0;
	};
	const auto within = [&](FixedVector2 point) {
		// As AIInternalMoveToState, a kinematic wing's state consumes the same authored flight-leg completion as Steer.
		if (motion.forced == 0 && motion.locomotor.appearance == LocomotorAppearance::Wings)
			return TrackFlightLeg(transform, motion, point).left < motion.locomotor.closeEnough;
		const Fixed reach = std::max(motion.locomotor.closeEnough * Fixed::FromInt(2), Fixed::FromInt(4));
		return Engine::Math::DistanceSquared(transform.position.XY(), point) <= reach * reach;
	};
	const auto setPath = [&](std::initializer_list<FixedVector3> points) {
		jet.pathCount = 0;
		for (const FixedVector3 &point : points)
			if (jet.pathCount < Jet::MaxPath)
				jet.path[jet.pathCount++] = point;
		jet.leg = 0;
	};
	// AIFollowPathState: on to the next point once near this one; true past the last.
	const auto follow = [&] {
		if (jet.leg < jet.pathCount && within(jet.path[jet.leg].XY()))
			++jet.leg;
		if (jet.leg >= jet.pathCount)
			return true;
		jet.goal = jet.path[jet.leg].XY();
		order = Explicit(jet.goal);
		return false;
	};
	// AIFollowPathState flown (JetTakeoffOrLandingState): a leg is done once what is left of it is under CloseEnoughDist
	// (AIInternalMoveToState::update for aircraft: getLocomotorDistanceToGoal along the leg, TrackFlightLeg), checked before
	// it moves as the state machine runs before doLocomotor; the next points within a cell are skipped (tooClose). Each leg:
	// its goal, its height its precise height (the path node's z), and the next leg's length past it (setPathExtraDistance:
	// 4 cells more with more after). True past the last.
	// (Not flown by forces, the port's kinematic legs: within reach of each point.)
	const auto flyLeg = [&] {
		const bool done = jet.leg < jet.pathCount &&
			(motion.forced != 0 ? motion.tracking != 0 && motion.flightGoal == jet.path[jet.leg].XY() &&
					TrackFlightLeg(transform, motion, motion.flightGoal).left < motion.locomotor.closeEnough
								: within(jet.path[jet.leg].XY()));
		if (done)
		{
			++jet.leg;
			while (jet.leg < jet.pathCount && Engine::Math::DistanceSquared(jet.path[jet.leg].XY(), transform.position.XY()) < Fixed::FromInt(100))
				++jet.leg;
		}
		if (jet.leg >= jet.pathCount)
			return true;
		jet.goal = jet.path[jet.leg].XY();
		order = MoveStraightTo(jet.goal);
		order.claim = GoalClaim::None;
		motion.preciseHeight = jet.path[jet.leg].z;
		motion.flightExtra = {};
		if (jet.leg + 1u < jet.pathCount)
		{
			motion.flightExtra = Engine::Math::Distance(jet.path[jet.leg + 1u].XY(), jet.path[jet.leg].XY());
			if (jet.leg + 2u < jet.pathCount)
				motion.flightExtra += Fixed::FromInt(40); // 4 * PATHFIND_CELL_SIZE_F
		}
		return false;
	};
	// JetTakeoffOrLandingState::onExit: neither precise nor ultra-accurate, its lift uncapped (setMaxLift(BIGNUM)), no path.
	const auto settle = [&] {
		motion.preciseZ = 0;
		motion.ultraAccurate = 0;
		motion.liftCap = Fixed::FromInt(99999);
		motion.flightExtra = {};
	};
	// chooseLocomotorSet to another set: fresh locomotors (Locomotor's m_maxLift, m_maxSpeed BIGNUM).
	const auto freshCaps = [&] {
		motion.liftCap = Fixed::FromInt(99999);
		motion.speedCap = Fixed::FromInt(99999);
	};
	// chooseLocomotorSet(NORMAL) in the air: the attack set while its attack run lasts, else the return set while flying home
	// out of ammo (JetAIUpdate::chooseLocomotorSet).
	const auto airSet = [&]() -> std::uint8_t { return jet.attackLocoUntil != 0 ? 1 : jet.returnLoco != 0 ? 2 : 0; };
	// (A set it was given none of, the default NORMAL: its flight locomotor.)
	const auto airLocomotor = [&](std::uint8_t set) -> const LocomotorDefinition & {
		const LocomotorDefinition &chosen = set == 1 ? jet.attack : set == 2 ? jet.returning : jet.flight;
		return chosen.maxSpeed > Fixed{} ? chosen : jet.flight;
	};
	const auto fly = [&] {
		const std::uint8_t set = airSet();
		if (set != jet.locoSet)
			freshCaps();
		jet.locoSet = set;
		motion.locomotor = airLocomotor(jet.locoSet);
	};
	// JetOrHeliTaxiState: its taxiing locomotor, precise and ultra accurate (onExit: neither).
	const auto taxi = [&] {
		if (jet.locoSet != 3)
			freshCaps();
		jet.locoSet = 3;
		motion.locomotor = jet.taxi;
		motion.preciseZ = 1;
		motion.ultraAccurate = 1;
		motion.preciseHeight = transform.position.z;
	};
	// An order given while it may not fly, or is taking off or landing (aiDoCommand: HAS_PENDING_COMMAND).
	const bool cycle = jet.state != JetState::Flying && jet.state != JetState::Returning && jet.state != JetState::ReturnToDeadAirfield &&
		jet.state != JetState::CirclingDeadAirfield;
	if (cycle && order.mode != MoveMode::Idle && !(order.mode == MoveMode::Direct && order.destination == jet.goal))
	{
		jet.pending = 1;
		jet.pendingGoal = order.destination;
	}
	if (cycle && context.wantsToFly && jet.pending == 0)
		jet.pending = 2;
	// JetAIUpdate::update: attacking (OBJECT_STATUS_IS_ATTACKING: at a target in the air) holds its attack locomotor and its
	// attackers' misses AttackLocomotorPersistTime / AttackersMissPersistTime on; after, each lapses on its frame (the
	// attack locomotor then giving way to the normal one: the fork's fix of the retail Aurora staying supersonic).
	bool backToNormal = false;
	if (jet.state == JetState::Flying && target != nullptr && target->target.IsValid())
	{
		jet.attackLocoUntil = tick + jet.attackPersistTicks;
		jet.missUntil = tick + jet.missPersistTicks;
	}
	else
	{
		if (jet.attackLocoUntil != 0 && tick >= jet.attackLocoUntil)
		{
			jet.attackLocoUntil = 0;
			backToNormal = true;
		}
		if (jet.missUntil != 0 && tick >= jet.missUntil)
			jet.missUntil = 0;
	}

	switch (jet.state)
	{
	case JetState::Parked:
	case JetState::TaxiToPrep: // (not a state of the original's: as parked)
		order = {};
		motion.speed = {};
		if (jet.order == Jet::Recall)
			jet.order = Jet::NoOrder;
		if (hasSpace && jet.pending != 0 && loaded)
		{
			enter(JetState::AwaitRunway);
			taxi();
		}
		break;
	case JetState::AwaitRunway:
		order = {};
		motion.speed = {};
		if (!hasSpace)
		{
			enter(JetState::Parked);
			break;
		}
		if (ReserveRunway(*context.table, context.self, jet.airfield, field->spaces[jet.space].runway, false))
		{
			const RunwayLane &lane = context.table->Lane(jet.airfield, field->spaces[jet.space].runway);
			jet.wasInLine = lane.wasInLine;
			const Places now = PlacesOf(*field, jet.space, jet.wasInLine != 0, jet.parkingOffset);
			enter(JetState::TaxiToStart);
			if (now.turnPoint)
				setPath({{now.turnPoint->x, now.turnPoint->y, now.parking.z}, now.prep, now.start});
			else
				setPath({now.prep, now.start});
			taxi();
			follow();
		}
		break;
	case JetState::TaxiToStart:
		if (!hasSpace)
		{
			enter(JetState::Parked);
			break;
		}
		if (follow())
		{
			settle();
			enter(JetState::PauseBeforeTakeoff);
			jet.paused = 0;
			jet.waitedFor = {};
			order = {};
			motion.speed = {};
		}
		break;
	case JetState::PauseBeforeTakeoff:
	{
		order = {};
		motion.speed = {};
		if (!hasSpace)
		{
			enter(JetState::Flying);
			break;
		}
		// AIFaceState: toward the runway's end, at its turn rate.
		const auto toEnd = places.end.XY() - transform.position.XY();
		const TurnAngle facing = Engine::Math::Atan2(toEnd.y, toEnd.x);
		const bool faced = static_cast<std::uint32_t>(std::abs(Diff(facing, transform.facing))) < FaceThreshold;
		if (!faced)
			Turn(transform, facing, motion.locomotor.turnRate);
		// findWaiter: another jet holding a runway of its airfield still taxiing to take off.
		bool waiter = false;
		for (const auto &[airfield, other] : context.taxiing)
			if (airfield == jet.airfield && other != context.self)
				for (const RunwayLane &lane : context.table->lanes)
					if (lane.airfield == jet.airfield && lane.inUse == other)
					{
						if (jet.waitedFor == ecs::Entity{})
							jet.waitedFor = other;
						waiter = true;
					}
		if (waiter)
			break;
		if (jet.paused == 0)
		{
			jet.paused = 1;
			jet.takeoffAt = tick + jet.takeoffPauseTicks;
			if (jet.waitedFor == ecs::Entity{})
			{
				jet.waitedFor = context.self;
				jet.transferAt = tick + 1;
			}
			else
				jet.transferAt = tick + 2;
		}
		if (tick >= jet.transferAt)
			TransferRunway(*context.table, context.self);
		if (tick >= jet.takeoffAt && faced)
		{
			// JetTakeoffOrLandingState (takeoff): down the runway to its end at ApproachHeight, on to the exit.
			enter(JetState::TakeoffRoll);
			// (onEnter: precise and ultra-accurate, its lift capped to nothing until its roll lifts it: setMaxLift(0).)
			setPath({{places.end.x, places.end.y, places.approach.z}, places.approach});
			fly();
			motion.ultraAccurate = 1;
			motion.preciseZ = 1;
			motion.liftCap = {};
			flyLeg();
		}
		break;
	}
	case JetState::TakeoffRoll:
	{
		if (!hasSpace)
		{
			settle();
			ReleaseRunway(*context.table, context.self);
			enter(JetState::Flying);
			break;
		}
		// JetTakeoffOrLandingState::update (takeoff): its lift capped at its lift as it entered x the square of (1 - its 3D
		// distance to the runway's end / runwayTakeoffDist), clipped to [0, 1] (so it wanes again past the end), then on
		// along its path.
		motion.liftCap = TakeoffLift(motion.locomotor.lift, places.end - transform.position, places.takeoffDistance);
		if (flyLeg())
		{
			settle();
			ReleaseRunway(*context.table, context.self);
			enter(JetState::Flying);
			jet.idleSince = tick;
			jet.paused = 0;
			if (jet.pending == 1)
				order = MoveToPoint(jet.pendingGoal);
			else
				order = {};
			jet.pending = 0;
		}
		break;
	}
	case JetState::Flying:
	{
		const bool idle = order.mode == MoveMode::Idle && (target == nullptr || !target->target.IsValid());
		if (!idle)
			jet.idleSince = tick;
		const bool idleTooLong = jet.idleReturnTicks != 0 && tick > jet.idleSince + jet.idleReturnTicks;
		const bool recalled = jet.order == Jet::Recall;
		if (recalled)
			jet.order = Jet::NoOrder;
		const bool outOfAmmo = !loaded && (idle || context.huntOrGuard);
		if (!hasSpace && !loaded && jet.hasHome != 0)
		{
			enter(JetState::ReturnToDeadAirfield);
			jet.goal = jet.home;
			order = MoveToPoint(jet.home);
			if (target != nullptr)
				*target = {};
			break;
		}
		if (hasSpace && (outOfAmmo || (idle && idleTooLong) || recalled))
		{
			// USE_SPECIAL_RETURN_LOCO: home out of ammo on its return set (not when idle too long, nor recalled).
			jet.returnLoco = outOfAmmo && !recalled ? 1 : 0;
			enter(JetState::Returning);
			jet.goal = places.approach.XY();
			order = MoveToPoint(jet.goal, GoalClaim::None);
			if (target != nullptr)
				*target = {};
		}
		break;
	}
	case JetState::ReturnToDeadAirfield:
		if (hasSpace)
			enter(JetState::Flying);
		else if (within(jet.home) || Engine::Math::DistanceSquared(transform.position.XY(), jet.home) <= Fixed::FromInt(100) * Fixed::FromInt(100))
			enter(JetState::CirclingDeadAirfield);
		else
			order = MoveToPoint(jet.home);
		if (target != nullptr)
			*target = {};
		break;
	case JetState::CirclingDeadAirfield:
		if (hasSpace)
		{
			enter(JetState::Flying);
			break;
		}
		order = MoveToPoint(jet.home);
		if (target != nullptr)
			*target = {};
		if (health != nullptr && jet.outOfAmmoDamage > Fixed{})
			damage.push_back({context.self, {}, health->maximum * jet.outOfAmmoDamage, jet.outOfAmmoDamageType, jet.outOfAmmoDeathType});
		break;
	case JetState::Returning:
		if (target != nullptr)
			*target = {};
		if (!hasSpace)
			enter(JetState::Flying);
		else if (within(places.approach.XY()))
			enter(JetState::AwaitLanding);
		else
			order = MoveToPoint(places.approach.XY(), GoalClaim::None);
		break;
	case JetState::AwaitLanding:
		if (target != nullptr)
			*target = {};
		if (!hasSpace)
		{
			enter(JetState::Flying);
			break;
		}
		if (ReserveRunway(*context.table, context.self, jet.airfield, field->spaces[jet.space].runway, true))
		{
			// Over the approach, onto the runway's end, along it to its start; its speed held at its least.
			enter(JetState::Landing);
			setPath({places.approach, places.end, PlacesOf(*field, jet.space, false).start});
			fly();
			// onEnter (landing): its speed capped at its least (setMaxSpeed(MinSpeed)), its lift uncapped.
			motion.speedCap = motion.locomotor.minSpeed;
			motion.liftCap = Fixed::FromInt(99999);
			motion.ultraAccurate = 1;
			motion.preciseZ = 1;
			flyLeg();
		}
		else
			order = {}; // circling (setLocomotorGoalNone)
		break;
	case JetState::Landing:
		if (target != nullptr)
			*target = {};
		if (!hasSpace)
		{
			settle();
			fly();
			ReleaseRunway(*context.table, context.self);
			enter(JetState::Flying);
			break;
		}
		// update (landing): its lift uncapped each frame (setMaxLift(BIGNUM)), then on along its path.
		motion.liftCap = Fixed::FromInt(99999);
		if (flyLeg())
		{
			settle();
			ReleaseRunway(*context.table, context.self);
			enter(JetState::TaxiToParking);
			jet.route = 0;
			if (places.turnPoint)
				setPath({places.prep, {places.turnPoint->x, places.turnPoint->y, places.parking.z}, places.parking});
			else
				setPath({places.prep, places.parking});
			taxi();
			follow();
			// TO_PARKING: its flares reloaded at once (reloadCountermeasures).
			if (countermeasures != nullptr)
				countermeasures->Reload();
		}
		break;
	case JetState::TaxiToParking:
		if (!hasSpace)
		{
			enter(JetState::Parked);
			break;
		}
		if (follow())
		{
			settle();
			enter(JetState::OrientForParking);
			order = {};
			motion.speed = {};
		}
		break;
	case JetState::OrientForParking:
		order = {};
		motion.speed = {};
		if (!hasSpace)
		{
			enter(JetState::Parked);
			break;
		}
		if (static_cast<std::uint32_t>(std::abs(Diff(places.parkingFacing, transform.facing))) <= OrientThreshold)
		{
			enter(JetState::Reloading);
			jet.returnLoco = 0; // friend_setUseSpecialReturnLoco(false)
			std::uint64_t reload = jet.reloadTicks;
			if (armament != nullptr && jet.clipSize > 0)
				reload = jet.reloadTicks * std::min(armament->clip, jet.clipSize) / jet.clipSize;
			jet.takeoffAt = tick + std::max<std::uint64_t>(reload, 1);
			break;
		}
		// Set on its space (z as it is), turned toward the space's turn.
		transform.position.x = places.parking.x;
		transform.position.y = places.parking.y;
		Turn(transform, places.parkingFacing, motion.locomotor.turnRate);
		break;
	case JetState::Reloading:
		order = {};
		motion.speed = {};
		if (tick >= jet.takeoffAt)
		{
			if (armament != nullptr)
			{
				armament->clip = 0;
				armament->scatterUsed = 0;
				if (armament->readyTick == OutOfAmmo || armament->readyTick > tick)
					armament->readyTick = tick;
				armament->reloading = false;
			}
			enter(JetState::Parked);
		}
		break;
	default:
		break;
	}
	// JetAIUpdate::update in the air: the attack set while the run lasts, else the return set while flying home; the run
	// over, the normal set again (unless flying home).
	const bool air = jet.state == JetState::TakeoffRoll || jet.state == JetState::Flying || jet.state == JetState::Returning ||
		jet.state == JetState::AwaitLanding || jet.state == JetState::Landing || jet.state == JetState::ReturnToDeadAirfield ||
		jet.state == JetState::CirclingDeadAirfield;
	if (air && (jet.attackLocoUntil != 0 || jet.returnLoco != 0 || backToNormal) && airSet() != jet.locoSet)
	{
		const bool landingSpeed = jet.state == JetState::Landing;
		fly();
		if (landingSpeed)
			motion.speedCap = motion.locomotor.minSpeed;
	}
	return jet.state != JetState::Flying && jet.state != JetState::Returning && jet.state != JetState::AwaitLanding &&
		jet.state != JetState::ReturnToDeadAirfield && jet.state != JetState::CirclingDeadAirfield;
}
}
