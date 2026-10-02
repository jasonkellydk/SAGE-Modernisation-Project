export module games.generalszh.gameplay.construction.systems.builder_boredom_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.construction.components.builder_boredom;
export import engine.gameplay.rts.construction.components.builder;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.rts.harvesting.components.harvester;

// DozerPrimaryIdleState::onEnter / update for every dozer and worker, chunk-parallel. At work on a task (its dozer
// machine out of its idle state), its idle stretch starts over; a worker gathering supplies (its supply brain active:
// not Busy, or told to gather) runs no dozer machine at all and its stretch stands still. Else its AI doing anything
// (moving, attacking, busy) starts the stretch over; idle past BoredTime, it is bored: the stretch starts over and it
// goes out as a BoredBuilder. DozerPrimaryIdleState's idle worker mark: idle it goes on its player's idle worker list
// (once); doing anything or out of the idle state (a task) it comes off. (Dying takes it off the list too: Object's
// removeIdleWorker as it dies; the list skips the dying.)
export namespace generalszh::gameplay
{
struct BuilderBoredomSystem
{
	using Query = ecs::Query<ecs::Write<BuilderBoredom>, ecs::Optional<engine::gameplay::MoveOrder>, ecs::Optional<engine::gameplay::AttackTarget>,
		ecs::Optional<engine::gameplay::AiActivity>, ecs::Optional<engine::gameplay::Builder>, ecs::Optional<engine::gameplay::Harvester>>;
	using Resources = ecs::Resources<ecs::Write<BoredBuilders>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<BoredBuilders>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		auto boredoms = chunk.Get<BuilderBoredom>();
		const auto orders = chunk.Get<gp::MoveOrder>();
		const auto attacks = chunk.Get<gp::AttackTarget>();
		const auto activities = chunk.Get<gp::AiActivity>();
		const auto builders = chunk.Get<gp::Builder>();
		const auto harvesters = chunk.Get<gp::Harvester>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		auto &bored = context.Write<BoredBuilders>().Slot(context);
		for (std::size_t row = 0; row < boredoms.size(); ++row)
		{
			BuilderBoredom &boredom = boredoms[row];
			if (!builders.empty())
			{
				boredom.since = tick;
				boredom.idleMarked = 0; // DozerPrimaryIdleState::onExit
				continue;
			}
			if (!harvesters.empty() && (harvesters[row].state != gp::HarvesterState::Busy || harvesters[row].forceWanting))
				continue;
			const bool idle = (orders.empty() || orders[row].mode == gp::MoveMode::Idle) && (attacks.empty() || !gp::Attacking(attacks[row])) &&
				(activities.empty() || activities[row].busy == 0);
			if (idle && boredom.idleMarked == 0)
				boredom.idleMarked = tick + 1;
			else if (boredom.idleMarked != 0 && !idle)
				boredom.idleMarked = 0;
			if (!idle)
			{
				boredom.since = tick;
				continue;
			}
			if (tick - boredom.since > boredom.boredTicks)
			{
				boredom.since = tick;
				bored.push_back(entities[row]);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::BuilderBoredomSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.builder_boredom";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
