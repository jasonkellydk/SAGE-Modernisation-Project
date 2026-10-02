export module games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.flight_deck.components.flight_deck;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.components.guard;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.combat.algorithms.common_targets;
import games.generalszh.gameplay.ai.algorithms.ai_base_building;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.areas.resources.trigger_areas;

// Guarding (AIUpdateInterface::aiGuardPosition / aiGuardObject -> AI_GUARD: AIGuardState with its AIGuardMachine),
// outside its system (GuardSystem steps the guards):
// - GuardPosition / GuardObject: a mobile unit that is no projectile drops what its AI was doing (the state machine
//   cleared) and guards; a player's position off the playable area is brought to its nearest edge. Its machine starts
//   in its inner state, which with no nemesis goes on to return (AIGuardState::onEnter: AI_GUARD_RETURN). In its guard
//   state its AI is not idle (no mood look of its own). Any later order ends it (EndStance).
// - GuardRetaliate (aiGuardRetaliate from ActiveBody's call for help, CMD_FROM_AI): its AI drops what it was doing and
//   strikes back at the aggressor from where it stands (AIGuardRetaliateMachine, starting in its attack-aggressor state).
// - ApplyGuards: the tick's guards' team victims cleared (setTeamTargetObject(none)); a retaliation over, its AI idles.
export namespace generalszh::gameplay
{
namespace guard_detail
{
namespace gp = engine::gameplay;

inline bool MayGuard(GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit) || !game.world.Has<gp::MoveOrder>(unit) || !detail::Mobile(game, unit))
		return false;
	const auto *ref = game.world.Get<gp::DefinitionRef>(unit);
	return ref == nullptr || !game.templates.DefinitionAt(ref->index).Is("PROJECTILE");
}

inline void Start(GameWorld &game, ecs::Entity unit, Guard guard, bool fromPlayer)
{
	(void)fromPlayer; // a player's or a script's order: its last command source is no longer its AI
	AiIdle(game, unit);
	Commanded(game, unit);
	guard.state = GuardState::Inner;
	guard.entered = 0;
	if (!game.world.Has<Guard>(unit))
		game.world.Add<Guard>(unit);
	*game.world.Get<Guard>(unit) = guard;
	if (auto *activity = game.world.Get<gp::AiActivity>(unit))
		activity->busy = 1;
}
}

void GuardPosition(GameWorld &game, ecs::Entity unit, Engine::Math::FixedVector2 position, GuardMode mode = GuardMode::Normal, bool fromPlayer = false)
{
	// FlightDeckBehavior::aiDoCommand: a carrier's guard goes to its jets.
	if (DesignateFlightDeck(game.world, unit, DeckOrder::Guard, {}, position, false))
		return;
	if (!guard_detail::MayGuard(game, unit))
		return;
	if (fromPlayer)
	{
		// Clipped to the playable area (findClosestEdgePoint).
		const auto [low, high] = game.ground.Extent();
		if (position.x < low.x || position.y < low.y || position.x > high.x || position.y > high.y)
			position = game.ground.ClosestEdgePoint(position).XY();
	}
	Guard guard;
	guard.position = position;
	guard.mode = mode;
	guard_detail::Start(game, unit, guard, fromPlayer);
}

void GuardObject(GameWorld &game, ecs::Entity unit, ecs::Entity target, GuardMode mode = GuardMode::Normal, bool fromPlayer = false)
{
	if (!game.world.IsAlive(target) || !guard_detail::MayGuard(game, unit))
		return;
	Guard guard;
	guard.target = target;
	guard.position = game.world.Get<engine::gameplay::Transform>(target)->position.XY();
	guard.mode = mode;
	guard_detail::Start(game, unit, guard, fromPlayer);
}

// aiGuardArea: guards the trigger area.
void GuardArea(GameWorld &game, ecs::Entity unit, std::uint32_t area, GuardMode mode = GuardMode::Normal, bool fromPlayer = false)
{
	const auto *areas = game.world.FindResource<engine::gameplay::TriggerAreas>();
	if (areas == nullptr || area >= areas->areas.size() || !guard_detail::MayGuard(game, unit))
		return;
	Guard guard;
	guard.area = area;
	guard.mode = mode;
	guard_detail::Start(game, unit, guard, fromPlayer);
}

void GuardRetaliate(GameWorld &game, ecs::Entity unit, ecs::Entity aggressor, Engine::Math::FixedVector2 position)
{
	if (!game.world.IsAlive(aggressor) || !guard_detail::MayGuard(game, unit))
		return;
	AiIdle(game, unit);
	AiCommanded(game, unit);
	Guard guard;
	guard.position = position;
	guard.nemesis = aggressor;
	guard.mode = GuardMode::Retaliate;
	guard.state = GuardState::Aggressor;
	if (!game.world.Has<Guard>(unit))
		game.world.Add<Guard>(unit);
	*game.world.Get<Guard>(unit) = guard;
	if (auto *activity = game.world.Get<engine::gameplay::AiActivity>(unit))
		activity->busy = 1;
}

