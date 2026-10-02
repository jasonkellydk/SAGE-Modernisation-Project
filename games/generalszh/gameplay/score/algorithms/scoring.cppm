export module games.generalszh.gameplay.score.algorithms.scoring;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.score.resources.score_keepers;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.economy.resources.player_money;

// ScoreKeeper's bookkeeping and where the original calls it:
// - ScoreObjectBuilt / ScoreObjectUnbuilt (addObjectBuilt / removeObjectBuilt, scoring on): a STRUCTURE that is SCORE or
//   SCORE_CREATE counts as a building built, an INFANTRY or VEHICLE that is SCORE or SCORE_CREATE as a unit; each
//   counted by kind.
// - ScoreUnitCreated (Player::onUnitCreated: produced, spawned, a start's units): built.
// - ScoreStructureComplete (Player::onStructureConstructionComplete: not a rebuild): built, and its cost spent
//   (calcCostToBuild).
// - ScoreCapture (Object::setTeam's capture, addObjectCaptured, scoring on): a STRUCTURE captured by the new owner, a
//   faction building if SCORE, else a tech building.
// - ScoreKills (Object::scoreTheKill for this tick's kills with a killer): a victim of a playable side, not IGNORED_IN_GUI,
//   is lost to its player (addObjectLost); if its killer's team is its enemy and of another player, it is destroyed by the
//   killer's player (addObjectDestroyed). Either counts, scoring on, a finished STRUCTURE that is SCORE or SCORE_DESTROY
//   as a building, a finished INFANTRY or VEHICLE that is SCORE or SCORE_DESTROY as a unit.
// - ScoreMoneySpent (addMoneySpent: upgrades researched), whether scoring is on or not.
// - Score (calculateScore): 100 a unit or building built, the money earned, 100 a unit or building destroyed of every
//   other player.
export namespace generalszh::gameplay
{
namespace score_detail
{
inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline std::uint32_t DefinitionIndex(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(entity);
	return ref != nullptr ? ref->index : 0xFFFFFFFFu;
}

enum class Counted : std::uint8_t
{
	None,
	Building,
	Unit,
};

// `alternative`: SCORE_CREATE for building, SCORE_DESTROY for losing.
inline Counted Classify(const content::ObjectDefinition &kind, std::string_view alternative)
{
	if (kind.Is("STRUCTURE") && (kind.Is("SCORE") || kind.Is(alternative)))
		return Counted::Building;
	if ((kind.Is("INFANTRY") || kind.Is("VEHICLE")) && (kind.Is("SCORE") || kind.Is(alternative)))
		return Counted::Unit;
	return Counted::None;
}

inline void Built(GameWorld &game, std::uint32_t player, ecs::Entity entity, std::int32_t step)
{
	auto &keepers = game.world.Resource<ScoreKeepers>();
	const content::ObjectDefinition *kind = DefinitionOf(game, entity);
	if (!keepers.enabled || kind == nullptr)
		return;
	ScoreKeeper &keeper = keepers.Of(player);
	const Counted counted = Classify(*kind, "SCORE_CREATE");
	if (counted == Counted::None)
		return;
	(counted == Counted::Building ? keeper.buildingsBuilt : keeper.unitsBuilt) += step;
	keeper.objectsBuilt[DefinitionIndex(game, entity)] += step;
}
}

inline void ScoreObjectBuilt(GameWorld &game, std::uint32_t player, ecs::Entity entity) { score_detail::Built(game, player, entity, 1); }
inline void ScoreObjectUnbuilt(GameWorld &game, std::uint32_t player, ecs::Entity entity) { score_detail::Built(game, player, entity, -1); }

inline void ScoreUnitCreated(GameWorld &game, ecs::Entity unit)
{
	if (game.world.IsAlive(unit))
		ScoreObjectBuilt(game, OwnerPlayer(game, unit), unit);
}

inline void ScoreMoneySpent(GameWorld &game, std::uint32_t player, std::int64_t amount)
{
	game.world.Resource<ScoreKeepers>().Of(player).moneySpent += amount;
}

inline void ScoreStructureComplete(GameWorld &game, ecs::Entity structure, bool rebuild)
{
	const content::ObjectDefinition *kind = score_detail::DefinitionOf(game, structure);
	if (rebuild || kind == nullptr)
		return;
	const std::uint32_t player = OwnerPlayer(game, structure);
	ScoreObjectBuilt(game, player, structure);
	ScoreMoneySpent(game, player, CostToBuild(game, player, *kind));
}

inline void ScoreCapture(GameWorld &game, std::uint32_t newOwner, ecs::Entity entity)
{
	auto &keepers = game.world.Resource<ScoreKeepers>();
	const content::ObjectDefinition *kind = score_detail::DefinitionOf(game, entity);
	if (!keepers.enabled || kind == nullptr || !kind->Is("STRUCTURE"))
		return;
	ScoreKeeper &keeper = keepers.Of(newOwner);
	(kind->Is("SCORE") ? keeper.factionBuildingsCaptured : keeper.techBuildingsCaptured) += 1;
	keeper.objectsCaptured[score_detail::DefinitionIndex(game, entity)] += 1;
}

inline void ScoreKills(GameWorld &game)
{
	namespace gp = engine::gameplay;
	const auto *deaths = game.world.FindResource<gp::Deaths>();
	if (deaths == nullptr)
		return;
	auto &keepers = game.world.Resource<ScoreKeepers>();
	deaths->ForEach([&](const gp::Death &death) {
		if (death.killer == ecs::Entity{} || !game.world.IsAlive(death.killer) || !game.world.IsAlive(death.entity))
			return;
		const content::ObjectDefinition *kind = score_detail::DefinitionOf(game, death.entity);
		const std::uint32_t victimPlayer = OwnerPlayer(game, death.entity);
		if (kind == nullptr || victimPlayer >= game.roster.PlayerCount() || !game.roster.PlayerAt(victimPlayer).playable || kind->Is("IGNORED_IN_GUI"))
			return;
		const score_detail::Counted counted = score_detail::Classify(*kind, "SCORE_DESTROY");
		const bool finished = !game.world.Has<gp::UnderConstruction>(death.entity);
		const std::uint32_t definition = score_detail::DefinitionIndex(game, death.entity);
		if (keepers.enabled && counted != score_detail::Counted::None && finished)
		{
			ScoreKeeper &lost = keepers.Of(victimPlayer);
			(counted == score_detail::Counted::Building ? lost.buildingsLost : lost.unitsLost) += 1;
			lost.objectsLost[definition] += 1;
		}
		const std::uint32_t killerPlayer = OwnerPlayer(game, death.killer);
		if (RelationOf(game, death.killer, death.entity) != gp::Relationship::Enemies || killerPlayer == victimPlayer)
			return;
		if (keepers.enabled && counted != score_detail::Counted::None && finished && victimPlayer < ScorePlayers)
		{
			ScoreKeeper &killer = keepers.Of(killerPlayer);
			(counted == score_detail::Counted::Building ? killer.buildingsDestroyed : killer.unitsDestroyed)[victimPlayer] += 1;
			killer.objectsDestroyed[victimPlayer][definition] += 1;
		}
	});
}

inline std::int64_t Score(const GameWorld &game, std::uint32_t player)
{
	const auto *keepers = game.world.FindResource<ScoreKeepers>();
	const ScoreKeeper *keeper = keepers != nullptr ? keepers->Find(player) : nullptr;
	std::int64_t score = 0;
	if (keeper != nullptr)
	{
		score += static_cast<std::int64_t>(keeper->unitsBuilt) * 100 + static_cast<std::int64_t>(keeper->buildingsBuilt) * 100;
		for (std::size_t other = 0; other < ScorePlayers; ++other)
			if (other != player)
				score += static_cast<std::int64_t>(keeper->unitsDestroyed[other]) * 100 + static_cast<std::int64_t>(keeper->buildingsDestroyed[other]) * 100;
	}
	if (const auto *money = game.world.FindResource<engine::gameplay::PlayerMoney>())
		score += money->Earned(player);
	return score;
}
}
