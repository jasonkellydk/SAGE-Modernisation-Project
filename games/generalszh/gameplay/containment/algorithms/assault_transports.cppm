export module games.generalszh.gameplay.containment.algorithms.assault_transports;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.containment.systems.assault_transport_system;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.resources.shots;

// The Troop Crawler's orders after the step:
// - AssaultTransportAIUpdate::beginAssault (Weapon::fireWeaponTemplate's DAMAGE_DEPLOY): a shot of a DEPLOY weapon from an
//   assault transport names its designated enemy (its members go for it from its next update);
// - the tick's AssaultOrders, in order: its own AI's (CMD_FROM_AI) exits, enters, attacks (allowed to chase: ordered),
//   the transport idling or attack-moving on; and a dead one's last orders to its members, as a player's.
export namespace generalszh::gameplay
{
inline void BeginAssaults(GameWorld &game, const engine::gameplay::FiredShots &fired)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *weapons = world.FindResource<gp::WeaponCatalog>();
	const std::uint32_t deploy = content::DamageTypeIndex("DEPLOY").value_or(0xFFFFFFFFu);
	if (weapons == nullptr)
		return;
	fired.ForEach([&](const gp::Shot &shot) {
		if (shot.weapon >= weapons->Size() || weapons->At(shot.weapon).damageType != deploy)
			return;
		if (auto *assault = world.IsAlive(shot.source) ? world.Get<AssaultTransport>(shot.source) : nullptr)
			if (world.IsAlive(shot.target))
				assault->designatedTarget = shot.target;
	});
}

inline void ApplyAssaultOrders(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Kind = AssaultOrder::Kind;
	auto &world = game.world;
	auto *resource = world.FindResource<AssaultOrders>();
	if (resource == nullptr)
		return;
	std::vector<AssaultOrder> orders;
	resource->AppendTo(orders);
	resource->Reset(0);
	for (const AssaultOrder &order : orders)
	{
		if (!world.IsAlive(order.member))
			continue;
		switch (order.kind)
		{
		case Kind::Exit:
			if (world.Has<gp::Passenger>(order.member))
			{
				AiCommanded(game, order.member);
				if (!world.Has<gp::ExitIntent>(order.member))
					world.Add<gp::ExitIntent>(order.member);
				*world.Get<gp::ExitIntent>(order.member) = {order.transport, 0u, 0u};
			}
			break;
		case Kind::Enter: OrderBoard(game, order.member, order.transport, false); break;
		case Kind::Attack:
			if (world.IsAlive(order.target))
			{
				AiCommanded(game, order.member);
				if (auto *attack = world.Get<gp::AttackTarget>(order.member))
					*attack = {order.target, true};
			}
			break;
		case Kind::TransportIdle: AiIdle(game, order.member); break;
		case Kind::TransportMove:
			OrderMove(game, order.member, order.goal, false, false);
			if (const auto *move = world.Get<gp::MoveOrder>(order.member); move != nullptr && move->mode != gp::MoveMode::Idle)
			{
				if (!world.Has<gp::AttackMove>(order.member))
					world.Add<gp::AttackMove>(order.member);
				*world.Get<gp::AttackMove>(order.member) = gp::AttackMove{order.goal};
			}
			break;
		case Kind::FinalAttack:
			if (world.IsAlive(order.target))
				OrderAttack(game, order.member, order.target);
			break;
		case Kind::FinalAttackMove: OrderAttackMove(game, order.member, order.goal); break;
		}
	}
}
}
