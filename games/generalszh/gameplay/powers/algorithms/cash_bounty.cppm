export module games.generalszh.gameplay.powers.algorithms.cash_bounty;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.powers.resources.cash_notices;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_bounties;

// Player::doBountyForKill (Object::scoreTheKill, with the general's points for the kill: an enemy's, of a playable
// side, not being built): the killer's player is paid its cash bounty's share of what the victim cost to build
// (calcCostToBuild: its BuildCost, the port having no cost modifiers), rounded up; the cash floats over the killer.
export namespace generalszh::gameplay
{
inline std::int64_t DoBountyForKill(GameWorld &game, std::uint32_t player, ecs::Entity killer, std::uint32_t victimDefinition, std::uint32_t victimPlayer)
{
	const auto *bounties = game.world.FindResource<engine::gameplay::PlayerBounties>();
	if (bounties == nullptr || victimDefinition == 0xFFFFFFFFu)
		return 0;
	const std::int64_t bounty = bounties->BountyFor(player, CostToBuild(game, victimPlayer, game.templates.DefinitionAt(victimDefinition)));
	if (bounty == 0)
		return 0;
	game.world.Resource<engine::gameplay::PlayerMoney>().Earn(player, bounty); // addMoneyEarned
	const auto *at = game.world.IsAlive(killer) ? game.world.Get<engine::gameplay::Transform>(killer) : nullptr;
	if (auto *notices = game.world.FindResource<CashNotices>(); notices != nullptr && at != nullptr)
	{
		Engine::Math::FixedVector3 over = at->position;
		over.z = over.z + Engine::Math::Fixed::FromInt(10);
		notices->list.push_back({CashNotice::Kind::Bounty, bounty, over});
	}
	return bounty;
}
}
