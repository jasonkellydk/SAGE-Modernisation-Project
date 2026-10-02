export module games.generalszh.gameplay.movement.algorithms.unit_settles;
import std;

export import games.generalszh.gameplay.orders.algorithms.unit_orders;
export import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
export import engine.gameplay.rts.blocking.resources.move_away_requests;
export import engine.gameplay.common.spatial.components.transform;

// Idle units the tick's collisions found standing on top of another (AIUpdateInterface::processCollision, not moving:
// adjustToPossibleDestination then aiMoveToPosition, CMD_FROM_AI): each, once, is ordered by its own AI to the spot
// nearest it that it may stand on (Pathfinder::adjustDestination's spiral off the cells others claim). Where that is the
// spot it stands on, no order is given: the original's move there ends on its first update, and given every tick it
// would only keep the unit from ever being idle (modernised on purpose).
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
		const auto *transform = game.world.Get<engine::gameplay::Transform>(unit);
		if (transform == nullptr)
			return;
		// adjustToPossibleDestination from where it stands; a spot no different from its own is a move that ends where
		// it began (left out: issuing it every tick would only keep the unit from being idle).
		const auto adjusted = AdjustDestinationFor(game.world, unit, transform->position.XY());
		if (!adjusted || Engine::Math::DistanceSquared(*adjusted, transform->position.XY()) < Engine::Math::Fixed::One())
			return;
		OrderMove(game, unit, *adjusted, false, false);
	});
}
}
