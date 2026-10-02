export module engine.gameplay.rts.combat.systems.attack_move_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.attack_move;
export import engine.gameplay.rts.combat.components.attack_move_resume;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
export import engine.gameplay.rts.combat.systems.targeting_system;

// AIAttackMoveToState::update, chunk-parallel, after targeting (which, for an attack-moving unit, takes on the mood target
// it comes across as it moves, and closes in on it or stops for it): while it fights, that is all; the fight over, it
// heads for its destination again (repathing). Its move over: done, unless it stopped more than ATTACK_CLOSE_ENOUGH_CELLS
// short with retries left, when it waits three seconds (still taking on what comes) and tries again. Following a path
// (AIAttackFollowWaypointPathState: AttackMoveResume), the fight over it goes back to the move along its path it was on
// (computeGoal, computePath); its path follower ends it, with no retries.
export namespace engine::gameplay
{
struct AttackMoveSystem
{
	using Query = ecs::Query<ecs::Write<AttackMove>, ecs::Write<MoveOrder>, ecs::Read<AttackTarget>, ecs::Read<Transform>, ecs::OptionalWrite<Route>,
		ecs::Optional<AttackMoveResume>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto moves = chunk.Get<AttackMove>();
		auto orders = chunk.Get<MoveOrder>();
		const auto targets = chunk.Get<AttackTarget>();
		const auto transforms = chunk.Get<Transform>();
		auto routes = chunk.Get<Route>();
		const auto resumes = chunk.Get<AttackMoveResume>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < moves.size(); ++row)
		{
			AttackMove &move = moves[row];
			MoveOrder &order = orders[row];
			// AIUpdateInterface's forceRepath: a fresh route for the same destination.
			const auto repath = [&] {
				order = MoveToPoint(move.destination);
				if (!routes.empty())
					routes[row].planned = false;
			};
			if (targets[row].target != ecs::Entity{} || targets[row].atPosition != 0)
			{
				move.engaged = 1;
				continue;
			}
			if (move.engaged != 0)
			{
				move.engaged = 0;
				if (!resumes.empty())
				{
					order = Replanned(resumes[row].order);
					if (!routes.empty())
						routes[row].planned = false;
					continue;
				}
				repath();
				continue;
			}
			if (!resumes.empty())
				continue;
			if (move.sleepUntil > tick)
				continue;
			if (move.sleepUntil != 0 && move.sleepUntil == tick)
			{
				move.sleepUntil = 0;
				repath();
				continue;
			}
			if (order.mode != MoveMode::Idle)
				continue;
			// Its move is over: close enough, or out of retries, and it is done.
			const Engine::Math::Fixed close = Engine::Math::Fixed::FromInt(AttackMove::CloseEnoughCells * PathfindCellSize);
			if (move.retries < 1 || Engine::Math::DistanceSquared(transforms[row].position.XY(), move.destination) < close * close)
			{
				context.Commands().Remove<AttackMove>(entities[row]);
				continue;
			}
			--move.retries;
			move.sleepUntil = tick + 3 * 30; // 3 * LOGICFRAMES_PER_SECOND
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AttackMoveSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.attack_move";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::MovementSystem>;
	using After = SystemTypeList<engine::gameplay::TargetingSystem>;
};
}
