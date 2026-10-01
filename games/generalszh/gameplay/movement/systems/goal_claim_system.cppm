export module games.generalszh.gameplay.movement.systems.goal_claim_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.move_goal;
export import engine.gameplay.rts.movement.components.move_ended;
export import engine.gameplay.rts.navigation.components.pathfind_goal;

// Goal claims as the last tick's moves went, before this tick's destinations are adjusted (nothing reads the claims in
// between: the original makes them in the same AI update), one mover after another in chunk and row order:
//   a route planned last tick for a move to a point: the route's end is claimed (AIInternalMoveToState::update, the
//   path in: updateGoal on its last node while it adjusts destinations), or, for a leg on the way, the claim let go
//   (removeGoal);
//   a move that ended last tick (AIUpdateInterface::update, m_movementComplete): the claim moves to where it stopped
//   when that is a cell or more from its goal cell's point (snapPosition), else stays (goalPosition); a mover with no
//   claim claims nothing.
export namespace generalszh::gameplay
{
struct GoalClaimSystem
{
	using Query = ecs::Query<ecs::Read<gp::Transform>, ecs::Read<gp::MoveOrder>, ecs::Read<gp::NavigationAgent>, ecs::Write<gp::PathfindGoal>,
		ecs::Read<gp::MoveGoal>, ecs::Optional<gp::Route>, ecs::Optional<gp::MoveEnded>, ecs::Exclude<gp::OffMap>>;
	using Resources = ecs::Resources<ecs::Read<gp::NavigationGrid>, ecs::Write<gp::GoalCells>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const gp::NavigationGrid &grid = context.Read<gp::NavigationGrid>();
		gp::GoalCells &cells = context.Write<gp::GoalCells>();
		cells.Fit(grid.Width(), grid.Height());
		if (context.Tick() == 0)
			return;
		const std::uint64_t tick = context.Tick() - 1; // the last tick's
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto orders = chunk.template Get<gp::MoveOrder>();
			const auto agents = chunk.template Get<gp::NavigationAgent>();
			auto claims = chunk.template Get<gp::PathfindGoal>();
			const auto goals = chunk.template Get<gp::MoveGoal>();
			const auto routes = chunk.template Get<gp::Route>();
			const auto endings = chunk.template Get<gp::MoveEnded>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < orders.size(); ++row)
			{
				const gp::GoalFootprint footprint = FootprintOf(agents[row]);
				if (!routes.empty() && routes[row].planned && routes[row].plannedTick == tick && routes[row].complete)
				{
					const gp::Route &route = routes[row];
					if (orders[row].claim == gp::GoalClaim::None)
						gp::RemoveGoal(cells, entities[row], claims[row], footprint);
					else
					{
						const bool adjusted = goals[row].ordered == orders[row].destination;
						const Engine::Math::FixedVector2 end = route.count > 0 ? route.points[route.count - 1u] : (adjusted ? goals[row].goal : orders[row].destination);
						gp::UpdateGoal(cells, entities[row], claims[row], footprint, end);
					}
				}
				if (!endings.empty() && endings[row].tick == tick && claims[row].Claimed())
				{
					const auto at = transforms[row].position.XY();
					Engine::Math::FixedVector2 spot = gp::GoalCellPoint(claims[row].x, claims[row].y, footprint.centered);
					const Fixed size = Fixed::FromInt(gp::PathfindCellSize);
					if (Engine::Math::DistanceSquared(spot, at) >= size * size)
					{
						// Too far: the cell it stands in (snapPosition).
						Engine::Math::FixedVector2 corner = at;
						if (!footprint.centered)
							corner = {corner.x + size / Fixed::FromInt(2), corner.y + size / Fixed::FromInt(2)};
						const auto [x, y] = gp::WorldToCell(grid, corner);
						spot = gp::GoalCellPoint(x, y, footprint.centered);
					}
					gp::UpdateGoal(cells, entities[row], claims[row], footprint, spot);
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::GoalClaimSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.goal_claims";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	static constexpr bool Batch = true;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
