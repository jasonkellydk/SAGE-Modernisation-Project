export module games.generalszh.gameplay.hacking.algorithms.hack_effects;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.powers.resources.cash_notices;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.disabled_until;
import engine.gameplay.rts.economy.resources.player_money;

// What a hack or sabotage does to its victim, shared by the saboteur's crate collides and the hackers' special abilities
// (SabotageSupplyCenterCrateCollide, SpecialAbilityUpdate::triggerAbilityEffect): cash taken from its player, or the
// victim DISABLED_HACKED until a tick.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// StealCashAmount, or what there is: from the building's player to the saboteur's (addMoneyEarned), floating over both.
inline std::int64_t StealCash(GameWorld &game, ecs::Entity saboteur, ecs::Entity building, std::int64_t amount)
{
	auto &money = game.world.Resource<gp::PlayerMoney>();
	const std::uint32_t victim = game.world.Get<gp::Owner>(building)->player;
	const std::uint32_t thief = game.world.Get<gp::Owner>(saboteur)->player;
	const std::int64_t cash = std::min(amount, money.Balance(victim));
	if (cash <= 0)
		return 0;
	money.Withdraw(victim, cash);
	money.Earn(thief, cash);
	if (auto *notices = game.world.FindResource<CashNotices>())
	{
		Engine::Math::FixedVector3 over = game.world.Get<gp::Transform>(saboteur)->position;
		over.z = over.z + Engine::Math::Fixed::FromInt(20);
		notices->list.push_back({CashNotice::Kind::Stolen, cash, over});
		Engine::Math::FixedVector3 under = game.world.Get<gp::Transform>(building)->position;
		under.z = under.z + Engine::Math::Fixed::FromInt(30);
		notices->list.push_back({CashNotice::Kind::Lost, cash, under});
	}
	return cash;
}

// setDisabledUntil(DISABLED_HACKED, until).
inline void DisableHacked(GameWorld &game, ecs::Entity entity, std::uint64_t until)
{
	if (auto *requests = game.world.FindResource<gp::DisableRequests>())
		requests->list.push_back({entity, gp::disabled_type::Hacked, 0, until});
}
}
