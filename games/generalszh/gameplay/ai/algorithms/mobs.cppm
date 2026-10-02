export module games.generalszh.gameplay.ai.algorithms.mobs;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.ai.components.mob_member;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import engine.gameplay.rts.lifecycle.resources.kill_requests;

// MobMemberSlavedUpdate's orders to its members, after the tick, in order: their AI's own (CMD_FROM_AI) moves, attacks,
// idles and locomotor sets; the members to die (kill) go with the next tick's kills.
export namespace generalszh::gameplay
{
inline void ApplyMobOrders(GameWorld &game)
{
	auto *events = game.world.FindResource<MobEvents>();
	if (events == nullptr)
		return;
	const std::vector<MobEvent> list = std::move(events->list);
	events->list.clear();
	for (const MobEvent &event : list)
	{
		if (!game.world.IsAlive(event.member))
			continue;
		switch (event.order)
		{
		case MobOrder::Move:
			OrderMove(game, event.member, event.at, false, false);
			break;
		case MobOrder::Attack:
			// MobMemberSlavedUpdate: aiAttackObject(target, 999, CMD_FROM_AI).
			OrderAttack(game, event.member, event.target, 999, engine::gameplay::CommandSource::Ai);
			AiCommanded(game, event.member);
			break;
		case MobOrder::Idle:
			AiIdle(game, event.member);
			break;
		case MobOrder::Kill:
			game.world.Resource<engine::gameplay::KillRequests>().entities.push_back(event.member);
			break;
		case MobOrder::UseSet:
			ChooseLocomotorSet(game, event.member, event.locomotorSet);
			break;
		}
	}
}
}
