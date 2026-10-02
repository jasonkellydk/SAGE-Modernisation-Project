export module games.generalszh.gameplay.production.systems.production_allowance_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.production.components.production_queue;
export import engine.gameplay.rts.production.systems.production_system;
export import engine.gameplay.rts.economy.resources.player_money;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.status.components.disabled;
export import games.generalszh.gameplay.objects.resources.object_templates;

// ProductionUpdate::update, before it counts the front entry's frame: a unit its player is no longer allowed to build
// (Player::allowedToBuild: a structure while its base may not be built, anything else while its units may not:
// PLAYER_DISABLE_BASE_CONSTRUCTION / PLAYER_DISABLE_UNIT_CONSTRUCTION) is cancelled (cancelUnitCreate: its cost given
// back, heard) unless it is a dozer, and that update does nothing more (the queue's spentTick: the next entry starts
// on the next update). Only where the update runs: not while the
// factory is disabled but by the types it processes (DisabledTypesToProcess), not while it is being sold. Once a tick,
// before the production system; a batch, in entity order.
export namespace generalszh::gameplay
{
struct ProductionAllowanceSystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::ProductionQueue>, ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::Disabled>,
		ecs::Exclude<engine::gameplay::Sale>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<ObjectTemplates>, ecs::Write<engine::gameplay::PlayerMoney>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		gp::PlayerMoney &money = context.Write<gp::PlayerMoney>();
		query.ForEachChunk([&](auto chunk) {
			auto queues = chunk.template Get<gp::ProductionQueue>();
			const auto owners = chunk.template Get<gp::Owner>();
			const auto disabledRows = chunk.template Get<gp::Disabled>();
			for (std::size_t row = 0; row < queues.size(); ++row)
			{
				gp::ProductionQueue &queue = queues[row];
				const std::uint32_t player = owners[row].player;
				if (queue.count == 0 || queue.entries[0].kind != gp::ProductionKind::Unit || player >= roster.PlayerCount())
					continue;
				const gp::Player &record = roster.PlayerAt(player);
				if (record.unitConstructionEnabled && record.canBuildBase)
					continue;
				if (!disabledRows.empty() && !gp::RunsWhileDisabled(disabledRows[row], queue.runsWhileDisabled))
					continue;
				const gp::ProductionEntry &front = queue.entries[0];
				if (front.definition >= templates.DefinitionCount())
					continue;
				const auto &unit = templates.DefinitionAt(front.definition);
				const bool allowed = unit.Is("STRUCTURE") ? record.canBuildBase : record.unitConstructionEnabled;
				if (allowed || unit.Is("DOZER"))
					continue;
				money.Deposit(player, front.paid);
				queue.RemoveAt(0);
				queue.spentTick = context.Tick();
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ProductionAllowanceSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.production_allowance";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
