export module engine.gameplay.rts.movement.systems.route_request_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.move_goal;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;
export import engine.gameplay.rts.navigation.algorithms.route_search;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.ecs.system.job_pool;
export import engine.gameplay.rts.navigation.resources.unit_cells;
export import engine.gameplay.rts.navigation.resources.goal_cells;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.rts.movement.components.attack_approach;
export import engine.gameplay.rts.navigation.algorithms.attack_view;

// Plans routes: a ground mover going to a point without a route (or, at least
// RouteReplanTicks after its route was planned, at the end of a partial one or
// with a destination moved over two cells) gets one from where it stands, over
// its surfaces' clearance plane, to its MoveGoal's goal while that is for the order's point (the destination
// adjusted off others' claims), else to the order's point. The tick's requests are gathered in chunk and
// row order, then planned one job each on the pool (a group order's searches
// share out instead of queueing in one chunk's job), each with its own scratch
// (kept between ticks), reading only the grid; the routes are then written back
// in request order, so the result is the same on any number of workers.
// Where the world keeps the units' cells (UnitCells, GoalCells, Relationships), a route weighs the units in its way as
// the mover sees them (its player and team, how it crushes: RouteUnits).
export namespace engine::gameplay
{
struct RouteScratchPool
{
	std::vector<std::unique_ptr<RouteScratch>> slots;

	void Reset(std::size_t count)
	{
		while (slots.size() < count)
			slots.push_back(std::make_unique<RouteScratch>());
	}
	RouteScratch &Slot(std::size_t index) { return *slots.at(index); }

