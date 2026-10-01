export module games.generalszh.gameplay.movement.algorithms.unit_settles;
import std;

export import games.generalszh.gameplay.orders.algorithms.unit_orders;
export import engine.gameplay.rts.blocking.resources.move_away_requests;
export import engine.gameplay.common.spatial.components.transform;

// Idle units the tick's collisions found standing on top of another (AIUpdateInterface::processCollision, not moving:
// adjustToPossibleDestination then aiMoveToPosition, CMD_FROM_AI): each, once, is ordered by its own AI to where it
// stands; its new move's goal is moved off the cells others claim (DestinationAdjustSystem: the move's
// adjustDestination), so it steps clear. (adjustToPossibleDestination leaves a unit's own spot as it is when it may stand
// there, as a unit holding its own goal may.)
export namespace generalszh::gameplay
{
inline void ApplyUnitSettles(GameWorld &game)
{
	const auto *settles = game.world.FindResource<engine::gameplay::UnitSettles>();
	if (settles == nullptr)
		return;
	std::vector<ecs::Entity> settled;
	settles->ForEach([&](ecs::Entity unit) {
		if (std::ranges::find(settled, unit) != settled.end() || !game.world.IsAlive(unit))
			return;
		settled.push_back(unit);
		if (const auto *transform = game.world.Get<engine::gameplay::Transform>(unit))
			OrderMove(game, unit, transform->position.XY(), false, false);
	});
}
}
