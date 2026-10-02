export module engine.gameplay.rts.movement.systems.move_path_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.movement.resources.path_points;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.navigation.definitions.pathfind_cell;

// Following a path of positions (AIFollowPathState: the next point once the last is reached), each tick before movement,
// chunk-parallel: a mover whose move is done heads for its path's next point, passing over those within a pathfinding
// cell of where it stands (AIFollowPathState::update's tooClose); a leg on the way claims no goal, the last one's goal is
// adjusted; past the last, the path is done. A new order in between (a move, or anything else) ends it. The points are
// PathPoints' (read only here).
export namespace engine::gameplay
{
struct MovePathSystem
{
	using Query = ecs::Query<ecs::Write<MovePath>, ecs::Write<MoveOrder>, ecs::OptionalWrite<Route>, ecs::Optional<Transform>>;
	using Resources = ecs::Resources<ecs::Read<PathPoints>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const PathPoints &points = context.Read<PathPoints>();
		auto paths = chunk.Get<MovePath>();
		auto orders = chunk.Get<MoveOrder>();
		auto routes = chunk.Get<Route>();
		const auto transforms = chunk.Get<Transform>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		const Engine::Math::Fixed cell = Engine::Math::Fixed::FromInt(PathfindCellSize);
		for (std::size_t row = 0; row < paths.size(); ++row)
		{
			MovePath &path = paths[row];
			// Taken over: its current move is no longer the path's point it last set.
			if (path.next > 0 && orders[row].mode != MoveMode::Idle && !(orders[row].destination == points.At(path.block, path.next - 1)))
			{
				commands.Remove<MovePath>(entities[row]);
				continue;
			}
			if (orders[row].mode != MoveMode::Idle)
				continue;
			// tooClose: points within a cell of where it stands are passed over.
			if (!transforms.empty())
				while (path.next < path.count &&
					Engine::Math::DistanceSquared(points.At(path.block, path.next), transforms[row].position.XY()) < cell * cell)
					++path.next;
			if (path.next >= path.count)
			{
				commands.Remove<MovePath>(entities[row]);
				continue;
			}
			orders[row] = MoveToPoint(points.At(path.block, path.next++));
			// AIFollowPathState: a leg with more after it keeps no goal (setAdjustsDestination(false)); the last is adjusted
			// and claimed.
			if (path.next < path.count)
				orders[row].claim = GoalClaim::None;
			// A new move: its route is planned afresh (AIFollowPathState::update: computePath for the next point).
			if (!routes.empty())
				routes[row].planned = false;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MovePathSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.move_path";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before movement.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
