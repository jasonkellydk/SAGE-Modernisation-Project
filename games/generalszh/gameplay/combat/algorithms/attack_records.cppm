export module games.generalszh.gameplay.combat.algorithms.attack_records;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.identity.components.owner;

// ActiveBody::attemptDamage, after the last damage is recorded: its attacker still there, the victim's player notes it
// was attacked by the attacker's player, and when (Player::setAttackedBy: SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER,
// AIPlayer::isSupplySourceAttacked read it).
// After the step, for each of the tick's hits in order.
export namespace generalszh::gameplay
{
inline void ApplyAttackRecords(GameWorld &game)
{
	namespace gp = engine::gameplay;
	const auto *hits = game.world.FindResource<gp::Hits>();
	if (hits == nullptr)
		return;
	hits->ForEach([&](const gp::Hit &hit) {
		const auto *health = game.world.IsAlive(hit.target) ? game.world.Get<gp::Health>(hit.target) : nullptr;
		const auto *victim = game.world.IsAlive(hit.target) ? game.world.Get<gp::Owner>(hit.target) : nullptr;
		if (health == nullptr || victim == nullptr || !game.world.IsAlive(health->lastAttacker))
			return;
		const auto *attacker = game.world.Get<gp::Owner>(health->lastAttacker);
		if (attacker == nullptr || victim->player >= game.roster.PlayerCount() || attacker->player >= 64)
			return;
		game.roster.PlayerAt(victim->player).attackedBy |= std::uint64_t{1} << attacker->player;
		game.roster.PlayerAt(victim->player).attackedTick = game.tick;
	});
}
}