	// The tick's requests (working space, kept between ticks).
	struct Request
	{
		ecs::Entity entity;
		Route *current{nullptr}; // its route, when it has one
		const ClearancePlane *plane{nullptr};
		NavigationAgent agent;
		Engine::Math::FixedVector2 from, destination, goal;
		ecs::Entity ignored;
		std::uint8_t layer{0};
		RouteUnits units;
		bool weighsUnits{false};
		Route planned;
		std::optional<AttackApproach> attack; // an attack's route to a spot it may fire from
		std::uint8_t goalLayer{NoGoalLayer};  // its order's goal layer (MoveOrder::goalLayer)
	};
	std::vector<Request> requests;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::RouteScratchPool>
{
	static constexpr std::string_view StableName = "engine.gameplay.route_scratch_pool";
};

}

export namespace engine::gameplay
{

// Whether a mover's route is planned (again) this run: none yet or none planned (a writer cleared Route::planned), a new
// order asking for its route at once (MoveOrder::replan), or RouteReplanTicks on from its last with the destination moved
// over two cells or a partial route walked to its end.
inline bool RouteStale(const Route *current, const MoveOrder &order, std::uint64_t tick) noexcept
{
	return current == nullptr || !current->planned || order.replan != 0 ||
		(tick >= current->plannedTick + RouteReplanTicks &&
			(Engine::Math::DistanceSquared(current->destination, order.destination) > Engine::Math::Fixed::FromInt(20 * 20) ||
				(!current->complete && current->next >= current->count)));
}

// Pathfinder::findAttackPath's search: no more than this many cells taken in.
inline constexpr std::uint32_t AttackRouteBudget = 2500;

// findAttackPath's goal test for cell (x, y), its point the cell's centre on the ground there (adjustCoordToCell): half a
// cell or more from where the mover stands, its weapon reaching the victim from there (isGoalPosWithinAttackRange:
// FROM_BOUNDINGSPHERE_2D, the gap between the bounding circles within its range less a quarter cell) and its view from
// there clear (isAttackViewBlockedByObstacle).
// Weapon::isGoalPosWithinAttackRange from `point`: the gap between the bounding circles within the range less a quarter
// cell, and not under the minimum range plus a quarter cell.
inline bool AttackGoalInRange(const AttackApproach &attack, Engine::Math::FixedVector2 point) noexcept
{
	using Engine::Math::Fixed;
	const Fixed cell = Fixed::FromInt(PathfindCellSize);
	Fixed gap = Engine::Math::Distance(point, attack.victimAt.XY()) - attack.ownRadius - attack.victimRadius;
	if (gap < Fixed{})
		gap = Fixed{};
	const Fixed closest = attack.minimumRange + cell / Fixed::FromInt(4);
	return gap <= attack.goalRange && gap >= closest;
}

inline bool AttackSpot(const NavigationGrid &grid, const GroundHeight &ground, const AttackApproach &attack, ecs::Entity self,
	Engine::Math::FixedVector2 from, std::int32_t x, std::int32_t y)
{
	using Engine::Math::Fixed;
	const Fixed cell = Fixed::FromInt(PathfindCellSize);
	const Engine::Math::FixedVector2 point{Fixed::FromInt(x) * cell + cell / Fixed::FromInt(2), Fixed::FromInt(y) * cell + cell / Fixed::FromInt(2)};
	const Fixed half = cell / Fixed::FromInt(2);
	if (Engine::Math::DistanceSquared(point, from) < half * half)
		return false;
	if (!AttackGoalInRange(attack, point))
		return false;
	if (attack.sight == 0)
		return true;
	// (The skip and the layer as the mover stands now: findAttackPath asks with the object itself.)
	const SightEye eye{{point.x, point.y, ground.At(point)}, attack.eyeTop, attack.weaponTerrain != 0, attack.skipCount, attack.viewLayer};
	const SightTarget victim{attack.victimAt, attack.victimTop, attack.victimCentre};
	return !AttackViewBlocked(ground, grid, eye, victim, ViewIgnores{self, attack.victim, attack.victimSlaver, attack.container, attack.slaver});
}

// A route for `destination`, from `from`, into `route` (its first RoutePoints points; the rest when those are walked),
// ending at `goal` (none: the destination itself). `ignored`: an obstacle whose cells are open to it (IgnoredObstacle).
inline void PlanRoute(Route &route, const NavigationGrid &grid, const ClearancePlane &plane, const NavigationAgent &agent,
	Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 destination, RouteScratch &scratch, std::uint64_t tick, ecs::Entity ignored = {},
	std::uint8_t fromLayer = GroundLayer, std::optional<Engine::Math::FixedVector2> goal = std::nullopt, const RouteUnits *units = nullptr,
	const AttackApproach *attack = nullptr, const GroundHeight *ground = nullptr, ecs::Entity self = {}, std::uint8_t goalLayer = NoGoalLayer)
{
	const Engine::Math::FixedVector2 to = goal.value_or(destination);
	// Pathfinder::findPath's destinationLayer: the order's own when it gives one (a ground mover sent onto the wall finds
	// no way up: the route ends at the reached cell nearest the goal, as findClosestPath), else found from the move.
	const std::uint8_t toLayer = goalLayer != NoGoalLayer && goalLayer <= grid.Decks().size() ? goalLayer : RouteGoalLayer(grid, plane, agent.radius, fromLayer, to);
	auto planned = attack != nullptr && ground != nullptr
		? SearchRoute(grid, plane, RouteMover{agent.radius, AttackRouteBudget, ignored}, from, to, scratch, fromLayer, toLayer, units, true,
			  [&](std::int32_t x, std::int32_t y) { return AttackSpot(grid, *ground, *attack, self, from, x, y); })
		: FindRoute(grid, plane, RouteMover{agent.radius, 5000, ignored}, from, to, scratch, fromLayer, toLayer, units);
	// computeAttackPath: the search found no spot it may fire from (its route's end out of reach) and the move would be under
	// three cells: it takes a closest path to where it stands instead (to unstack it from a unit it is on top of).
	if (attack != nullptr && ground != nullptr && !planned.points.empty())
	{
		const Engine::Math::FixedVector2 end = planned.points.back();
		const Engine::Math::Fixed shortMove = Engine::Math::Fixed::FromInt(PathfindCellSize * 3);
		if (!AttackGoalInRange(*attack, end) && Engine::Math::DistanceSquared(end, from) < shortMove * shortMove)
			planned = FindRoute(grid, plane, RouteMover{agent.radius, 5000, ignored}, from, from, scratch, fromLayer, fromLayer, units);
	}
	// Wedged in where its size does not fit: it moves out as a smaller mover would.
	for (std::uint8_t radius = agent.radius; !planned.reachedGoal && !planned.exhausted && planned.points.size() <= 1 && radius > 0;)
		planned = FindRoute(grid, plane, RouteMover{--radius, 5000, ignored}, from, to, scratch, fromLayer, toLayer, units);
	route = {};
	route.destination = destination;
	route.planned = true;
	route.plannedTick = tick;
	route.start = from;
	route.hasStart = true;
	// The first point is where the mover stands: it walks from the second.
	const std::size_t available = planned.points.size() > 1 ? planned.points.size() - 1 : 0;
	const std::size_t kept = std::min(available, RoutePoints);
	for (std::size_t index = 0; index < kept; ++index)
	{
		route.points[index] = planned.points[index + 1];
		route.layers[index] = index + 1 < planned.layers.size() ? planned.layers[index + 1] : GroundLayer;
	}
	route.count = static_cast<std::uint8_t>(kept);
	route.blockedByAlly = planned.blockedByAlly;
	// Cut short by the budget: walked, then planned on from its end; with nowhere better to go (no point, or none past the
	// cell beside where it stands: findClosestPath's route, walked, ends the move), the move is done.
	const Engine::Math::Fixed nearby = Engine::Math::Fixed::FromInt(PathfindCellSize * 3 / 2);
	const bool stuckInPlace = kept == 1 && !planned.reachedGoal && Engine::Math::DistanceSquared(route.points[0], from) <= nearby * nearby;
	route.complete = kept == available && (!planned.exhausted || kept == 0 || stuckInPlace);
}

struct RouteRequestSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<MoveOrder>, ecs::Read<NavigationAgent>, ecs::OptionalWrite<Route>, ecs::Optional<IgnoredObstacle>,
		ecs::Optional<SurfaceLayer>, ecs::Optional<MoveGoal>, ecs::Optional<Owner>, ecs::Optional<TeamMember>, ecs::Optional<Collider>, ecs::Optional<Disabled>,
		ecs::Optional<BlockedState>, ecs::Optional<AttackApproach>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<NavigationGrid>, ecs::Write<RouteScratchPool>, ecs::Read<ecs::JobPool>, ecs::Read<UnitCells>,
		ecs::Read<GoalCells>, ecs::Read<Relationships>, ecs::Read<GroundHeight>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		const GroundHeight *ground = context.Find<GroundHeight>();
		RouteScratchPool &pool = context.Write<RouteScratchPool>();
		const std::uint64_t tick = context.Tick();
		auto &requests = pool.requests;
		requests.clear();
		const UnitCells *unitCells = context.Find<UnitCells>();
		const GoalCells *goalCells = context.Find<GoalCells>();
		const Relationships *relationships = context.Find<Relationships>();
		const bool weighs = unitCells != nullptr && goalCells != nullptr && relationships != nullptr;
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<Transform>();
			const auto orders = chunk.template Get<MoveOrder>();
			const auto agents = chunk.template Get<NavigationAgent>();
			auto routes = chunk.template Get<Route>();
			const auto ignoredRows = chunk.template Get<IgnoredObstacle>();
			const auto layerRows = chunk.template Get<SurfaceLayer>();
			const auto goals = chunk.template Get<MoveGoal>();
			const auto owners = chunk.template Get<Owner>();
			const auto members = chunk.template Get<TeamMember>();
			const auto colliders = chunk.template Get<Collider>();
			const auto disabledRows = chunk.template Get<Disabled>();
			const auto blockedRows = chunk.template Get<BlockedState>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < orders.size(); ++row)
			{
				const MoveOrder &order = orders[row];
				// A point, or a waypoint path's leg (each leg routed: AIInternalMoveToState).
				if (!RoutedMode(order.mode))
					continue;
				const ClearancePlane *plane = grid.ClearanceFor(agents[row].surfaces);
				if (plane == nullptr)
					continue;
				Route *current = routes.empty() ? nullptr : &routes[row];
				// Planned again at most every RouteReplanTicks: for a destination that moved, or past a partial route's end.
				if (!RouteStale(current, order, tick))
					continue;
				const bool adjusted = !goals.empty() && goals[row].ordered == order.destination;
				RouteUnits units;
				units.cells = unitCells;
				units.goals = goalCells;
				units.relationships = relationships;
				units.self = entities[row];
				units.ignored = ignoredRows.empty() ? ecs::Entity{} : ignoredRows[row].obstacle;
				units.player = owners.empty() ? 0u : owners[row].player;
				units.team = members.empty() ? Relationships::NoTeam : members[row].team;
				units.crusherLevel = colliders.empty() ? 0u : colliders[row].crusherLevel;
				units.unmanned = !disabledRows.empty() && (disabledRows[row].mask & disabled_type::Unmanned) != 0;
				units.centered = agents[row].centered != 0;
				units.throughUnits = !blockedRows.empty() && (blockedRows[row].throughUnits | blockedRows[row].docking) != 0;
				if (!blockedRows.empty() && blockedRows[row].ignoring != ecs::Entity{})
					units.ignored = blockedRows[row].ignoring;
				requests.push_back({entities[row], current, plane, agents[row], transforms[row].position.XY(), order.destination,
					adjusted ? goals[row].goal : order.destination, units.ignored, layerRows.empty() ? GroundLayer : layerRows[row].layer, units, weighs,
					Route{}, std::nullopt});
				requests.back().goalLayer = order.goalLayer;
				// An attack approaching a spot to fire from (requestAttackPath): its order is for the victim's position.
				if (const auto attacks = chunk.template Get<AttackApproach>(); !attacks.empty() && attacks[row].active != 0 && attacks[row].search != 0 &&
					attacks[row].victimAt.XY() == order.destination)
					requests.back().attack = attacks[row];
			}
		});
		// Each request planned on its own, shared out over as many jobs as threads (every one taking every n-th request,
		// with a scratch of its own), then written back in request order.
		const ecs::JobPool &jobs = context.Read<ecs::JobPool>();
		const std::size_t threads = jobs.jobs != nullptr ? jobs.jobs->WorkerCount() + 1 : 1;
		const std::size_t groups = std::min(requests.size(), threads);
		pool.Reset(groups);
		ecs::ParallelFor(jobs, groups, [&](std::size_t group) {
			for (std::size_t index = group; index < requests.size(); index += groups)
			{
				RouteScratchPool::Request &request = requests[index];
				PlanRoute(request.planned, grid, *request.plane, request.agent, request.from, request.destination, pool.Slot(group), tick, request.ignored,
					request.layer, request.goal, request.weighsUnits ? &request.units : nullptr, request.attack ? &*request.attack : nullptr, ground,
					request.entity, request.goalLayer);
			}
		});
		for (const RouteScratchPool::Request &request : requests)
		{
			if (request.current != nullptr)
				*request.current = request.planned;
			else
				context.Commands().Add<Route>(request.entity, request.planned);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::RouteRequestSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.route_requests";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Gathers on the caller, shares its searches out on the pool.
	static constexpr bool Batch = true;
	static constexpr bool BorrowsJobs = true;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
