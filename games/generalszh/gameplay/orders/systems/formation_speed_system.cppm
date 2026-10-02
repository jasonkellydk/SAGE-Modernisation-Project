export module games.generalszh.gameplay.orders.systems.formation_speed_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.orders.components.formation_move;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.resources.path_points;
export import engine.gameplay.rts.movement.components.desired_speed;

// The end of a formation move's group speed, chunk-parallel and stateless: a member whose move is done (no order, no
// path left) or whose order is no longer one of its formation move's points (another order took over: the next move
// state's onEnter asks for FAST_AS_POSSIBLE) goes as fast as it can again (its DesiredSpeed and FormationMove go, deferred).
export namespace generalszh::gameplay
{
struct FormationSpeedSystem
{
	using Query = ecs::Query<ecs::Read<FormationMove>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Optional<engine::gameplay::MovePath>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::PathPoints>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto moves = chunk.Get<FormationMove>();
		const auto orders = chunk.Get<gp::MoveOrder>();
		const auto paths = chunk.Get<gp::MovePath>();
		const gp::PathPoints &points = context.Read<gp::PathPoints>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < moves.size(); ++row)
		{
			const gp::MoveOrder &order = orders[row];
			const gp::MovePath *path = paths.empty() ? nullptr : &paths[row];
			bool ours = false;
			if (order.mode == gp::MoveMode::Point)
			{
				ours = order.destination == moves[row].goal;
				if (path != nullptr)
					for (std::uint32_t index = 0; index < path->count && !ours; ++index)
						ours = order.destination == points.At(path->block, index);
			}
			else if (order.mode == gp::MoveMode::Idle)
				ours = path != nullptr && path->next < path->count; // between legs
			if (ours)
				continue;
			commands.Remove<FormationMove>(entities[row]);
			commands.Remove<gp::DesiredSpeed>(entities[row]);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::FormationSpeedSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.formation_speed";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
