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
export namespace engine::gameplay
{
struct MovementSystem
{
	using Query = ecs::Query<ecs::Write<Transform>, ecs::Write<Locomotion>, ecs::Write<MoveOrder>, ecs::Exclude<OffMap>, ecs::Exclude<Carried>, ecs::Exclude<Descent>, ecs::Optional<Disabled>,
		ecs::Optional<NavigationAgent>, ecs::OptionalWrite<Route>, ecs::Optional<IgnoredObstacle>, ecs::OptionalWrite<PathCompleted>, ecs::OptionalWrite<MoveEnded>,
		ecs::Optional<WanderAnchor>, ecs::Optional<PhysicsBody>, ecs::Optional<SurfaceLayer>, ecs::Optional<FloorLift>, ecs::OptionalWrite<BlockedState>,
		ecs::OptionalWrite<BlockContact>, ecs::OptionalWrite<MoveAway>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<WaypointGraph>, ecs::Read<GroundHeight>, ecs::Read<DeckSurfaces>>;

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
		const auto bodies = chunk.Get<PhysicsBody>();
		const auto layerRows = chunk.Get<SurfaceLayer>();
		const auto lifts = chunk.Get<FloorLift>();
		auto blockedStates = chunk.Get<BlockedState>();
		auto blockContacts = chunk.Get<BlockContact>();
		auto moveAways = chunk.Get<MoveAway>();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
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
				Step(waypoints, seed, transform, motion, none, entities[row], context.Tick(), anchors.empty() ? nullptr : &anchors[row]);
				const auto stopped = transform.position.XY();
				HoldHeight(transform, motion, floor(stopped), surface(stopped));
				continue;
			}
			if (motion.vertical)
			{
				// Straight up or down: its orders wait (ChinookTakeoffOrLandingState scrubs its drift).
				motion.speed = {};
				HoldHeight(transform, motion, floor(where), surface(where));
				continue;
			}
			if (!agents.empty() && order.mode == MoveMode::Point)
			{
				// A route point reached puts it on that point's layer.
				if (const auto reached = FollowRoute(transform, motion, order, route, context.Tick(), &blocking); reached && *reached != layer)
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
				Step(waypoints, seed, transform, motion, order, entities[row], context.Tick(), anchors.empty() ? nullptr : &anchors[row], &blocking);
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
				const auto next = transform.position.XY();
				HoldHeight(transform, motion, floor(next), surface(next));
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
			const auto next = transform.position.XY();
			HoldHeight(transform, motion, floor(next), surface(next));
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
		~Blocking()
		{
			if (state != nullptr && contact != nullptr)
				EndBlockedTick(*state, *contact, blocked);
		}
	};

	static std::optional<std::uint8_t> FollowRoute(Transform &transform, Locomotion &motion, MoveOrder &order, Route *route, std::uint64_t tick,
		Blocking *blocking = nullptr)
	{
		if (route == nullptr || !route->planned || route->next >= route->count)
		{
			// A planned route with nowhere to go: there is no getting nearer, the move is over.
			if (route != nullptr && route->planned && route->complete && route->count == 0)
				order.mode = MoveMode::Idle;
			Coast(transform, motion);
			return std::nullopt;
		}
		const bool last = route->complete && route->next + 1u >= route->count;
		// The route's end follows a destination that moved since it was planned (until it is planned again).
		const bool moved = route->destination != order.destination;
		const Engine::Math::FixedVector2 goal = last && moved ? order.destination : route->points[route->next];
		bool reached = false;
		if (IsGroundLocomotor(motion.locomotor))
		{
			// onPathDistToGoal: what is left along the route (a route not planned to its end: far).
			Engine::Math::Fixed onPath = Engine::Math::Distance(transform.position.XY(), goal);
			for (std::uint32_t point = route->next + 1u; point < route->count; ++point)
				onPath += Engine::Math::Distance(route->points[point - 1u], route->points[point]);
			if (!route->complete)
				onPath += Engine::Math::Fixed::FromInt(100000);
			// Along its route: no faster than the unit in its way allows.
			Engine::Math::Fixed speed = motion.locomotor.maxSpeed;
			bool *blocked = nullptr;
			if (blocking != nullptr && blocking->state != nullptr && blocking->contact != nullptr)
			{
				speed = BlockedSpeed(*blocking->state, *blocking->contact, speed, blocking->blocked);
				blocked = &blocking->blocked;
			}
			reached = SteerGround(transform, motion, goal, last, onPath, tick, speed, blocked);
		}
		else
			reached = Steer(transform, motion, goal, last);
		if (!reached)
			return std::nullopt;
		const std::uint8_t layer = route->layers[route->next];
		++route->next;
		if (last)
			order.mode = MoveMode::Idle;
		return layer;
	}

	static void Step(const WaypointGraph &waypoints, std::uint64_t seed, Transform &transform, Locomotion &motion, MoveOrder &order,
		ecs::Entity entity, std::uint64_t tick, const WanderAnchor *anchor = nullptr, Blocking *blocking = nullptr)
	{
		if (order.mode == MoveMode::Idle)
		{
			Coast(transform, motion);
			return;
		}
		if (order.mode == MoveMode::Face || order.mode == MoveMode::FaceObject)
		{
			if (Face(transform, motion, order.destination))
				order.mode = MoveMode::Idle;
			return;
		}
		const bool pathing = order.mode == MoveMode::Path || order.mode == MoveMode::PathExact || Wandering(order.mode);
		const bool final = !pathing || waypoints.Links(order.waypoint).empty();
		bool reached = false;
		if (IsGroundLocomotor(motion.locomotor))
		{
			const Engine::Math::Fixed left = Engine::Math::Distance(transform.position.XY(), order.destination);
			// Straight at its goal (POSITION_EXPLICIT): its locomotor knows it is blocked, its speed is its own.
			bool *blocked = blocking != nullptr && blocking->state != nullptr && blocking->contact != nullptr ? &blocking->blocked : nullptr;
			reached = SteerGround(transform, motion, order.destination, final, final ? left : left + Engine::Math::Fixed::FromInt(100000), tick,
				motion.locomotor.maxSpeed, blocked);
		}
		else
			reached = Steer(transform, motion, order.destination, final);
		if (!reached)
			return;
		auto random = Engine::Math::Stream(seed, {tick, entity.index, entity.generation});
		const auto uniform = [&](std::int64_t low, std::int64_t high) { return Engine::Math::UniformInt(random, low, high); };
		// Wandering in place: never done; on to another point about its anchor.
		if (order.mode == MoveMode::WanderInPlace)
		{
			const Engine::Math::FixedVector2 origin = anchor != nullptr ? anchor->origin : transform.position.XY();
			order.destination = origin + WanderOffset(PointWanderCells(motion.locomotor.wanderAboutPointRadius, PathfindCellSize), PathfindCellSize, uniform);
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
		// Wandering: this waypoint's goal off it by a new offset.
		if (Wandering(order.mode))
			if (const std::int64_t cells = PathWanderCells(motion.locomotor.wanderWidth); cells > 0)
				order.destination = order.destination + WanderOffset(cells, PathfindCellSize, uniform);
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
