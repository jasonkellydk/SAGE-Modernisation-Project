export module engine.gameplay.rts.blocking.systems.blocked_repath_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.rts.blocking.components.block_contact;
export import engine.gameplay.rts.blocking.algorithms.locomotor_blocking;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.movement.components.move_away;
export import engine.gameplay.rts.death.components.dying;

// A ground unit's move taking in what the last tick's collisions found, then planning its route again when held up
// (AIInternalMoveToState::update and AIUpdateInterface::requestPath / computePath, EA's Zero Hour source), before routes
// are planned and units move:
//   stuck, or blocked over two seconds (60 ticks), its route is asked for again; a request under three ticks after its
//   last route waits a second (setQueueForPathTime), and, stuck, it ignores collisions for two seconds and is no longer
//   blocked or stuck; otherwise the route is planned this tick, and it is no longer blocked or stuck (computePath).
//   A request waiting is planned once its second is up.
// A unit making way (MoveAway, the temporary AI_MOVE_OUT_OF_THE_WAY state, run before its own AI's state) is done once
// there, after its ten seconds or dying: its own order resumes, planned anew from where it is (the state's exit destroys
// the path), and it no longer moves through units nor makes way for anyone (clearMoveOutOfWay). Stuck while making way it
// does not plan again: it moves through the units (AIMoveOutOfTheWayState::computePath).
// Planning around the units in its way (Pathfinder::patchPath) waits for the units' cells in the pathfinder; until then
// the route is planned as any other.
export namespace engine::gameplay
{
inline constexpr std::uint32_t BlockedRepathTicks = 60;  // 2 * LOGICFRAMES_PER_SECOND
inline constexpr std::uint64_t PathQueueWaitTicks = 30;  // LOGICFRAMES_PER_SECOND
inline constexpr std::uint64_t StuckIgnoreTicks = 60;    // 2 * LOGICFRAMES_PER_SECOND

struct BlockedRepathSystem
{
	using Query = ecs::Query<ecs::Write<BlockedState>, ecs::Write<BlockContact>, ecs::Read<MoveOrder>, ecs::OptionalWrite<Route>, ecs::Optional<MoveAway>,
		ecs::Optional<Dying>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		auto states = chunk.Get<BlockedState>();
		auto contacts = chunk.Get<BlockContact>();
		const auto orders = chunk.Get<MoveOrder>();
		auto routes = chunk.Get<Route>();
		for (std::size_t row = 0; row < states.size(); ++row)
			TakeBlockedContact(states[row], contacts[row]);
		const auto aways = chunk.Get<MoveAway>();
		if (!aways.empty())
		{
			const bool dying = !chunk.Get<Dying>().empty();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < states.size(); ++row)
			{
				BlockedState &state = states[row];
				if (aways[row].done != 0 || tick > aways[row].until || dying)
				{
					context.Commands().Remove<MoveAway>(entities[row]);
					if (!routes.empty())
						routes[row].planned = false;
					state.throughUnits = 0;
					state.awayFrom = {};
					state.awayFromBefore = {};
				}
				else if (state.stuck != 0)
					state.throughUnits = 1;
			}
			return;
		}
		if (routes.empty())
			return;
		for (std::size_t row = 0; row < states.size(); ++row)
		{
			if (orders[row].mode != MoveMode::Point)
				continue;
			Replan(states[row], contacts[row], routes[row], tick);
		}
	}

	static void Replan(BlockedState &state, BlockContact &contact, Route &route, std::uint64_t tick) noexcept
	{
		if (state.replanAt != 0)
		{
			if (tick < state.replanAt)
				return;
			state.replanAt = 0;
			Plan(state, route);
			return;
		}
		if (state.stuck == 0 && state.frames <= BlockedRepathTicks)
			return;
		// requestPath: too soon after its last route, it waits a second.
		if (route.planned && route.plannedTick + 3 > tick)
		{
			state.replanAt = tick + PathQueueWaitTicks;
			if (state.stuck != 0)
			{
				state.ignoreUntil = tick + StuckIgnoreTicks;
				state.frames = 0;
				state.stuck = 0;
				contact.blocked = 0;
			}
			return;
		}
		Plan(state, route);
	}

	// computePath: a new route, no longer blocked nor stuck.
	static void Plan(BlockedState &state, Route &route) noexcept
	{
		route.planned = false;
		state.frames = 0;
		state.stuck = 0;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::BlockedRepathSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.blocked_repath";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before routes are planned.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
