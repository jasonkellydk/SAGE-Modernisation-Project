export module engine.gameplay.rts.movement.systems.movement_system;
import std;
export import engine.gameplay.rts.movement.components.floor_lift;
export import engine.gameplay.common.spatial.components.carried;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.movement.algorithms.steering;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.path_completed;
export import engine.gameplay.rts.movement.components.move_ended;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.movement.components.descent;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;
export import engine.gameplay.rts.navigation.definitions.pathfind_cell;
export import engine.gameplay.rts.movement.components.wander_anchor;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.rts.blocking.algorithms.locomotor_blocking;
export import engine.gameplay.rts.movement.components.move_away;
export import engine.gameplay.rts.movement.components.desired_speed;
export import engine.gameplay.rts.movement.algorithms.flight_forces;
export import engine.gameplay.rts.movement.components.pursuit;
export import engine.gameplay.common.physics.components.bounce_sound;
export import engine.gameplay.common.physics.resources.fall_damage;
export import engine.gameplay.common.physics.resources.landings;
import Engine.Core.Math.FixedRandom;

// Moves every mobile entity one tick along its move order, chunk-parallel (a
// ground mover going to a point walks its route's points, waiting the tick its
// route is planned; the others go straight):
// each entity only writes its own components, reads the shared waypoint
// graph and ground, and draws path choices from a stream keyed by tick and
// entity, so the result does not depend on thread count or chunk order.
// A mover on a deck (SurfaceLayer) stands at the deck's height where it is over the deck (TerrainLogic::getLayerHeight);
// reaching a route point it takes that point's layer (AIUpdate's pathfinder->updateLayer from its path).
// A unit whose AI weighs what it runs into (BlockedState) counts the ticks it is blocked, and along its route creeps behind
// the unit in its way (AIUpdateInterface::doLocomotor: BeginBlockedTick, BlockedSpeed, EndBlockedTick).
// A unit making way (MoveAway: the temporary AI_MOVE_OUT_OF_THE_WAY state) follows that order and route instead of its own,
// which waits (its endings are not its own order's); reaching the end it is done.
// A unit its AI asks to go slower (DesiredSpeed) has its locomotor's maximum capped by it.
export namespace engine::gameplay
{
// MoveOrder::replan done with: a route planned for it this tick (for its destination), or no route to plan (no Route of
// its own yet, or no point to go to).
inline bool ReplanServed(const Route *route, const MoveOrder &order, std::uint64_t tick) noexcept
{
	return route == nullptr || order.mode != MoveMode::Point || (route->planned && route->plannedTick == tick && route->destination == order.destination);
}

struct MovementSystem
{
	using Query = ecs::Query<ecs::Write<Transform>, ecs::Write<Locomotion>, ecs::Write<MoveOrder>, ecs::Exclude<OffMap>, ecs::Exclude<Carried>, ecs::Exclude<Descent>, ecs::Optional<Disabled>,
		ecs::Optional<NavigationAgent>, ecs::OptionalWrite<Route>, ecs::Optional<IgnoredObstacle>, ecs::OptionalWrite<PathCompleted>, ecs::OptionalWrite<MoveEnded>,
		ecs::Optional<WanderAnchor>, ecs::OptionalWrite<PhysicsBody>, ecs::Optional<SurfaceLayer>, ecs::Optional<FloorLift>, ecs::OptionalWrite<BlockedState>,
		ecs::OptionalWrite<BlockContact>, ecs::OptionalWrite<MoveAway>, ecs::Optional<DesiredSpeed>, ecs::Optional<Pursuit>, ecs::OptionalWrite<Attitude>,
		ecs::Optional<BounceSound>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<WaypointGraph>, ecs::Read<GroundHeight>, ecs::Read<DeckSurfaces>, ecs::Read<PhysicsSettings>,
		ecs::Write<LocomotorFalls>, ecs::Write<LocomotorLandings>>;

