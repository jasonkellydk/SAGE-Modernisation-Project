export module engine.gameplay.rts.aircraft.algorithms.helicopter_flight;
import std;

export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.spatial.resources.ground_height;
export import Engine.Core.Math.FixedVector;

// A helicopter's tick (JetAIUpdate with NeedsRunway No: HeliAIStateMachine, as the PRODUCED_AT_HELIPAD ones use it), a
// free function the jet system calls for each one:
// - flying, it is a plain aircraft (no parking space, no runway; it never goes home idle). Sent to its airfield
//   (doLandingCommand: Jet::Recall, its landing spot set) or out of its return-to-base ammo, it flies to its landing
//   spot (JetOrHeliReturnForLandingState, adjustsDestination off), then
// - comes down (HeliTakeoffOrLandingState, landing: PRECISE_Z_POS and ULTRA_ACCURATE on its flight locomotor) through
//   ApplyApproachHeight over the spot and then the spot itself, each point reached within 3 (THRESH, in three
//   dimensions);
// - down, it is on its taxiing locomotor (no air locomotion) and refills its clips (JetOrHeliReloadAmmoState: the
//   longest of its weapons' clip reloads, scaled by how empty the clip is, at least a frame), then stands; on the
//   ground it is its airfield's healee;
// - whole again (JetAIUpdate::update: idle or reloading, health at its maximum, no order waiting) or given an order, it
//   lifts off (HeliTakeoffOrLandingState, taking off) from where it stands to ApproachHeight over it, and is flying
//   again: an order given while it came down, stood or lifted off (aiDoCommand's HAS_PENDING_COMMAND) is carried out
//   then.
// Its airfield gone while it heads there, it flies on.
// Returns whether it is in its airfield's cycle (coming down, down, or lifting off): it fires at nothing then.
export namespace engine::gameplay
{
namespace helicopter_detail
{
inline bool InCycle(JetState state) noexcept
{
	return state == JetState::HeliLanding || state == JetState::HeliReloading || state == JetState::HeliParked || state == JetState::HeliTakingOff;
}

// setLocomotorGoalPositionExplicit: straight for the point, no pathfinding, no claim on the spot.
inline MoveOrder Explicit(Engine::Math::FixedVector2 point) noexcept
{
	MoveOrder order{point, 0xFFFFFFFFu, MoveMode::Direct};
	order.claim = GoalClaim::None;
	order.explicitGoal = 1;
	return order;
}
}

inline bool StepHelicopter(Jet &jet, Locomotion &motion, MoveOrder &order, Transform &transform, AttackTarget *target, Armament *armament,
	const Health *health, const Airfield *field, std::uint64_t tick)
{
	using Engine::Math::Fixed;
	using namespace helicopter_detail;
	const bool loaded = armament == nullptr || armament->readyTick != OutOfAmmo;
	const auto enter = [&](JetState state) {
		jet.state = state;
		jet.since = tick;
		jet.leg = 0;
	};
	// An order given while in its cycle: what it does once flying (HAS_PENDING_COMMAND). Its own explicit goal aside.
	if (InCycle(jet.state))
	{
		const bool own = order.mode == MoveMode::Direct && order.destination == jet.goal;
		if (order.mode != MoveMode::Idle && !own)
		{
			jet.pending = 1;
			jet.pendingGoal = order.destination;
		}
		else if (target != nullptr && target->target.IsValid() && jet.pending == 0)
			jet.pending = 2; // an attack: its target is kept
	}
	const Fixed approach = field != nullptr ? field->approachHeight + field->deckHeight : Fixed{};
	// HeliTakeoffOrLandingState::update: for the point it is at, within 3 of it.
	const auto follow = [&] {
		const Engine::Math::FixedVector3 &point = jet.heliPath[std::min<std::uint32_t>(jet.leg, 1)];
		jet.goal = point.XY();
		order = Explicit(jet.goal);
		motion.preciseZ = 1;
		motion.ultraAccurate = 1;
		motion.preciseHeight = point.z;
		const auto apart = transform.position - point;
		if (apart.x * apart.x + apart.y * apart.y + apart.z * apart.z <= Fixed::FromInt(9))
			++jet.leg;
		return jet.leg >= 2;
	};
	const auto settle = [&] {
		motion.preciseZ = 0;
		motion.ultraAccurate = 0;
	};
	const auto takeOff = [&] {
		enter(JetState::HeliTakingOff);
		// m_parkingLoc where it stands, up ApproachHeight over it.
		jet.heliPath[0] = transform.position;
		jet.heliPath[1] = {transform.position.x, transform.position.y, transform.position.z + approach};
		motion.locomotor = jet.flight;
	};

	switch (jet.state)
	{
	case JetState::Flying:
	{
		const bool recalled = jet.order == Jet::Recall;
		if (recalled)
			jet.order = Jet::NoOrder;
		if (field != nullptr && (recalled || !loaded))
		{
			enter(JetState::HeliReturning);
			jet.goal = jet.landingSpot.XY();
			order = MoveToPoint(jet.goal, GoalClaim::None);
			if (target != nullptr)
				*target = {};
		}
		break;
	}
	case JetState::HeliReturning:
	{
		if (target != nullptr)
			*target = {};
		if (field == nullptr)
		{
			enter(JetState::Flying);
			break;
		}
		const Fixed near = std::max(motion.locomotor.closeEnough, Fixed::One());
		if (Engine::Math::DistanceSquared(transform.position.XY(), jet.goal) <= near * near)
		{
			// LANDING_AWAIT_CLEARANCE and ORIENT_FOR_PARKING_PLACE pass at once (a helipad's): down over the spot.
			enter(JetState::HeliLanding);
			jet.heliPath[0] = {jet.landingSpot.x, jet.landingSpot.y, jet.landingSpot.z + approach};
			jet.heliPath[1] = jet.landingSpot;
			motion.locomotor = jet.flight;
			follow();
		}
		else
			order = MoveToPoint(jet.goal, GoalClaim::None);
		break;
	}
	case JetState::HeliLanding:
		if (target != nullptr && jet.pending != 2)
			*target = {};
		if (follow())
		{
			// onExit (landing): no air locomotion, its taxiing locomotor. RELOAD_AMMO.
			settle();
			motion.locomotor = jet.taxi;
			order = {};
			motion.speed = {};
			enter(JetState::HeliReloading);
			std::uint64_t reload = 0;
			if (armament != nullptr && jet.clipSize > 0)
			{
				// (`clip` counts the clip's shots fired.)
				const std::uint32_t needed = std::min(armament->clip, jet.clipSize);
				reload = jet.reloadTicks * needed / jet.clipSize;
			}
			else if (armament != nullptr)
				reload = jet.reloadTicks;
			jet.takeoffAt = tick + std::max<std::uint64_t>(reload, 1);
		}
		break;
	case JetState::HeliReloading:
	case JetState::HeliParked:
	{
		order = {};
		motion.speed = {};
		if (jet.state == JetState::HeliReloading && tick >= jet.takeoffAt)
		{
			if (armament != nullptr)
			{
				armament->clip = 0;
				armament->scatterUsed = 0;
				if (armament->readyTick == OutOfAmmo || armament->readyTick > tick)
					armament->readyTick = tick;
				armament->reloading = false;
			}
			enter(JetState::HeliParked);
		}
		const bool whole = health != nullptr && health->current == health->maximum;
		// An order waits out the reload; healed whole with none, it goes up.
		if ((jet.pending != 0 && jet.state == JetState::HeliParked) || (jet.pending == 0 && whole))
			takeOff();
		break;
	}
	case JetState::HeliTakingOff:
		if (follow())
		{
			settle();
			enter(JetState::Flying);
			jet.idleSince = tick;
			if (jet.pending == 1)
				order = MoveToPoint(jet.pendingGoal);
			else
				order = {};
			jet.pending = 0;
		}
		break;
	default:
		break;
	}
	return InCycle(jet.state);
}
}
