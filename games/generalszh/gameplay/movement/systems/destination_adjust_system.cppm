export module games.generalszh.gameplay.movement.systems.destination_adjust_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.move_goal;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;
export import engine.gameplay.rts.navigation.components.pathfind_goal;

// Where each new move to a point really ends, before its route is planned (AIInternalMoveToState::onEnter: with
// getAdjustsDestination and not ULTRA_ACCURATE, adjustDestination, snapClosestGoalPosition when that finds nothing, then
// updateGoal): a ground mover whose order is new (its route not planned for it yet) and adjusts (GoalClaim::Adjust) gets
// its goal moved off cells others claim, and claims it at once, so the next mover's goal this tick is moved off it in
// turn; any other new order goes where it was ordered (MoveGoal). One after another in chunk and row order, as the
// original's objects update one after another: each sees the claims made before it.
export namespace generalszh::gameplay
{
struct DestinationAdjustSystem
{
	using Query = ecs::Query<ecs::Read<gp::Transform>, ecs::Read<gp::MoveOrder>, ecs::Read<gp::NavigationAgent>, ecs::Write<gp::PathfindGoal>,
		ecs::Write<gp::MoveGoal>, ecs::Optional<gp::Route>, ecs::Optional<gp::IgnoredObstacle>, ecs::Optional<gp::Owner>, ecs::Optional<gp::Locomotion>,
		ecs::Exclude<gp::OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<gp::Transform>, ecs::Read<gp::NavigationAgent>, ecs::Read<gp::Owner>, ecs::Read<gp::TeamMember>,
		ecs::Read<gp::Collider>, ecs::Read<gp::Squishable>, ecs::Read<gp::Disabled>, ecs::Read<gp::Dying>, ecs::Read<gp::Passenger>, ecs::Read<gp::OffMap>>;
	using Resources = ecs::Resources<ecs::Write<gp::NavigationGrid>, ecs::Write<gp::GoalCells>, ecs::Read<gp::Relationships>, ecs::Read<gp::TeamRoster>,
		ecs::Read<gp::GroundHeight>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		gp::NavigationGrid &grid = context.Write<gp::NavigationGrid>();
		gp::GoalCells &cells = context.Write<gp::GoalCells>();
		cells.Fit(grid.Width(), grid.Height());
		const gp::Relationships &relationships = context.Read<gp::Relationships>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const gp::LogicalExtent extent = LogicalExtentOf(context.Read<gp::GroundHeight>());
		const auto lookup = context.Lookup<Lookup>();
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto orders = chunk.template Get<gp::MoveOrder>();
			const auto agents = chunk.template Get<gp::NavigationAgent>();
			auto claims = chunk.template Get<gp::PathfindGoal>();
			auto goals = chunk.template Get<gp::MoveGoal>();
			const auto routes = chunk.template Get<gp::Route>();
			const auto ignoredRows = chunk.template Get<gp::IgnoredObstacle>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto motions = chunk.template Get<gp::Locomotion>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < orders.size(); ++row)
			{
				const gp::MoveOrder &order = orders[row];
				if (order.mode != gp::MoveMode::Point || (!routes.empty() && routes[row].planned))
					continue;
				gp::MoveGoal &goal = goals[row];
				goal = {order.destination, order.destination};
				if (order.claim != gp::GoalClaim::Adjust || (!motions.empty() && motions[row].ultraAccurate != 0))
					continue;
				const gp::GoalSeeker seeker{entities[row], ignoredRows.empty() ? ecs::Entity{} : ignoredRows[row].obstacle, FootprintOf(agents[row]),
					agents[row].surfaces, owners.empty() || HumanMover(roster, owners[row].player), transforms[row].position.XY()};
				const GoalClaimRules<decltype(lookup)> rules{lookup, relationships, entities[row]};
				const auto adjusted = gp::AdjustDestination(grid, cells, seeker, extent, order.destination, rules);
				goal.goal = adjusted ? *adjusted : gp::SnapClosestGoal(grid, cells, seeker, order.destination, rules);
				gp::UpdateGoal(cells, entities[row], claims[row], seeker.footprint, goal.goal);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::DestinationAdjustSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.destination_adjust";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// One mover after another: each adjusts around the claims made before it.
	static constexpr bool Batch = true;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