// doTeamGuardPosition / doTeamGuard (TEAM_GUARD_POSITION, TEAM_GUARD): each member with an AI guards the waypoint
// (groupGuardPosition), or where it stands without one.
void TeamGuard(GameWorld &game, const std::string &team, const std::string &waypoint)
{
	const std::uint32_t at = waypoint.empty() ? engine::gameplay::WaypointGraph::None : game.waypoints.Find(waypoint);
	if (!waypoint.empty() && at == engine::gameplay::WaypointGraph::None)
		return;
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (!HasAi(game, entity))
			return;
		const auto *where = game.world.Get<engine::gameplay::Transform>(entity);
		GuardPosition(game, entity, at != engine::gameplay::WaypointGraph::None ? game.waypoints.Position(at).XY() : where->position.XY());
	});
}

// doTeamGuardObject (TEAM_GUARD_OBJECT): the team as an AIGroup guards the named unit (groupGuardObject, GUARDMODE_NORMAL).
void TeamGuardObject(GameWorld &game, const std::string &team, ecs::Entity target)
{
	if (!game.world.IsAlive(target) || !ResolveTeam(game, team))
		return;
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (HasAi(game, entity))
			GuardObject(game, entity, target);
	});
}

// doGuardSupplyCenter (TEAM_GUARD_SUPPLY_CENTER) -> AIPlayer::guardSupplyCenter, for a team of a computer player: the
// source of supplies under attack (isSupplySourceAttacked, looked at now), else its supply centre (findSupplyCenter with
// at least `minimumSupplies`); the team guards a spot on its near side toward its skirmish enemy's structures (0.8 of
// its bounding radius from its centre, groupGuardPosition).
void GuardSupplyCenter(GameWorld &game, const std::string &team, std::int64_t minimumSupplies)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto index = ResolveTeam(game, team);
	auto *ais = game.world.FindResource<AiPlayers>();
	AiPlayer *ai = index && ais != nullptr ? ais->Of(game.roster.TeamAt(*index).owner) : nullptr;
	if (ai == nullptr)
		return;
	ai->supplyAttackCheckTick = 0; // force the look
	ecs::Entity warehouse;
	if (SupplySourceAttacked(game, *ai))
		warehouse = ai->attackedSupplyCenter;
	if (!game.world.IsAlive(warehouse))
		warehouse = FindSupplyCenter(game, *ais, *ai, minimumSupplies);
	const auto *at = game.world.IsAlive(warehouse) ? game.world.Get<gp::Transform>(warehouse) : nullptr;
	const auto *ref = at != nullptr ? game.world.Get<gp::DefinitionRef>(warehouse) : nullptr;
	if (ref == nullptr)
		return;
	Engine::Math::FixedVector2 location = at->position.XY();
	const auto enemy = SkirmishEnemy(game, *ais, ai->player);
	const auto bounds = enemy ? PlayerStructureBounds(game, *enemy) : std::array<Engine::Math::FixedVector2, 2>{};
	const Engine::Math::FixedVector2 away = location - (bounds[0] + bounds[1]) / Fixed::FromInt(2);
	const Fixed length = Engine::Math::Length(away);
	const Engine::Math::FixedVector2 offset = length > Fixed{} ? away / length : Engine::Math::FixedVector2{};
	const Fixed radius = content::BoundingCircleRadius(game.templates.DefinitionAt(ref->index).geometry) * Fixed::FromRatio(4, 5);
	location = location - offset * radius;
	ForTeam(game, team, [&](ecs::Entity entity) {
		if (HasAi(game, entity))
			GuardPosition(game, entity, location);
	});
}

inline void ApplyGuards(GameWorld &game)
{
	auto *resource = game.world.FindResource<GuardEvents>();
	if (resource == nullptr)
		return;
	std::vector<GuardEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	for (const GuardEvent &event : events)
	{
		if (!game.world.IsAlive(event.unit))
			continue;
		if (event.kind == GuardEvent::Kind::End)
		{
			if (game.world.Has<Guard>(event.unit))
				game.world.Remove<Guard>(event.unit);
			AiIdle(game, event.unit);
			continue;
		}
		if (const auto *member = game.world.Get<engine::gameplay::TeamMember>(event.unit))
			SetTeamTarget(game, member->team, {});
	}
}
}
