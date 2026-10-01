export module games.generalszh.gameplay.construction.algorithms.construction_cancel;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.construction_progress;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.gameplay.production.algorithms.build_cost;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;

// GameLogic::onDozerCancelConstruct (MSG_DOZER_CANCEL_CONSTRUCT, the under-construction panel's Cancel): a structure of
// `player`'s still under construction is refunded its cost to build for that player (Money::deposit without counting it
// as income), unless it is a rebuild hole's reconstruction (OBJECT_STATUS_RECONSTRUCTING: it cost nothing), then killed;
// its builder leaves off with it. False: not the player's, or not under construction.
export namespace generalszh::gameplay
{
inline bool CancelConstruction(GameWorld &game, std::uint32_t player, ecs::Entity building)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(building))
		return false;
	const auto *owner = world.Get<gp::Owner>(building);
	if (owner == nullptr || owner->player != player || !world.Has<gp::UnderConstruction>(building))
		return false;
	const auto *progress = world.Get<gp::ConstructionProgress>(building);
	const auto *ref = world.Get<gp::DefinitionRef>(building);
	if ((progress == nullptr || progress->rebuild == 0) && ref != nullptr)
		world.Resource<gp::PlayerMoney>().Deposit(player, CostToBuild(game, player, game.templates.DefinitionAt(ref->index)));
	KillNow(game, building);
	return true;
}
}
