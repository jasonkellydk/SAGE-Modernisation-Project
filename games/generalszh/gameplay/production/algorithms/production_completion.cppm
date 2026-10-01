export module games.generalszh.gameplay.production.algorithms.production_completion;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.construction.resources.sales;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.production.systems.production_system;
import engine.gameplay.rts.veterancy.resources.skill_point_awards;
import games.generalszh.gameplay.ai.algorithms.ai_team_building;
import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.powers.algorithms.cash_bounty;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.production.resources.production_notices;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import games.generalszh.gameplay.score.algorithms.scoring;

// What this tick finished, after its systems: the kills scored, structures completed (their create modules, score
// keepers, EVA and computer players told), units produced brought out (score keepers, the ready voice, computer
// players told) and the airfields' parking assigned.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// Factories bring out what they finished; the AI players queue their teams' next units.
inline void CompleteProduction(GameWorld &game)
{
	// Object::scoreTheKill: the general's points for this tick's kills.
	const std::vector<gp::SkillPointAward> awards = std::move(game.world.Resource<gp::SkillPointAwards>().list);
	game.world.Resource<gp::SkillPointAwards>().list.clear();
	for (const gp::SkillPointAward &award : awards)
	{
		AddSkillPoints(game, award.player, award.points);
		DoBountyForKill(game, award.player, award.killer, award.victimDefinition, award.victimPlayer);
	}
	// Object::scoreTheKill: the score keepers hear of this tick's kills.
	ScoreKills(game);
	// Player::onStructureConstructionComplete: the computer players hear of their finished structures.
	for (const gp::ConstructionDone &done : game.world.Resource<gp::ConstructionsDone>().list)
	{
		OnBuildComplete(game, done.structure); // DozerAIUpdate: the structure's create modules
		CreateModulesBuildComplete(game, done.structure);
		ScoreStructureComplete(game, done.structure, done.rebuild); // its score keeper: built (not a rebuild)
	}
	// Player::onStructureConstructionComplete: EVA hears of a superweapon put up.
	for (const gp::ConstructionDone &done : game.world.Resource<gp::ConstructionsDone>().list)
		NoticeSuperweaponDetected(game, done.structure);
	for (const gp::ConstructionDone &done : game.world.Resource<gp::ConstructionsDone>().list)
		if (const auto *owner = game.world.IsAlive(done.structure) ? game.world.Get<gp::Owner>(done.structure) : nullptr)
			if (AiPlayer *ai = game.world.Resource<AiPlayers>().Of(owner->player))
				OnAiStructureProduced(game, *ai, done.structure);
	std::vector<gp::Produced> produced;
	game.world.Resource<gp::ProductionDone>().ForEach([&](const gp::Produced &done) { produced.push_back(done); });
	for (const gp::Produced &done : produced)
		if (done.kind == gp::ProductionKind::Unit) // research completes within the tick (ResearchCompletionSystem)
		{
			const std::vector<ecs::Entity> units = OnProduced(game, done);
			for (const ecs::Entity unit : units)
				ScoreUnitCreated(game, unit); // Player::onUnitCreated: its score keeper
			// ProductionUpdate: the first of it says it is ready (VoiceCreate).
			if (!units.empty())
				game.world.Resource<ProductionNotices>().created.push_back(units.front());
			for (const ecs::Entity unit : units)
				if (const auto *owner = game.world.Get<gp::Owner>(done.factory))
					if (AiPlayer *ai = game.world.Resource<AiPlayers>().Of(owner->player))
						OnAiUnitProduced(game, *ai, done.factory, unit); // Player::onUnitCreated
		}
	AssignAirfields(game);
}
}
