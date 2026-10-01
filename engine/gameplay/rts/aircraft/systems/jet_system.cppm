export module engine.gameplay.rts.aircraft.systems.jet_system;
import std;

export import engine.gameplay.rts.movement.components.floor_lift;
export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.countermeasures;
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

// Jets through their cycle at their airfield, once a tick between targeting
// and movement (a batch: runways are shared, so it runs in one
// deterministic pass). A runway is in use while a jet taxies onto it, rolls
// down it or lands on it; the others wait their turn. On the ground a jet
// only goes where its cycle takes it, and does not fire. Lined up at the
// runway's start it pauses (JetPauseBeforeTakeoffState, as retail: while
// another jet of its airfield taxies to take off it waits; then afterburners
// on and TakeoffPause more ticks); its afterburners burn until it is flying.
// On a flight deck (JetAIUpdate's DECK_HEIGHT_OFFSET paths): it stands on the deck (its floor raised by the deck's
// height); only the front row takes off (one further back waits to be moved up); it comes in over the landing strip's
// start from well behind it and lands along the strip, touching down at the deck's height, then rolls through the
// runway's taxi points to its space; one new from the hangar comes out by the runway's creation points.
export namespace engine::gameplay
{
struct JetSystem
{
	using Query = ecs::Query<ecs::Write<Jet>, ecs::Write<Locomotion>, ecs::Write<MoveOrder>, ecs::Write<Transform>, ecs::Optional<Aggression>,
		ecs::OptionalWrite<AttackTarget>, ecs::OptionalWrite<Armament>, ecs::OptionalWrite<Appearance>, ecs::OptionalWrite<DrawOffset>, ecs::Optional<Health>,
		ecs::OptionalWrite<Countermeasures>, ecs::OptionalWrite<FloorLift>>;
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
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < jets.size(); ++row)
			{
				Jet &jet = jets[row];
				Locomotion &motion = motions[row];
				MoveOrder &order = orders[row];
				Transform &transform = transforms[row];
				Armament *armament = armaments.empty() ? nullptr : &armaments[row];
				AttackTarget *target = targets.empty() ? nullptr : &targets[row];
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
				const Fixed length = Engine::Math::Length(along);
				// Where it lands: a flight deck's own strip, else the runway.
				const bool deck = field != nullptr && field->frontRow != 0;
				const auto landStart = runway.landing != 0 ? runway.landStart.XY() : runway.start.XY();
				const auto landEnd = runway.landing != 0 ? runway.landEnd.XY() : runway.end.XY();
				const auto landAlong = landEnd - landStart;
				const Fixed landLength = Engine::Math::Length(landAlong);
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
				const auto head = [&](Engine::Math::FixedVector2 goal, const LocomotorDefinition &locomotor) {
					jet.goal = goal;
					motion.locomotor = locomotor;
				};
				const auto arrived = [&] {
					const Fixed near = std::max(motion.locomotor.closeEnough * Fixed::FromInt(2), Fixed::FromInt(4));
					return Engine::Math::DistanceSquared(transform.position.XY(), jet.goal) <= near * near;
				};
				const bool grounded = jet.state != JetState::TakeoffRoll && jet.state != JetState::Flying && jet.state != JetState::Returning &&
					jet.state != JetState::AwaitLanding && jet.state != JetState::Landing &&
					jet.state != JetState::ReturnToDeadAirfield && jet.state != JetState::CirclingDeadAirfield;

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
						// Roll down the runway on the ground, flight speeds.
						enter(JetState::TakeoffRoll);
						LocomotorDefinition roll = jet.flight;
						roll.height = HeightBehavior::NoMotiveForce;
						roll.preferredHeight = {};
						head(runway.end.XY() + along, roll);
					}
					break;
				}
				case JetState::TakeoffRoll:
				{
					const Fixed rolled = Engine::Math::Distance(runway.start.XY(), transform.position.XY());
					if (rolled >= length * jet.lift)
						motion.locomotor = jet.flight; // lift off
					if (rolled >= length)
					{
						enter(JetState::Flying);
						jet.idleSince = tick;
						order = MoveToPoint(jet.goal);
					}
					break;
				}
				case JetState::Flying:
				{
					const bool busyInAir = (target != nullptr && target->target.IsValid()) || order.mode != MoveMode::Idle;
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
						// In over the start of its runway (its landing strip), from well behind it.
						head(landStart - landAlong - landAlong / Fixed::FromInt(2), jet.flight);
						order = MoveToPoint(jet.goal);
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
					else if (arrived() || Engine::Math::DistanceSquared(transform.position.XY(), jet.goal) <= landLength * landLength)
						enter(JetState::AwaitLanding);
					else
						order = MoveToPoint(jet.goal);
					if (target != nullptr)
						*target = {};
					break;
				case JetState::AwaitLanding:
					if (!hasSpace)
						enter(JetState::Flying);
					else if (!inUse(jet.airfield, space.runway))
					{
						busy.push_back({jet.airfield, space.runway});
						enter(JetState::Landing);
						// Down along the runway: aimed at its end (wings count as there from far off),
						// touching down near its start (a flight deck: on its deck).
						LocomotorDefinition landing = jet.flight;
						landing.preferredHeight = field->deckHeight;
						head(landEnd, landing);
						order = MoveToPoint(jet.goal);
					}
					else
						order = MoveToPoint(landStart - landAlong - landAlong); // circle out and try again
					if (target != nullptr)
						*target = {};
					break;
				case JetState::Landing:
				{
					// How far along the runway (its landing strip) it is (negative: short of it).
					const auto from = transform.position.XY() - landStart;
					const Fixed covered = landLength > Fixed{} ? Engine::Math::Dot(from, landAlong) / landLength : Fixed{};
					if (!hasSpace)
						enter(JetState::Flying);
					else if (jet.leg == 0 && covered > Fixed{} - landLength / Fixed::FromInt(4))
					{
						// Touch down: on its wheels, braking, down the runway.
						jet.leg = 1;
						head(landEnd, jet.taxi);
					}
					else if (jet.leg == 1 && covered >= landLength * Fixed::FromRatio(3, 4))
					{
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

				// JetAIUpdate::update's MinHeight: in its airfield cycle (not plain flight), or not above the terrain, it is
				// drawn raised to MinHeight above the terrain when lower.
				if (!offsets.empty() && jet.minHeight > Fixed{})
				{
					const Fixed height = std::max(Fixed{}, transform.position.z - context.Read<GroundHeight>().At(transform.position.XY()));
					const bool check = jet.state != JetState::Flying || height <= Fixed{};
					offsets[row].z = check && height < jet.minHeight ? jet.minHeight - height : Fixed{};
				}
				// friend_enableAfterburners: lit from the pause's countdown until it is flying.
				if (!appearances.empty() && jet.afterburnerBit != Jet::NoAfterburner)
					appearances[row].Set(jet.afterburnerBit,
						(jet.state == JetState::PauseBeforeTakeoff && jet.leg != 0) || jet.state == JetState::TakeoffRoll);
				if (grounded || jet.state == JetState::Landing || jet.state == JetState::TakeoffRoll)
				{
					// Its own way only; no shots on the ground.
					const bool still = jet.state == JetState::Parked || jet.state == JetState::AwaitRunway || jet.state == JetState::Reloading ||
						jet.state == JetState::PauseBeforeTakeoff;
					order = still ? MoveOrder{} : MoveToPoint(jet.goal);
					if (still)
						motion.speed = {};
					if (armament != nullptr && armament->readyTick != OutOfAmmo && armament->readyTick <= tick)
						armament->readyTick = tick + 1;
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
