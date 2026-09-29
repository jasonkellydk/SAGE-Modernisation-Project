export module engine.gameplay.rts.movement.systems.route_request_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;
export import engine.gameplay.rts.navigation.algorithms.route_search;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;

// Plans routes, chunk-parallel: a ground mover going to a point without a
// route (or, at least RouteReplanTicks after its route was planned, at the end
// of a partial one or with a destination moved over two cells) gets one from
// where it stands, over its surfaces' clearance plane. Each chunk
// searches with its own scratch (kept between ticks), reads only the grid and
// writes its own entities' routes, so the result is the same on any number
// of workers.
export namespace engine::gameplay
{
struct RouteScratchPool
{
	std::vector<std::unique_ptr<RouteScratch>> slots;

	void Reset(std::size_t chunks)
	{
		while (slots.size() < chunks)
			slots.push_back(std::make_unique<RouteScratch>());
	}
	RouteScratch &Slot(const ecs::SystemContext &context) { return *slots.at(context.ChunkOrder()); }
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

// A route for `destination`, from `from`, into `route` (its first RoutePoints points; the rest when those are walked).
// `ignored`: an obstacle whose cells are open to it (IgnoredObstacle).
inline void PlanRoute(Route &route, const NavigationGrid &grid, const ClearancePlane &plane, const NavigationAgent &agent,
	Engine::Math::FixedVector2 from, Engine::Math::FixedVector2 destination, RouteScratch &scratch, std::uint64_t tick, ecs::Entity ignored = {},
	std::uint8_t fromLayer = GroundLayer)
{
	const std::uint8_t toLayer = RouteGoalLayer(grid, plane, agent.radius, fromLayer, destination);
	auto planned = FindRoute(grid, plane, RouteMover{agent.radius, 5000, ignored}, from, destination, scratch, fromLayer, toLayer);
	// Wedged in where its size does not fit: it moves out as a smaller mover would.
	for (std::uint8_t radius = agent.radius; !planned.reachedGoal && !planned.exhausted && planned.points.size() <= 1 && radius > 0;)
		planned = FindRoute(grid, plane, RouteMover{--radius, 5000, ignored}, from, destination, scratch, fromLayer, toLayer);
	route = {};
	route.destination = destination;
	route.planned = true;
	route.plannedTick = tick;
	// The first point is where the mover stands: it walks from the second.
	const std::size_t available = planned.points.size() > 1 ? planned.points.size() - 1 : 0;
	const std::size_t kept = std::min(available, RoutePoints);
	for (std::size_t index = 0; index < kept; ++index)
	{
		route.points[index] = planned.points[index + 1];
		route.layers[index] = index + 1 < planned.layers.size() ? planned.layers[index + 1] : GroundLayer;
	}
	route.count = static_cast<std::uint8_t>(kept);
	// Cut short by the budget: walked, then planned on from its end; with nowhere better to go, the move is done.
	route.complete = kept == available && (!planned.exhausted || kept == 0);
}

struct RouteRequestSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<MoveOrder>, ecs::Read<NavigationAgent>, ecs::OptionalWrite<Route>, ecs::Optional<IgnoredObstacle>,
		ecs::Optional<SurfaceLayer>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<NavigationGrid>, ecs::Write<RouteScratchPool>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<RouteScratchPool>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		const auto transforms = chunk.Get<Transform>();
		const auto orders = chunk.Get<MoveOrder>();
		const auto agents = chunk.Get<NavigationAgent>();
		auto routes = chunk.Get<Route>();
		const auto ignoredRows = chunk.Get<IgnoredObstacle>();
		const auto layerRows = chunk.Get<SurfaceLayer>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < orders.size(); ++row)
		{
			const MoveOrder &order = orders[row];
			if (order.mode != MoveMode::Point)
				continue;
			const ClearancePlane *plane = grid.ClearanceFor(agents[row].surfaces);
			if (plane == nullptr)
				continue;
			Route *current = routes.empty() ? nullptr : &routes[row];
			const std::uint64_t tick = context.Tick();
			// Planned again at most every RouteReplanTicks: for a destination that moved, or past a partial route's end.
			const bool stale = current == nullptr || !current->planned ||
				(tick >= current->plannedTick + RouteReplanTicks &&
					(Engine::Math::DistanceSquared(current->destination, order.destination) > Engine::Math::Fixed::FromInt(20 * 20) ||
						(!current->complete && current->next >= current->count)));
			if (!stale)
				continue;
			Route route;
			PlanRoute(route, grid, *plane, agents[row], transforms[row].position.XY(), order.destination, context.Write<RouteScratchPool>().Slot(context), context.Tick(),
				ignoredRows.empty() ? ecs::Entity{} : ignoredRows[row].obstacle, layerRows.empty() ? GroundLayer : layerRows[row].layer);
			if (current != nullptr)
				*current = route;
			else
				context.Commands().Add<Route>(entities[row], route);
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
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
