export module engine.gameplay.rts.aircraft.systems.jet_system;
import std;

export import engine.gameplay.rts.movement.components.floor_lift;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.rts.combat.components.sneaky_target;
export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.systems.movement_system;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.appearance.components.draw_offset;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.aircraft.resources.jet_damage;
export import Engine.Core.Math.FixedVector;
import engine.gameplay.rts.aircraft.algorithms.helicopter_flight;
import engine.gameplay.rts.aircraft.algorithms.jet_runway_cycle;

// Jets through their cycle at their airfield, once a tick between targeting
// and movement (a batch: runways are shared, so it runs in one
// deterministic pass). A runway is in use while a jet taxies onto it, rolls
// down it or lands on it; the others wait their turn. On the ground a jet
// only goes where its cycle takes it, and does not fire. Lined up at the
// runway's start it pauses (JetPauseBeforeTakeoffState, as retail: while
// another jet of its airfield taxies to take off it waits; then afterburners
// on and TakeoffPause more ticks); its afterburners burn until it is flying.
// On a flight deck (JetAIUpdate's DECK_HEIGHT_OFFSET paths): it stands on the deck (its floor raised by the deck's
// height); only the front row takes off (one further back waits to be moved up), rolling down the runway and climbing to
// runwayExit (FlightDeckBehavior::calcPPInfo: DeckPlacesOf, ApproachHeight + LandingDeckHeightOffset up); it comes back to
// runwayApproach behind the landing strip's start and lands along the strip (JetTakeoffOrLandingState's path), then
// rolls through the runway's taxi points to its space; one new from the hangar comes out by the runway's creation points.
export namespace engine::gameplay
{
struct JetSystem
{
	using Query = ecs::Query<ecs::Write<Jet>, ecs::Write<Locomotion>, ecs::Write<MoveOrder>, ecs::Write<Transform>, ecs::Optional<Aggression>,
		ecs::OptionalWrite<AttackTarget>, ecs::OptionalWrite<Armament>, ecs::OptionalWrite<Appearance>, ecs::OptionalWrite<DrawOffset>, ecs::Optional<Health>,
		ecs::OptionalWrite<Countermeasures>, ecs::OptionalWrite<FloorLift>, ecs::OptionalWrite<SneakyTarget>>;
	using Lookup = ecs::Lookup<ecs::Read<Airfield>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>, ecs::Write<JetDamage>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const std::uint64_t tick = context.Tick();
		const auto lookup = context.Lookup<Lookup>();
		auto &damage = context.Write<JetDamage>().records;
		damage.clear();
		// Runways in use (airfield, runway), from where the jets are in their cycles.
		std::vector<std::pair<ecs::Entity, std::uint32_t>> busy;
		// Jets taxiing to take off, by airfield (JetPauseBeforeTakeoffState::findWaiter waits for them).
		std::vector<std::pair<ecs::Entity, ecs::Entity>> taxiing;
		RunwayTable runways;
		const auto runwayOf = [&](const Jet &jet) -> std::uint32_t {
			const Airfield *field = lookup.Get<Airfield>(jet.airfield);
			return field != nullptr && jet.space < field->spaceCount ? field->spaces[jet.space].runway : 0u;
		};
		query.ForEachChunk([&](auto chunk) {
			const auto jets = chunk.template Get<Jet>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < jets.size(); ++row)
			{
				const Jet &jet = jets[row];
				if (jet.state == JetState::TaxiToStart || jet.state == JetState::PauseBeforeTakeoff || jet.state == JetState::TakeoffRoll ||
					jet.state == JetState::Landing)
					busy.push_back({jet.airfield, runwayOf(jet)});
				if (jet.state == JetState::TaxiToStart)
					taxiing.push_back({jet.airfield, entities[row]});
				// An airfield jet's runway hold (ParkingPlaceBehavior's RunwayInfo).
				if (jet.runwayHold != 0)
				{
					RunwayLane &lane = runways.Lane(jet.airfield, runwayOf(jet));
					if (jet.runwayHold == 1)
					{
						lane.inUse = entities[row];
						lane.wasInLine = jet.wasInLine;
					}
					else
						lane.next = entities[row];
				}
			}
		});
		const auto inUse = [&](ecs::Entity field, std::uint32_t runway) {
			return std::find(busy.begin(), busy.end(), std::pair{field, runway}) != busy.end();
		};

