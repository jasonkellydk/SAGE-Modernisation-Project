export module engine.gameplay.rts.docking.systems.repair_dock_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.docking.components.docking;
export import engine.gameplay.rts.docking.components.repair_dock;
export import engine.gameplay.rts.docking.resources.dock_repairs;
export import engine.gameplay.common.health.components.health;

// The business of repair docks (AIDockProcessDockState::update -> RepairDockUpdate::action), once per action delay
// while a docker is at it: as the docker's repair begins, its rate is what it lacks over the dock's TimeForFullHeal;
// whole, its business is done (the docker leaves); else it heals by the rate (attemptHealing: up to its maximum). A
// batch: a dock serves one docker at a time, and its books change through the command buffer.
//   Retail quirk fixed: the original works the rate out only when its last docker is unset (m_lastRepair == 0),
//   which it is again only once a docker is made whole; a docker sent off part way left the next one healing at
//   its rate. Here the rate is worked out whenever the docker is not the one it was worked out for.
export namespace engine::gameplay
{
struct RepairDockSystem
{
	using Query = ecs::Query<ecs::Write<Docking>, ecs::Write<Health>>;
	using Lookup = ecs::Lookup<ecs::Read<RepairDock>>;
	using Resources = ecs::Resources<ecs::Write<DockRepairs>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto &repairs = context.Write<DockRepairs>().list;
		repairs.clear();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto dockings = chunk.template Get<Docking>();
			auto healths = chunk.template Get<Health>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < dockings.size(); ++row)
			{
				Docking &docking = dockings[row];
				if (!IsDocking(docking) || docking.phase != DockPhase::Process || docking.entering || docking.finished || tick < docking.nextAction)
					continue;
				const RepairDock *dock = lookup.Get<RepairDock>(docking.dock);
				if (dock == nullptr)
					continue;
				docking.nextAction = tick + docking.actionDelay;
				RepairDock books = *dock;
				Health &health = healths[row];
				if (books.lastRepair != entities[row])
				{
					books.lastRepair = entities[row];
					books.healthPerTick = (health.maximum - health.current) / books.fullHealTicks;
				}
				if (health.current >= health.maximum)
				{
					books.lastRepair = {};
					docking.finished = true;
				}
				else
				{
					Heal(health, books.healthPerTick, context.Tick());
					repairs.push_back({entities[row], docking.dock});
				}
				commands.Set<RepairDock>(docking.dock, books);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::RepairDockSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.repair_dock";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
