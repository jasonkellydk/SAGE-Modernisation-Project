export module games.generalszh.gameplay.ai.systems.exact_path_follow_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.exact_path_follow;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.desired_speed;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;

// A team's exact path follow, chunk-parallel and stateless, after the tick's movement: a member that moved on to its
// path's next waypoint (its goal the waypoint itself) heads for that waypoint plus its group offset (setPathFromWaypoint
// put every waypoint of its path off by it); one no longer following the path exactly (done, or another order) drops the
// follow and, if it went at the group's speed, goes as fast as it can again (deferred).
export namespace generalszh::gameplay
{
struct ExactPathFollowSystem
{
	using Query = ecs::Query<ecs::Read<ExactPathFollow>, ecs::Write<engine::gameplay::MoveOrder>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::WaypointGraph>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const gp::WaypointGraph &waypoints = context.Read<gp::WaypointGraph>();
		const auto follows = chunk.Get<ExactPathFollow>();
		auto orders = chunk.Get<gp::MoveOrder>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < follows.size(); ++row)
		{
			gp::MoveOrder &order = orders[row];
			if (order.mode != gp::MoveMode::PathExact)
			{
				commands.Remove<ExactPathFollow>(entities[row]);
				if (follows[row].groupSpeed != 0)
					commands.Remove<gp::DesiredSpeed>(entities[row]);
				continue;
			}
			if (order.waypoint != gp::WaypointGraph::None && order.destination == waypoints.Position(order.waypoint).XY())
				order.destination = order.destination + follows[row].offset;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ExactPathFollowSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.exact_path_follow";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