		query.ForEachChunk([&](auto chunk) {
			auto jets = chunk.template Get<Jet>();
			auto motions = chunk.template Get<Locomotion>();
			auto orders = chunk.template Get<MoveOrder>();
			auto transforms = chunk.template Get<Transform>();
			const auto aggressions = chunk.template Get<Aggression>();
			auto targets = chunk.template Get<AttackTarget>();
			auto armaments = chunk.template Get<Armament>();
			auto appearances = chunk.template Get<Appearance>();
			auto offsets = chunk.template Get<DrawOffset>();
			const auto healths = chunk.template Get<Health>();
			auto countermeasureRows = chunk.template Get<Countermeasures>();
			auto lifts = chunk.template Get<FloorLift>();
			auto sneakies = chunk.template Get<SneakyTarget>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < jets.size(); ++row)
			{
				Jet &jet = jets[row];
				Locomotion &motion = motions[row];
				MoveOrder &order = orders[row];
				Transform &transform = transforms[row];
				Armament *armament = armaments.empty() ? nullptr : &armaments[row];
				AttackTarget *target = targets.empty() ? nullptr : &targets[row];
				// JetAIUpdate::update, before its state machine steps: JETEXHAUST while its body moves (its speed as the last
				// tick left it) on its air locomotion (ALLOW_AIR_LOCO as its last state left it).
				const bool exhaust = motion.speed > Fixed{} && AllowsAirLocomotion(jet.state);
				const Airfield *field = lookup.IsAlive(jet.airfield) ? lookup.Get<Airfield>(jet.airfield) : nullptr;
				const bool hasSpace = field != nullptr && jet.space < field->spaceCount;
				const ParkingSpace space = hasSpace ? field->spaces[jet.space] : ParkingSpace{};
				const RunwayPath runway = hasSpace && space.runway < field->runwayCount ? field->runways[space.runway] : RunwayPath{};
				// Where its airfield stands, remembered for when it is gone.
				if (field != nullptr)
					if (const Transform *at = lookup.Get<Transform>(jet.airfield))
					{
						jet.home = at->position.XY();
						jet.hasHome = 1;
					}
				const auto along = runway.end.XY() - runway.start.XY();
				const bool deck = field != nullptr && field->frontRow != 0;
				// Its floor: the deck while it is one of a flight deck's.
				if (!lifts.empty())
					lifts[row].height = field != nullptr ? field->deckHeight : Fixed{};
				// Its way to its space once down or out of the hangar: on a flight deck through the runway's taxi points (landed)
				// or creation points after the hangar (new), then its prep point and space; else the prep point and space.
				std::array<Engine::Math::FixedVector2, RunwayPath::MaxTaxi + 2> taxiWay{};
				std::uint32_t taxiWayCount = 0;
				if (deck && jet.route == 0)
					for (std::uint32_t point = 0; point < runway.taxiCount; ++point)
						taxiWay[taxiWayCount++] = runway.taxi[point].XY();
				if (deck && jet.route == 1)
					for (std::uint32_t point = 1; point < runway.creationCount; ++point)
						taxiWay[taxiWayCount++] = runway.creation[point].XY();
				taxiWay[taxiWayCount++] = space.prep.XY();
				taxiWay[taxiWayCount++] = space.parking.XY();
				const bool wantsToFly = (!aggressions.empty() && aggressions[row].stance == Stance::Hunt) || (target != nullptr && target->target.IsValid()) ||
					jet.order == Jet::Scramble;
				const bool loaded = armament == nullptr || armament->readyTick != OutOfAmmo;
				const auto enter = [&](JetState state) {
					jet.state = state;
					jet.since = tick;
					jet.leg = 0;
				};
				// A change of locomotor set: fresh locomotors (Locomotor's m_maxLift, m_maxSpeed BIGNUM).
				const auto freshCaps = [&] {
					motion.liftCap = Fixed::FromInt(99999);
					motion.speedCap = Fixed::FromInt(99999);
				};
				const auto head = [&](Engine::Math::FixedVector2 goal, const LocomotorDefinition &locomotor) {
					jet.goal = goal;
					freshCaps(); // (taxiing: its own set)
					motion.locomotor = locomotor;
				};
				const auto arrived = [&] {
					const Fixed reach = std::max(motion.locomotor.closeEnough * Fixed::FromInt(2), Fixed::FromInt(4));
					return Engine::Math::DistanceSquared(transform.position.XY(), jet.goal) <= reach * reach;
				};
				// A flight deck jet's takeoff and landing (JetTakeoffOrLandingState: AIFollowPathState over calcPPInfo's
				// points): on to the next point once near this one, true past the last.
				const runway_detail::DeckPlaces deckPlaces = deck ? runway_detail::DeckPlacesOf(*field, runway) : runway_detail::DeckPlaces{};
				const auto setPath = [&](std::initializer_list<Engine::Math::FixedVector3> points) {
					jet.pathCount = 0;
					for (const Engine::Math::FixedVector3 &point : points)
						if (jet.pathCount < Jet::MaxPath)
							jet.path[jet.pathCount++] = point;
					jet.leg = 0;
					if (jet.pathCount > 0)
						jet.goal = jet.path[0].XY();
				};
				// Flown by forces, each leg is done by the aircraft arrival rule (AIInternalMoveToState::update: what is left of it
				// along its line under CloseEnoughDist, TrackFlightLeg; the next points within a cell skipped), its height the
				// node's z, the next leg's length past it (setPathExtraDistance); kinematic (no body), within reach.
				// `entering`: the path just given (AIFollowPathState::onEnter sets the first point as its goal; whether it is
				// there already is asked by its update, from the next frame on).
				const auto followPath = [&](bool entering = false) {
					const bool done = !entering && jet.leg < jet.pathCount &&
						(motion.forced != 0 ? motion.tracking != 0 && motion.flightGoal == jet.path[jet.leg].XY() &&
								TrackFlightLeg(transform, motion, motion.flightGoal).left < motion.locomotor.closeEnough
											: arrived());
					if (done)
					{
						++jet.leg;
						if (motion.forced != 0)
							while (jet.leg < jet.pathCount && Engine::Math::DistanceSquared(jet.path[jet.leg].XY(), transform.position.XY()) < Fixed::FromInt(100))
								++jet.leg;
					}
					if (jet.leg >= jet.pathCount)
						return true;
					jet.goal = jet.path[jet.leg].XY();
					order = MoveStraightTo(jet.goal);
					order.claim = GoalClaim::None;
					if (motion.forced != 0)
						motion.preciseHeight = jet.path[jet.leg].z;
					motion.flightExtra = {};
					if (jet.leg + 1u < jet.pathCount)
					{
						motion.flightExtra = Engine::Math::Distance(jet.path[jet.leg + 1u].XY(), jet.path[jet.leg].XY());
						if (jet.leg + 2u < jet.pathCount)
							motion.flightExtra += Fixed::FromInt(40);
					}
					return false;
				};
				const bool grounded = jet.state != JetState::TakeoffRoll && jet.state != JetState::Flying && jet.state != JetState::Returning &&
					jet.state != JetState::AwaitLanding && jet.state != JetState::Landing &&
					jet.state != JetState::ReturnToDeadAirfield && jet.state != JetState::CirclingDeadAirfield;

				// A helicopter's own cycle (HeliAIStateMachine).
				bool heliCycle = false;
				// An airfield's (or no airfield's) jet: its runway cycle (JetAIStateMachine); a flight deck's keeps the deck's.
				const bool deckJet = field != nullptr && field->frontRow != 0;
				if (jet.helicopter != 0)
					heliCycle = StepHelicopter(jet, motion, order, transform, target, armament, healths.empty() ? nullptr : &healths[row], field, tick);
				else if (!deckJet)
				{
					RunwayJetContext runwayContext{entities[row], field, &runways, taxiing, wantsToFly,
						!aggressions.empty() && (aggressions[row].stance == Stance::Hunt || aggressions[row].stance == Stance::Guard), tick};
					heliCycle = StepRunwayJet(runwayContext, jet, motion, order, transform, target, armament, healths.empty() ? nullptr : &healths[row],
						countermeasureRows.empty() ? nullptr : &countermeasureRows[row], damage);
				}
				else
				switch (jet.state)
				{
				case JetState::Parked:
					if (jet.order == Jet::Recall)
						jet.order = Jet::NoOrder; // down already
					if (hasSpace && wantsToFly && loaded)
					{
						if (jet.order == Jet::Scramble)
							jet.order = Jet::NoOrder;
						enter(JetState::TaxiToPrep);
						head(space.prep.XY(), jet.taxi);
					}
					break;
				case JetState::TaxiToPrep:
					if (arrived())
						enter(JetState::AwaitRunway);
					break;
				case JetState::AwaitRunway:
					// A flight deck launches from its front row only.
					if (deck && jet.space >= field->runwayCount)
						break;
					if (!inUse(jet.airfield, space.runway))
					{
						busy.push_back({jet.airfield, space.runway});
						enter(JetState::TaxiToStart);
						head(runway.start.XY(), jet.taxi);
					}
					break;
				case JetState::TaxiToStart:
					if (arrived())
					{
						// Lined up at the start, facing down the runway: the pause before takeoff.
						enter(JetState::PauseBeforeTakeoff);
						transform.facing = Engine::Math::Heading(along);
						jet.takeoffAt = 0;
					}
					break;
				case JetState::PauseBeforeTakeoff:
				{
					// findWaiter: another jet of its airfield still taxiing to take off holds it.
					bool waiter = false;
					for (const auto &[field, other] : taxiing)
						waiter = waiter || (field == jet.airfield && other != entities[row]);
					if (waiter)
						break;
					if (jet.leg == 0)
					{
						// The countdown starts, and its afterburners light.
						jet.leg = 1;
						jet.takeoffAt = tick + jet.takeoffPauseTicks;
					}
					if (tick >= jet.takeoffAt)
					{
						// JetTakeoffOrLandingState (takeoff) on a flight deck: its flight locomotor, precise and ultra
						// accurate, down the runway to its end at runwayApproach's height, on to runwayExit.
						enter(JetState::TakeoffRoll);
						setPath({{runway.end.x, runway.end.y, deckPlaces.approach.z}, deckPlaces.exit});
						freshCaps();
						motion.locomotor = jet.flight;
						motion.ultraAccurate = 1;
						motion.preciseZ = 1;
						motion.preciseHeight = transform.position.z;
						motion.liftCap = Fixed{}; // setMaxLift(0)
						followPath(true);
					}
					break;
				}
				case JetState::TakeoffRoll:
				{
					// Its lift: (1 - its distance to the runway's end / runwayTakeoffDist) squared, full past the end; the
					// port lifts it from the end's height toward the point it makes for in that share.
					// Flown by forces: the original's own (JetTakeoffOrLandingState::update: setMaxLift(m_maxLift x ratio), the
					// path node's height precise; TakeoffLift).
					if (motion.forced != 0)
						motion.liftCap = runway_detail::TakeoffLift(motion.locomotor.lift, runway.end - transform.position, deckPlaces.takeoffDistance);
					else
					{
						const auto toEnd = runway.end - transform.position;
						const Fixed distance = Engine::Math::Sqrt(toEnd.x * toEnd.x + toEnd.y * toEnd.y + toEnd.z * toEnd.z);
						Fixed ratio = deckPlaces.takeoffDistance > Fixed{} ? Fixed::One() - distance / deckPlaces.takeoffDistance : Fixed::One();
						ratio = std::clamp(ratio * ratio, Fixed{}, Fixed::One());
						if (jet.leg >= 1)
							ratio = Fixed::One();
						const Fixed toward = jet.path[std::min<std::uint32_t>(jet.leg, Jet::MaxPath - 1)].z;
						motion.preciseHeight = runway.end.z + (toward - runway.end.z) * ratio;
					}
					if (followPath())
					{
						motion.preciseZ = 0;
						motion.ultraAccurate = 0;
						motion.liftCap = Fixed::FromInt(99999); // onExit: setMaxLift(BIGNUM)
						motion.flightExtra = {};
						enter(JetState::Flying);
						jet.idleSince = tick;
						order = {};
					}
					break;
				}
				case JetState::Flying:
				{
					const bool busyInAir = (target != nullptr && target->target.IsValid()) || (order.mode != MoveMode::Idle && order.explicitGoal == 0);
					if (busyInAir)
						jet.idleSince = tick;
					// JetAIUpdate::update: its first idle tick in the air arms m_returnToBaseFrame at that tick plus
					// ReturnToBaseIdleTime; it heads home on that tick (a busy tick disarms it).
					const bool idleTooLong = jet.idleReturnTicks != 0 && tick > jet.idleSince + jet.idleReturnTicks;
					// Recalled by its airfield (aiEnter): back to land.
					const bool recalled = jet.order == Jet::Recall;
					if (recalled)
						jet.order = Jet::NoOrder;
					// Out of ammo with its airfield gone (and none to take it in): back to where it was.
					if (!hasSpace && !loaded && jet.hasHome != 0)
					{
						enter(JetState::ReturnToDeadAirfield);
						jet.goal = jet.home;
						order = MoveToPoint(jet.home);
						if (target != nullptr)
							*target = {};
						break;
					}
					if (hasSpace && (!loaded || idleTooLong || recalled))
					{
						enter(JetState::Returning);
						// JetOrHeliReturnForLandingState: to runwayApproach (0.75 of its landing strip short of the strip's
						// start, ApproachHeight + LandingDeckHeightOffset over it).
						head(deckPlaces.approach.XY(), jet.flight);
						order = MoveToPoint(jet.goal, GoalClaim::None);
						if (target != nullptr)
							*target = {};
					}
					break;
				}
				case JetState::ReturnToDeadAirfield:
					if (hasSpace)
						enter(JetState::Flying); // an airfield took it in
					else if (arrived() || Engine::Math::DistanceSquared(transform.position.XY(), jet.home) <= Fixed::FromInt(100) * Fixed::FromInt(100))
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
					// Staying where it is (flying round it), losing OutOfAmmoDamagePerSecond of its health a second.
					order = MoveToPoint(jet.home);
					if (target != nullptr)
						*target = {};
					if (!healths.empty() && jet.outOfAmmoDamage > Fixed{})
						damage.push_back({entities[row], {}, healths[row].maximum * jet.outOfAmmoDamage, jet.outOfAmmoDamageType, jet.outOfAmmoDeathType});
					break;
				case JetState::Returning:
					if (!hasSpace)
						enter(JetState::Flying);
					else if (arrived())
						enter(JetState::AwaitLanding);
					else
						order = MoveToPoint(jet.goal, GoalClaim::None);
					if (target != nullptr)
						*target = {};
					break;
				case JetState::AwaitLanding:
					if (!hasSpace)
						enter(JetState::Flying);
					else if (!inUse(jet.airfield, space.runway))
					{
						busy.push_back({jet.airfield, space.runway});
						// JetTakeoffOrLandingState (landing) on a flight deck: over runwayApproach, onto its landing strip's
						// start and along it to its end, precise and ultra accurate, its speed held at its least.
						enter(JetState::Landing);
						setPath({deckPlaces.approach, deckPlaces.landStart, deckPlaces.landEnd});
						motion.locomotor = jet.flight;
						motion.locomotor.maxSpeed = motion.locomotor.minSpeed;
						motion.speedCap = motion.locomotor.minSpeed; // setMaxSpeed(MinSpeed)
						motion.liftCap = Fixed::FromInt(99999);
						motion.ultraAccurate = 1;
						motion.preciseZ = 1;
						motion.preciseHeight = deckPlaces.approach.z;
						followPath(true);
					}
					else
						order = {}; // circling (setLocomotorGoalNone)
					if (target != nullptr)
						*target = {};
					break;
				case JetState::Landing:
				{
					if (!hasSpace)
					{
						motion.preciseZ = 0;
						motion.ultraAccurate = 0;
						motion.locomotor = jet.flight;
						enter(JetState::Flying);
						break;
					}
					// The height of the point it makes for (the path's end: its strip's end); its lift uncapped each frame.
					if (jet.leg < jet.pathCount)
						motion.preciseHeight = jet.path[jet.leg].z;
					motion.liftCap = Fixed::FromInt(99999);
					if (followPath())
					{
						motion.preciseZ = 0;
						motion.ultraAccurate = 0;
						motion.flightExtra = {};
						enter(JetState::TaxiToParking);
						jet.route = 0;
						// Its way as a landed jet's: through the taxi points on a flight deck.
						if (deck && runway.taxiCount > 0)
							head(runway.taxi[0].XY(), jet.taxi);
						else
							head(space.prep.XY(), jet.taxi);
						// Taxiing to its parking place: its flares reloaded at once (reloadCountermeasures).
						if (!countermeasureRows.empty())
							countermeasureRows[row].Reload();
					}
					if (target != nullptr)
						*target = {};
					break;
				}
				case JetState::TaxiToParking:
					if (arrived())
					{
						if (jet.leg + 1 < taxiWayCount)
						{
							++jet.leg;
							head(taxiWay[jet.leg], jet.taxi);
						}
						else
						{
							enter(JetState::Reloading);
							transform.facing = space.parkingFacing;
						}
					}
					break;
				case JetState::Reloading:
					if (tick >= jet.since + jet.reloadTicks)
					{
						if (armament != nullptr && armament->readyTick == OutOfAmmo)
						{
							armament->clip = 0; // refilled on its next shot
							armament->scatterUsed = 0;
							armament->readyTick = tick;
							armament->reloading = false;
						}
						enter(JetState::Parked);
					}
					break;
				}

				// getSneakyTargetingOffset: SneakyOffsetWhenAttacking along its facing while its attackers miss it.
				if (!sneakies.empty())
					sneakies[row] = {Engine::Math::Direction(transform.facing) * jet.sneakyOffset, jet.missUntil};
				// JetAIUpdate::update's MinHeight: in its airfield cycle (not plain flight), or not above the terrain, it is
				// drawn raised to MinHeight above the terrain when lower.
				if (!offsets.empty() && jet.minHeight > Fixed{})
				{
					const Fixed height = std::max(Fixed{}, transform.position.z - context.Read<GroundHeight>().At(transform.position.XY()));
					const bool check = jet.state != JetState::Flying || height <= Fixed{};
					offsets[row].z = check && height < jet.minHeight ? jet.minHeight - height : Fixed{};
				}
				if (!appearances.empty() && jet.exhaustBit != Jet::NoExhaust)
					appearances[row].Set(jet.exhaustBit, exhaust);
				// friend_enableAfterburners: lit from the pause's countdown until it is flying.
				if (!appearances.empty() && jet.afterburnerBit != Jet::NoAfterburner)
					appearances[row].Set(jet.afterburnerBit,
						(jet.state == JetState::PauseBeforeTakeoff && (jet.leg != 0 || jet.paused != 0)) || jet.state == JetState::TakeoffRoll);
				if (heliCycle)
				{
					// Coming down, down or lifting off: no shots.
					if (armament != nullptr && armament->readyTick != OutOfAmmo && armament->readyTick <= tick)
						armament->readyTick = tick + 1;
				}
				else if (jet.helicopter == 0 && deckJet && (grounded || jet.state == JetState::Landing || jet.state == JetState::TakeoffRoll))
				{
					// Its own way only; no shots on the ground.
					const bool still = jet.state == JetState::Parked || jet.state == JetState::AwaitRunway || jet.state == JetState::Reloading ||
						jet.state == JetState::PauseBeforeTakeoff;
					// (Rolling or landing it follows its path's explicit points.)
					if (jet.state != JetState::Landing && jet.state != JetState::TakeoffRoll)
						order = still ? MoveOrder{} : MoveToPoint(jet.goal);
					if (still)
						motion.speed = {};
					if (armament != nullptr && armament->readyTick != OutOfAmmo && armament->readyTick <= tick)
						armament->readyTick = tick + 1;
				}
			}
		});
		// The runways as the jets left them: each keeps its own hold.
		query.ForEachChunk([&](auto chunk) {
			auto jets = chunk.template Get<Jet>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < jets.size(); ++row)
			{
				Jet &jet = jets[row];
				jet.runwayHold = 0;
				jet.wasInLine = 0;
				for (const RunwayLane &lane : runways.lanes)
				{
					if (lane.inUse == entities[row])
					{
						jet.runwayHold = 1;
						jet.wasInLine = lane.wasInLine;
					}
					else if (lane.next == entities[row] && jet.runwayHold == 0)
						jet.runwayHold = 2;
				}
			}
		});
	}
};
// Jets circling a dead airfield: their hurt joins the tick's incoming damage (after the tick's impacts fill it,
// before it is applied).
struct JetDamageSystem
{
	using Query = ecs::Query<ecs::Read<Jet>>;
	using Resources = ecs::Resources<ecs::Read<JetDamage>, ecs::Write<IncomingDamage>>;

	void Execute(ecs::SystemContext &context) const
	{
		const JetDamage &hurt = context.Read<JetDamage>();
		if (hurt.records.empty())
			return;
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		for (const DamageRecord &record : hurt.records)
			incoming.Add(record);
		incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::JetSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.jets";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After targeting (it may send them off), before movement (it takes them where they go).
	using Before = SystemTypeList<engine::gameplay::MovementSystem>;
	using After = SystemTypeList<engine::gameplay::TargetingSystem>;
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::JetDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the jets and the tick's impacts.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
