export module games.generalszh.gameplay.ai.algorithms.repulsion;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.components.repulsion;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;

// Civilians running from enemies, after the step:
// - ApplyRepulsorMarks (ActiveBody::attemptDamage): each runner hit this tick is a repulsor for the next two seconds
//   (OBJECT_STATUS_REPULSOR until its ObjectRepulsorHelper wakes).
// - ApplyRepulsions, the tick's runner events in order:
//   - Flee (AIMoveAwayFromRepulsorsState::onEnter): on its panic locomotors (PANICKING while it runs), away along a safe
//     path: to the point a cell beyond its vision range plus RepulsedDistance from the repulsor, straight away from it
//     (Pathfinder::findSafePath's goal: out of that radius; the modern route planner finds the way there).
//   - WanderInPlace (its run over, done or not): it wanders in place (AI_WANDER_IN_PLACE: on its wander locomotors).
export namespace generalszh::gameplay
{
inline void ApplyRepulsorMarks(GameWorld &game)
{
	namespace gp = engine::gameplay;
	const auto *hits = game.world.FindResource<gp::Hits>();
	const auto *rules = game.world.FindResource<RepulsionRules>();
	if (hits == nullptr || rules == nullptr || !rules->enabled)
		return;
	const std::uint64_t until = game.tick + 2 * game.step.TicksPerSecond();
	hits->ForEach([&](const gp::Hit &hit) {
		if (!game.world.IsAlive(hit.target) || !game.world.Has<Repulsable>(hit.target))
			return;
		if (!game.world.Has<RepulsorMark>(hit.target))
			game.world.Add<RepulsorMark>(hit.target);
		// Object::setStatus: only turning it on (it was off) sets the helper's two seconds going.
		RepulsorMark &mark = *game.world.Get<RepulsorMark>(hit.target);
		if (game.tick >= mark.until)
			mark.until = until;
	});
}

inline void Flee(GameWorld &game, ecs::Entity unit, ecs::Entity threat)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit) || !world.IsAlive(threat) || !world.Has<Repulsable>(unit) || !world.Has<gp::MoveOrder>(unit))
		return;
	ChooseLocomotorSet(game, unit, locomotor_set::Panic);
	const Engine::Math::FixedVector2 at = world.Get<gp::Transform>(unit)->position.XY();
	const Engine::Math::FixedVector2 from = world.Get<gp::Transform>(threat)->position.XY();
	const auto *rules = world.FindResource<RepulsionRules>();
	const Engine::Math::Fixed reach = world.Get<Repulsable>(unit)->vision + (rules != nullptr ? rules->repulsedDistance : Engine::Math::Fixed{}) +
		Engine::Math::Fixed::FromInt(gp::PathfindCellSize);
	const Engine::Math::Fixed apart = Engine::Math::Distance(at, from);
	const Engine::Math::FixedVector2 away = apart > Engine::Math::Fixed{}
		? Engine::Math::FixedVector2{(at.x - from.x) * reach / apart, (at.y - from.y) * reach / apart}
		: Engine::Math::FixedVector2{reach, Engine::Math::Fixed{}};
	// AIMoveAwayFromRepulsorsState: no adjusting, no claim.
	*world.Get<gp::MoveOrder>(unit) = gp::MoveToPoint(from + away, gp::GoalClaim::None);
	if (auto *route = world.Get<gp::Route>(unit))
		route->planned = false;
	world.Get<Repulsable>(unit)->fleeing = 1;
}

inline void ApplyRepulsions(GameWorld &game)
{
	auto *events = game.world.FindResource<RepulsionEvents>();
	if (events == nullptr)
		return;
	std::vector<RepulsionEvent> list;
	events->AppendTo(list);
	events->Reset(0);
	for (const RepulsionEvent &event : list)
	{
		if (event.kind == RepulsionEvent::Kind::Flee)
			Flee(game, event.unit, event.threat);
		else if (game.world.IsAlive(event.unit))
		{
			ChooseLocomotorSet(game, event.unit, locomotor_set::Wander);
			OrderWanderInPlace(game, event.unit, false);
		}
	}
}
}
