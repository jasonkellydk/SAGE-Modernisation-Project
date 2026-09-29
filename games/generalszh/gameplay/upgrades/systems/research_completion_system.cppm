export module games.generalszh.gameplay.upgrades.systems.research_completion_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.production.systems.production_system;
export import engine.gameplay.rts.upgrades.systems.upgrade_system;
export import engine.gameplay.common.identity.components.owner;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.gameplay.scripts.resources.script_records;
export import games.generalszh.gameplay.score.resources.score_keepers;

// ProductionUpdate's PRODUCTION_UPGRADE completion, within the frame it
// finishes: a PLAYER upgrade becomes the owner's (Player::addUpgrade COMPLETE:
// every object of the player looks again in this tick's upgrade pass); an
// OBJECT upgrade becomes the building's own (Object::giveUpgrade), handed to
// the upgrade pass that follows.
export namespace generalszh::gameplay
{
struct ResearchCompletionSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Upgradable>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::ProductionDone>, ecs::Read<ObjectTemplates>, ecs::Write<engine::gameplay::PlayerUpgrades>,
		ecs::Write<engine::gameplay::ObjectUpgradeGrants>, ecs::Write<ScriptRecords>, ecs::Write<ScoreKeepers>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto &upgrades = context.Read<ObjectTemplates>().Content().upgrades.upgrades;
		gp::PlayerUpgrades &players = context.Write<gp::PlayerUpgrades>();
		auto &grants = context.Write<gp::ObjectUpgradeGrants>().list;
		const auto lookup = context.Lookup<Lookup>();
		context.Read<gp::ProductionDone>().ForEach([&](const gp::Produced &done) {
			if (done.kind != gp::ProductionKind::Upgrade || done.definition >= upgrades.size())
				return;
			const gp::Owner *owner = lookup.Get<gp::Owner>(done.factory);
			if (owner == nullptr)
				return;
			// ProductionUpdate: its cost goes to the score keeper (addMoneySpent: calcCostToBuild), and the script engine
			// hears of it (notifyOfCompletedUpgrade).
			context.Write<ScoreKeepers>().Of(owner->player).moneySpent += upgrades[done.definition].cost;
			context.Write<ScriptRecords>().CompletedUpgrade(owner->player, upgrades[done.definition].name, done.factory);
			if (upgrades[done.definition].player)
				players.Grant(owner->player, done.definition);
			else if (lookup.Get<gp::Upgradable>(done.factory) != nullptr)
				grants.push_back({done.factory, done.definition});
			else
			{
				// No triggers to set off, but it has the upgrade (it may not research it again).
				gp::Upgradable own;
				own.completed.Set(done.definition);
				context.Commands().Add<gp::Upgradable>(done.factory, own);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ResearchCompletionSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.research_completion";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<engine::gameplay::UpgradeSystem>;
	using After = SystemTypeList<engine::gameplay::ProductionSystem>;
};
}
