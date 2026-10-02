export module engine.gameplay.rts.combat.systems.attack_move_resume_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.attack_move;
export import engine.gameplay.rts.combat.components.attack_move_resume;
export import engine.gameplay.rts.movement.components.move_order;

// After the tick's movement, chunk-parallel and stateless: an attack-mover following a path that is not fighting keeps the
// move it is on (its current waypoint as the movement moved it on) to go back to after its next fight. Following alone,
// its path over (its move idle): AIAttackFollowWaypointPathState succeeds and it idles (attack-moving ends).
export namespace engine::gameplay
{
struct AttackMoveResumeSystem
{
	using Query = ecs::Query<ecs::Read<AttackMove>, ecs::Read<MoveOrder>, ecs::Write<AttackMoveResume>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto moves = chunk.Get<AttackMove>();
		const auto orders = chunk.Get<MoveOrder>();
		auto resumes = chunk.Get<AttackMoveResume>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < moves.size(); ++row)
		{
			if (moves[row].engaged != 0)
				continue;
			if (orders[row].mode != MoveMode::Idle)
			{
				resumes[row].order = orders[row];
				continue;
			}
			if (resumes[row].team == 0)
			{
				context.Commands().Remove<AttackMove>(entities[row]);
				context.Commands().Remove<AttackMoveResume>(entities[row]);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AttackMoveResumeSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_move_resume";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
