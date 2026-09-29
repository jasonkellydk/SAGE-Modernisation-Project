export module engine.gameplay.rts.movement.systems.move_path_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.components.move_order;

// Following a path of positions (FollowPath: the next point once the last is reached), each tick before movement,
// chunk-parallel: a mover whose move is done heads for its path's next point; past the last, the path is done. A new
// order in between (a move, or anything else) ends it.
export namespace engine::gameplay
{
struct MovePathSystem
{
	using Query = ecs::Query<ecs::Write<MovePath>, ecs::Write<MoveOrder>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto paths = chunk.Get<MovePath>();
		auto orders = chunk.Get<MoveOrder>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < paths.size(); ++row)
		{
			MovePath &path = paths[row];
			// Taken over: its current move is no longer the path's point it last set.
			if (path.next > 0 && orders[row].mode != MoveMode::Idle &&
				(orders[row].destination.x != path.points[path.next - 1].x || orders[row].destination.y != path.points[path.next - 1].y))
			{
				commands.Remove<MovePath>(entities[row]);
				continue;
			}
			if (orders[row].mode != MoveMode::Idle)
				continue;
			if (path.next >= path.count)
			{
				commands.Remove<MovePath>(entities[row]);
				continue;
			}
			orders[row] = MoveToPoint(path.points[path.next++]);
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
