export module games.generalszh.gameplay.ai.algorithms.slave_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import engine.gameplay.rts.slaves.resources.slave_orders;
import engine.gameplay.rts.movement.components.locomotion;

// SlavedUpdate's orders to its own locomotor, after the tick, in order (endRepair, doRepairLogic,
// moveToNewRepairSpot): its locomotor set first (chooseLocomotorSet: a fresh locomotor), then its precise height and
// ultra-accuracy on whichever locomotor it moves on (a set its template lacks leaves the one it has).
export namespace generalszh::gameplay
{
inline void ApplySlaveOrders(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto *orders = game.world.FindResource<gp::SlaveOrders>();
	if (orders == nullptr)
		return;
	for (const gp::SlaveLocomotorOrder &order : orders->locomotors)
	{
		if (!game.world.IsAlive(order.slave))
			continue;
		if (order.set != gp::SlaveLocomotorSet::Keep)
			ChooseLocomotorSet(game, order.slave, order.set == gp::SlaveLocomotorSet::Panic ? locomotor_set::Panic : locomotor_set::Normal);
		gp::Locomotion *motion = game.world.Get<gp::Locomotion>(order.slave);
		if (motion == nullptr)
			continue;
		motion->preciseZ = order.preciseZ;
		motion->preciseHeight = order.preciseHeight;
		if (order.ultraAccurate != gp::SlaveLocomotorOrder::Keep)
			motion->ultraAccurate = order.ultraAccurate;
	}
	orders->locomotors.clear();
}
}
