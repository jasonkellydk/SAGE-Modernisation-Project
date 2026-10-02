export module games.generalszh.gameplay.movement.systems.enter_obstacle_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import games.generalszh.gameplay.crates.components.pilot_seeker;

// A pilot going for a vehicle to climb into ignores that vehicle as an obstacle (AIEnterState::onEnter:
// ignoreObstacle(goal object)): neither its collisions nor its routes count it in the way. (The engine's own entering
// and docking are taken in by the blocking domain; this is the game's pilot.)
export namespace generalszh::gameplay
{
struct EnterObstacleSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::BlockedState>, ecs::Read<PilotSeeker>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		auto states = chunk.Get<engine::gameplay::BlockedState>();
		const auto seekers = chunk.Get<PilotSeeker>();
		for (std::size_t row = 0; row < states.size(); ++row)
			if (seekers[row].goal != ecs::Entity{})
				states[row].ignoring = seekers[row].goal;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::EnterObstacleSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.enter_obstacle";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