	// The tick's landings and hard falls of the bodies flown here (none held by the world: none kept).
	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		// (Declared for writing; looked up so a world without them flies nothing to report.)
		if (auto *falls = const_cast<LocomotorFalls *>(context.Find<LocomotorFalls>()))
			falls->Reset(query.PreparedChunkCount());
		if (auto *landings = const_cast<LocomotorLandings *>(context.Find<LocomotorLandings>()))
			landings->Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const GroundHeight &ground = context.Read<GroundHeight>();
		const DeckSurfaces &decks = context.Read<DeckSurfaces>();
		const WaypointGraph &waypoints = context.Read<WaypointGraph>();
		const std::uint64_t seed = context.Read<RandomSeed>().value;
		auto transforms = chunk.Get<Transform>();
		auto motions = chunk.Get<Locomotion>();
		auto orders = chunk.Get<MoveOrder>();
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto agents = chunk.Get<NavigationAgent>();
		auto routes = chunk.Get<Route>();
		const auto ignoredRows = chunk.Get<IgnoredObstacle>();
		auto completions = chunk.Get<PathCompleted>();
		auto endings = chunk.Get<MoveEnded>();
		const auto anchors = chunk.Get<WanderAnchor>();
		auto bodies = chunk.Get<PhysicsBody>();
		// The world's physics (none: nothing is flown by forces).
		const PhysicsSettings *physics = context.Find<PhysicsSettings>();
		const auto layerRows = chunk.Get<SurfaceLayer>();
		const auto lifts = chunk.Get<FloorLift>();
		auto blockedStates = chunk.Get<BlockedState>();
		auto blockContacts = chunk.Get<BlockContact>();
		auto moveAways = chunk.Get<MoveAway>();
		const auto desiredSpeeds = chunk.Get<DesiredSpeed>();
		const auto pursuitRows = chunk.Get<Pursuit>();
		auto attitudes = chunk.Get<Attitude>();
		const auto bounceSounds = chunk.Get<BounceSound>();
		auto *falls = physics != nullptr ? const_cast<LocomotorFalls *>(context.Find<LocomotorFalls>()) : nullptr;
		auto *landed = physics != nullptr ? const_cast<LocomotorLandings *>(context.Find<LocomotorLandings>()) : nullptr;
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			// MoveOrder::replan: let go once the route requests have planned for it (this tick, for this destination), or when
			// it has no route to be planned (no Route of its own yet, or no point to go to).
			if (orders[row].replan != 0 && ReplanServed(routes.empty() ? nullptr : &routes[row], orders[row], context.Tick()))
				orders[row].replan = 0;
			std::uint8_t layer = layerRows.empty() ? GroundLayer : layerRows[row].layer;
			// Its layer's height (and surface: a deck over water is the deck); on a raised floor (a carrier's deck) that much higher.
			const Engine::Math::Fixed lift = lifts.empty() ? Engine::Math::Fixed{} : lifts[row].height;
			const auto floor = [&](Engine::Math::FixedVector2 at) { return (layer == GroundLayer ? ground.At(at) : LayerHeight(decks, ground, at, layer)) + lift; };
			const auto surface = [&](Engine::Math::FixedVector2 at) {
				if (layer == GroundLayer)
					return ground.Surface(at);
				const Engine::Math::Fixed deck = LayerHeight(decks, ground, at, layer), water = ground.Surface(at);
				return deck > ground.At(at) ? std::max(deck, water) : water;
			};
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::Held))
				continue;
			// doLocomotor: blocked by the last tick's collisions, one more tick blocked; it ends by letting go of the speed
			// they allowed.
			Blocking blocking{blockedStates.empty() ? nullptr : &blockedStates[row], blockContacts.empty() ? nullptr : &blockContacts[row]};
			blocking.desired = desiredSpeeds.empty() ? FastAsPossible : desiredSpeeds[row].speed;
			// AIAttackPursueTargetState::setDesiredSpeed: while it pursues, the speed its chase asks for (none matched: as
			// fast as it can), over any other.
			if (!pursuitRows.empty() && pursuitRows[row].active != 0)
				blocking.desired = pursuitRows[row].matched != 0 ? pursuitRows[row].speed : FastAsPossible;
			if (blocking.state != nullptr && blocking.contact != nullptr)
				blocking.blocked = BeginBlockedTick(*blocking.state, *blocking.contact);
			// Held (DISABLED_HELD): nothing moves it nor sets it on the ground (PhysicsBehavior::update integrates nothing,
			// Locomotor::handleBehaviorZ leaves it be); whoever holds it places it.
			if (!disabledRows.empty() && (disabledRows[row].mask & disabled_type::Held) != 0)
			{
				motions[row].speed = {};
				continue;
			}
			// Pushed by a shock wave: its locomotor lets go until physics has set it down (Locomotor's getIsStunned).
			if (!bodies.empty() && (bodies[row].Has(physics_flag::Pushed) || bodies[row].Has(physics_flag::PhysicsDriven)))
				continue;
			Transform &transform = transforms[row];
			Locomotion &motion = motions[row];
			MoveAway *away = moveAways.empty() ? nullptr : &moveAways[row];
			MoveOrder &order = away != nullptr ? away->order : orders[row];
			Route *route = away != nullptr ? &away->route : (routes.empty() ? nullptr : &routes[row]);
			const MoveMode before = order.mode;
			motion.turning = 0; // until it steers this tick
			const auto where = transform.position.XY();
			// Above the ground (or deck) under it (Thing::isAboveTerrain: a carrier's raised floor is not its terrain).
			const bool aloft = transform.position.z > floor(where) - lift;
			// Flown by forces (a flyer with a body: flight_forces): its locomotor pushes its body, which then steps; else the
			// kinematic locomotion.
			FlightStep flightStep;
			FlightStep *flight = nullptr;
			if (physics != nullptr && !bodies.empty() && FliesByForce(motion.locomotor))
			{
				flightStep.body = &bodies[row];
				flightStep.settings = physics;
				flightStep.ground = &ground;
				flightStep.terrain = floor(where) - lift;
				flightStep.surface = motion.locomotor.height == HeightBehavior::RelativeToHighestLayer ? flightStep.terrain : surface(where);
				flightStep.tick = context.Tick();
				flightStep.aloft = aloft;
				flight = &flightStep;
				BeginFlight(transform, motion, bodies[row]);
			}
			else
				motion.forced = 0;
			// Its height for the tick: kinematic, or its body stepped by physics.
			const auto settle = [&](Engine::Math::FixedVector2 at) {
				if (flight != nullptr)
				{
					// PhysicsBehavior::update's landing (its bounce sound) and hard fall (falling damage), as the physics step's.
					const StepResult step = EndFlight(transform, motion, *flight, [&](Engine::Math::FixedVector2 under) { return floor(under); },
						attitudes.empty() ? nullptr : &attitudes[row]);
					if (step.landed && landed != nullptr && !bounceSounds.empty())
						landed->Slot(context).push_back({entities[row], bounceSounds[row].sound, transform.position});
					if (step.fall > Engine::Math::Fixed{} && falls != nullptr)
						falls->Slot(context).push_back({entities[row], entities[row], step.fall * flight->body->mass * flight->body->fallDamageFactor,
							physics->fallDamageType, physics->fallDeathType});
				}
				else
					HoldHeight(transform, motion, floor(at), surface(at));
			};
			if (IsParked(motion, order.mode == MoveMode::Idle))
			{
				transform.position.z = floor(where);
				continue;
			}
			if (order.held != 0)
			{
				// setLocomotorGoalNone: no goal this tick; it brakes, its order kept.
				if (IsParked(motion, true))
				{
					transform.position.z = floor(where);
					continue;
				}
				MoveOrder none{};
				none.destination = where;
				Step(waypoints, seed, transform, motion, none, entities[row], context.Tick(), anchors.empty() ? nullptr : &anchors[row], nullptr, aloft, flight);
				settle(transform.position.XY());
				continue;
			}
			if (motion.vertical)
			{
				// Straight up or down: its orders wait (ChinookTakeoffOrLandingState scrubs its drift).
				if (flight != nullptr)
					FlightVertical(transform, motion, *flight);
				else
					motion.speed = {};
				settle(where);
				continue;
			}
			if (!agents.empty() && RoutedMode(order.mode))
			{
				// A waypoint path (AIFollowWaypointPathState, AIWanderState, AIPanicState, AIWanderInPlaceState): each leg a
				// routed move (AIInternalMoveToState) to its goal; a leg done, on to the next (its route planned afresh).
				const MoveMode mode = order.mode;
				const std::uint32_t waypoint = order.waypoint;
				const bool legs = mode != MoveMode::Point;
				const bool finalLeg = !legs || (mode != MoveMode::WanderInPlace && waypoints.Links(order.waypoint).empty());
				const auto reached = FollowRoute(transform, motion, order, route, context.Tick(), &blocking, aloft, flight, finalLeg);
				if (legs && order.mode == MoveMode::Idle)
				{
					order.mode = mode;
					Arrive(waypoints, seed, transform, motion, order, entities[row], context.Tick(), anchors.empty() ? nullptr : &anchors[row], &decks);
					if (route != nullptr)
						route->planned = false;
					if (order.mode == MoveMode::Idle)
					{
						const PathCompleted completed{waypoint, 0, context.Tick()};
						if (!completions.empty())
							completions[row] = completed;
						else
							context.Commands().Add<PathCompleted>(entities[row], completed);
					}
				}
				// A route point reached puts it on that point's layer.
				if (reached && *reached != layer)
				{
					if (*reached == GroundLayer)
						context.Commands().Remove<SurfaceLayer>(entities[row]);
					else if (layerRows.empty())
						context.Commands().Add<SurfaceLayer>(entities[row], SurfaceLayer{*reached});
					else
						context.Commands().Set<SurfaceLayer>(entities[row], SurfaceLayer{*reached});
					layer = *reached;
				}
			}
			else
			{
				// Along a path: its last waypoint reached, the path is completed (setCompletedWaypoint).
				const bool pathing = order.mode == MoveMode::Path || order.mode == MoveMode::PathExact || Wandering(order.mode);
				const std::uint32_t waypoint = order.waypoint;
				Step(waypoints, seed, transform, motion, order, entities[row], context.Tick(), anchors.empty() ? nullptr : &anchors[row], &blocking, aloft, flight,
					&decks);
				if (pathing && order.mode == MoveMode::Idle)
				{
					const PathCompleted completed{waypoint, 0, context.Tick()};
					if (!completions.empty())
						completions[row] = completed;
					else
						context.Commands().Add<PathCompleted>(entities[row], completed);
				}
			}
			// Made way: done (its own order resumes once the move ends).
			if (away != nullptr)
			{
				if (before != MoveMode::Idle && order.mode == MoveMode::Idle)
					away->done = 1;
				settle(transform.position.XY());
				continue;
			}
			// Where it was going reached: it has left the obstacle it walked out of.
			if (!ignoredRows.empty() && order.mode == MoveMode::Idle)
				context.Commands().Remove<IgnoredObstacle>(entities[row]);
			// Its move ended this tick (only a stepped order ends: the rows left above keep theirs).
			if (before != MoveMode::Idle && order.mode == MoveMode::Idle)
			{
				if (!endings.empty())
					endings[row].tick = context.Tick();
				else
					context.Commands().Add<MoveEnded>(entities[row], MoveEnded{context.Tick()});
			}
			settle(transform.position.XY());
		}
	}

	// Along a route to a point: its next point, the destination at its last (a route that could not reach
	// the destination ends where it got nearest); none planned for this destination yet: wait for it.
	// The layer of the point it reached this tick, if it reached one.
	// A unit's blocking for the tick (none without a BlockedState), let go of when the tick is done with it.
	struct Blocking
	{
		BlockedState *state{nullptr};
		BlockContact *contact{nullptr};
		bool blocked{false};
		Engine::Math::Fixed desired{FastAsPossible}; // m_desiredSpeed
		~Blocking()
		{
			if (state != nullptr && contact != nullptr)
				EndBlockedTick(*state, *contact, blocked);
		}
	};

	static std::optional<std::uint8_t> FollowRoute(Transform &transform, Locomotion &motion, MoveOrder &order, Route *route, std::uint64_t tick,
		Blocking *blocking = nullptr, bool aloft = true, FlightStep *flight = nullptr, bool finalLeg = true)
	{
		if (route == nullptr || !route->planned || route->next >= route->count)
		{
			// A planned route with nowhere to go: there is no getting nearer, the move is over.
			if (route != nullptr && route->planned && route->complete && route->count == 0)
				order.mode = MoveMode::Idle;
			if (flight != nullptr)
				FlightMaintain(transform, motion, *flight);
			else
				Coast(transform, motion, aloft);
			return std::nullopt;
		}
		const bool last = route->complete && route->next + 1u >= route->count;
		// A leg of a waypoint path with more waypoints after it (setPathExtraDistance): no slowing for its end.
		const bool ending = last && finalLeg;
		// The route's end follows a destination that moved since it was planned (until it is planned again).
		const bool moved = route->destination != order.destination;
		const Engine::Math::FixedVector2 goal = last && moved ? order.destination : route->points[route->next];
		bool reached = false;
		if (flight != nullptr)
		{
			// Flown: on along its route at the speed its AI asks for.
			Engine::Math::Fixed onPath = Engine::Math::Distance(transform.position.XY(), goal);
			for (std::uint32_t point = route->next + 1u; point < route->count; ++point)
				onPath += Engine::Math::Distance(route->points[point - 1u], route->points[point]);
			if (!route->complete || !finalLeg)
				onPath += Engine::Math::Fixed::FromInt(100000);
			const Engine::Math::Fixed speed = blocking != nullptr && blocking->desired < motion.locomotor.maxSpeed ? blocking->desired : motion.locomotor.maxSpeed;
			reached = FlightSteer(transform, motion, *flight, goal, ending, onPath, speed);
		}
		else if (IsGroundLocomotor(motion.locomotor))
		{
			// onPathDistToGoal: what is left along the route (a route not planned to its end: far).
			Engine::Math::Fixed onPath = Engine::Math::Distance(transform.position.XY(), goal);
			for (std::uint32_t point = route->next + 1u; point < route->count; ++point)
				onPath += Engine::Math::Distance(route->points[point - 1u], route->points[point]);
			if (!route->complete || !finalLeg)
				onPath += Engine::Math::Fixed::FromInt(100000);
			// Along its route: no faster than the unit in its way allows.
			Engine::Math::Fixed speed = motion.locomotor.maxSpeed;
			if (blocking != nullptr && blocking->desired < speed)
				speed = blocking->desired;
			bool *blocked = nullptr;
			if (blocking != nullptr && blocking->state != nullptr && blocking->contact != nullptr)
			{
				speed = BlockedSpeed(*blocking->state, *blocking->contact, speed, blocking->blocked);
				blocked = &blocking->blocked;
			}
			reached = SteerGround(transform, motion, goal, ending, onPath, tick, speed, blocked);
		}
		else
			reached = Steer(transform, motion, goal, ending);
		if (!reached)
			return std::nullopt;
		const std::uint8_t layer = route->layers[route->next];
		++route->next;
		if (last)
			order.mode = MoveMode::Idle;
		return layer;
	}

	static void Step(const WaypointGraph &waypoints, std::uint64_t seed, Transform &transform, Locomotion &motion, MoveOrder &order,
		ecs::Entity entity, std::uint64_t tick, const WanderAnchor *anchor = nullptr, Blocking *blocking = nullptr, bool aloft = true,
		FlightStep *flight = nullptr, const DeckSurfaces *decks = nullptr)
	{
		if (order.mode == MoveMode::Idle)
		{
			if (flight != nullptr)
				FlightMaintain(transform, motion, *flight);
			else
				Coast(transform, motion, aloft);
			return;
		}
		if (order.mode == MoveMode::Face || order.mode == MoveMode::FaceObject)
		{
			if (flight != nullptr ? FlightFace(transform, motion, *flight, order.destination) : Face(transform, motion, order.destination))
				order.mode = MoveMode::Idle;
			return;
		}
		const bool pathing = order.mode == MoveMode::Path || order.mode == MoveMode::PathExact || Wandering(order.mode);
		const bool final = !pathing || waypoints.Links(order.waypoint).empty();
		bool reached = false;
		if (flight != nullptr)
		{
			const Engine::Math::Fixed left = Engine::Math::Distance(transform.position.XY(), order.destination);
			const Engine::Math::Fixed speed = blocking != nullptr && blocking->desired < motion.locomotor.maxSpeed ? blocking->desired : motion.locomotor.maxSpeed;
			reached = FlightSteer(transform, motion, *flight, order.destination, final, final ? left : left + Engine::Math::Fixed::FromInt(100000), speed,
				order.explicitGoal != 0);
		}
		else if (IsGroundLocomotor(motion.locomotor))
		{
			const Engine::Math::Fixed left = Engine::Math::Distance(transform.position.XY(), order.destination);
			// Straight at its goal (POSITION_EXPLICIT): its locomotor knows it is blocked, its speed is its own.
			bool *blocked = blocking != nullptr && blocking->state != nullptr && blocking->contact != nullptr ? &blocking->blocked : nullptr;
			const Engine::Math::Fixed speed = blocking != nullptr && blocking->desired < motion.locomotor.maxSpeed ? blocking->desired : motion.locomotor.maxSpeed;
			reached = SteerGround(transform, motion, order.destination, final, final ? left : left + Engine::Math::Fixed::FromInt(100000), tick, speed,
				blocked);
		}
		else
			reached = Steer(transform, motion, order.destination, final);
		if (!reached)
			return;
		Arrive(waypoints, seed, transform, motion, order, entity, tick, anchor, decks);
	}

	// Its goal reached (AIFollowWaypointPathState::update on its internal move's success, AIWanderInPlaceState): wandering
	// in place, on to another point about its anchor; at a path's last waypoint, idle; else on along one of the
	// waypoint's links, its next goal there (computeGoal).
	static void Arrive(const WaypointGraph &waypoints, std::uint64_t seed, Transform &transform, Locomotion &motion, MoveOrder &order, ecs::Entity entity,
		std::uint64_t tick, const WanderAnchor *anchor, const DeckSurfaces *decks)
	{
		const bool pathing = order.mode == MoveMode::Path || order.mode == MoveMode::PathExact || Wandering(order.mode);
		const bool final = !pathing || waypoints.Links(order.waypoint).empty();
		auto random = Engine::Math::Stream(seed, {tick, entity.index, entity.generation});
		const auto uniform = [&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(random, low, high); };
		// Wandering in place: never done; on to another point about its anchor.
		if (order.mode == MoveMode::WanderInPlace)
		{
			const Engine::Math::FixedVector2 origin = anchor != nullptr ? anchor->origin : transform.position.XY();
			order.destination = origin + WanderOffset(PointWanderCells(motion.locomotor.wanderAboutPointRadius, PathfindCellSize), PathfindCellSize, uniform);
			order.goalLayer = GroundLayer;
			return;
		}
		if (final)
		{
			order.mode = MoveMode::Idle;
			return;
		}
		// At a waypoint: on along one of its links, chosen at random as the original (exactly: its first).
		const auto links = waypoints.Links(order.waypoint);
		if (order.mode == MoveMode::PathExact)
			order.waypoint = links.front();
		else
			order.waypoint = links[static_cast<std::size_t>(uniform(0, static_cast<std::int64_t>(links.size()) - 1))];
		order.destination = waypoints.Position(order.waypoint).XY();
		// Wandering: this waypoint's goal off it by a new offset (computeGoal: a waypoint on the wall keeps its goal on it).
		if (Wandering(order.mode))
			if (const std::int64_t cells = PathWanderCells(motion.locomotor.wanderWidth); cells > 0)
			{
				const Engine::Math::FixedVector2 offsetGoal = order.destination + WanderOffset(cells, PathfindCellSize, uniform);
				order.destination = decks != nullptr ? WaypointGoalOnWall(*decks, order.destination, offsetGoal) : offsetGoal;
			}
		// computeGoal's m_goalLayer: the ground, or the wall for a waypoint on it.
		order.goalLayer = decks != nullptr ? WaypointGoalLayer(*decks, waypoints.Position(order.waypoint).XY()) : GroundLayer;
	}
};

// Starts following the path at `waypoint` (exactly: each waypoint's first link on).
inline MoveOrder FollowPath(const WaypointGraph &waypoints, std::uint32_t waypoint, bool exact = false) noexcept
{
	if (waypoint == WaypointGraph::None)
		return {};
	return {waypoints.Position(waypoint).XY(), waypoint, exact ? MoveMode::PathExact : MoveMode::Path};
}
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MovementSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.movement";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
