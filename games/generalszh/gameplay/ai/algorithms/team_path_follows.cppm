export module games.generalszh.gameplay.ai.algorithms.team_path_follows;
import std;
import engine.gameplay.rts.navigation.algorithms.waypoint_steps;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.components.team_path_follow;
export import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.combat.components.attack_move_resume;
import engine.gameplay.rts.combat.components.attack_move;
import engine.gameplay.rts.movement.components.desired_speed;
import engine.gameplay.rts.movement.components.path_completed;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.movement.resources.movement_penalty;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.resources.waypoint_graph;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.health.components.health;
import Engine.Core.Math.FixedRandom;

// A team following a waypoint path as a team (AIGroup::groupFollowWaypointPathAsTeam -> AIUpdateInterface::
// aiFollowWaypointPathAsTeam -> AIFollowWaypointPathState with m_moveAsGroup, AIStates.cpp): each member keeps its
// offset from the group's centre at every waypoint and goes at the group's speed (its slowest member's); the team shares
// one current waypoint: the first member there (or, for a skirmish computer player's, once the team's centre is within
// SkirmishGroupFudgeDistance times its member count of that member's goal) moves the whole team on to the next.
export namespace generalszh::gameplay
{
namespace team_path_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// AIGroup::add: an object with an AI, or a structure or ALWAYS_SELECTABLE thing without one.
inline bool HasAi(const GameWorld &game, ecs::Entity unit)
{
	return game.world.Get<gp::MoveOrder>(unit) != nullptr || game.world.Get<gp::Locomotion>(unit) != nullptr;
}
inline bool InGroup(const GameWorld &game, ecs::Entity unit)
{
	if (!game.world.IsAlive(unit))
		return false;
	if (HasAi(game, unit))
		return true;
	const auto *ref = game.world.Get<gp::DefinitionRef>(unit);
	if (ref == nullptr)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	return kind.Is("STRUCTURE") || kind.Is("ALWAYS_SELECTABLE");
}
inline bool Held(const GameWorld &game, ecs::Entity unit)
{
	const auto *disabled = game.world.Get<gp::Disabled>(unit);
	return disabled != nullptr && (disabled->mask & gp::disabled_type::Held) != 0;
}

// AIGroup::getCenter: the middle of its members with an AI that are not held (none such: of the others not held).
inline std::optional<FixedVector2> GroupCenter(const GameWorld &game, std::span<const ecs::Entity> group)
{
	for (const bool ais : {true, false})
	{
		FixedVector2 total;
		std::int64_t count = 0;
		for (const ecs::Entity unit : group)
		{
			if (Held(game, unit) || (ais && !HasAi(game, unit)))
				continue;
			total = total + game.world.Get<gp::Transform>(unit)->position.XY();
			++count;
		}
		if (count > 0)
			return FixedVector2{total.x / Fixed::FromInt(count), total.y / Fixed::FromInt(count)};
		if (ais && !group.empty())
			continue;
		break;
	}
	return std::nullopt;
}

// AIGroup::recompute's m_speed: the slowest current locomotor speed of its members that move (not immobile, not held,
// with an AI), leaving out those slowed by their damage (no better than MovementPenaltyDamageState); none: as fast as
// possible.
inline std::optional<Fixed> GroupSpeed(const GameWorld &game, std::span<const ecs::Entity> group)
{
	const auto *penalty = game.world.FindResource<gp::MovementPenalty>();
	std::optional<Fixed> speed;
	for (const ecs::Entity unit : group)
	{
		const auto *ref = game.world.Get<gp::DefinitionRef>(unit);
		if (ref != nullptr && game.templates.DefinitionAt(ref->index).Is("IMMOBILE"))
			continue;
		const auto *motion = game.world.Get<gp::Locomotion>(unit);
		if (Held(game, unit) || motion == nullptr)
			continue;
		const auto *health = game.world.Get<gp::Health>(unit);
		if (penalty != nullptr && health != nullptr && penalty->Applies(*health))
			continue;
		if (!speed || motion->locomotor.maxSpeed < *speed)
			speed = motion->locomotor.maxSpeed;
	}
	return speed;
}

// AIFollowWaypointPathState::hasNextWaypoint / getNextWaypoint on the map's waypoints, its choices from the session's
// stream.
inline bool HasNextWaypoint(const GameWorld &game, std::uint32_t current, std::uint32_t prior)
{
	return engine::gameplay::HasNextWaypoint(game.waypoints, current, prior);
}

inline std::uint32_t NextWaypoint(GameWorld &game, std::uint32_t current, std::uint32_t prior)
{
	return engine::gameplay::NextWaypoint(game.waypoints, current, prior, game.random);
}

// AIFollowWaypointPathState::computeGoal: the waypoint plus the member's offset (a waypoint on the wall whose offset goal
// is off it: the waypoint itself, on the wall); a waypoint on the map whose offset goal is off it is held a cell inside
// the map's edge.
inline FixedVector2 GoalAt(const GameWorld &game, std::uint32_t waypoint, FixedVector2 offset)
{
	const FixedVector2 destination = game.waypoints.Position(waypoint).XY();
	FixedVector2 goal = destination + offset;
	if (const auto *decks = game.world.FindResource<gp::DeckSurfaces>())
		goal = gp::WaypointGoalOnWall(*decks, destination, goal);
	const auto [low, high] = game.ground.Extent();
	const auto inside = [&](FixedVector2 at) { return low.x <= at.x && at.x <= high.x && low.y <= at.y && at.y <= high.y; };
	if (inside(destination) && !inside(goal))
	{
		const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
		goal.x = std::clamp(goal.x, low.x + cell, high.x - cell);
		goal.y = std::clamp(goal.y, low.y + cell, high.y - cell);
	}
	return goal;
}

// computeGoal's m_goalLayer: waypoints are on the ground, except one on the wall (LAYER_WALL).
inline std::uint8_t GoalLayerAt(const GameWorld &game, std::uint32_t waypoint)
{
	const auto *decks = game.world.FindResource<gp::DeckSurfaces>();
	if (decks != nullptr && decks->wall.layer != 0 && gp::PointOnWall(*decks, game.waypoints.Position(waypoint).XY()))
		return decks->wall.layer;
	return gp::GroundLayer;
}

// computeGoal and on (AIInternalMoveToState's next leg): its move to the goal at its waypoint; a waypoint with a next one
// is only a leg on the way (setAdjustsDestination(false)), the last one's goal is adjusted and claimed. The route is
// planned afresh.
inline void SendTo(GameWorld &game, ecs::Entity unit, TeamPathFollow &follow)
{
	follow.goal = GoalAt(game, follow.waypoint, follow.offset);
	auto *order = game.world.Get<gp::MoveOrder>(unit);
	if (order == nullptr)
		return;
	const bool leg = HasNextWaypoint(game, follow.waypoint, follow.prior);
	*order = gp::MoveToPointOn(follow.goal, GoalLayerAt(game, follow.waypoint), leg ? gp::GoalClaim::None : gp::GoalClaim::Adjust);
	if (auto *route = game.world.Get<gp::Route>(unit))
		route->planned = false;
	// Attack-following (AIAttackFollowWaypointPathState): after a fight it goes back to this leg.
	if (auto *resume = game.world.Get<gp::AttackMoveResume>(unit))
		resume->order = *order;
}

// The state's success (no waypoint left): its machine goes idle (AI_IDLE).
inline void Finish(GameWorld &game, ecs::Entity unit)
{
	detail::EndTeamPathFollow(game, unit);
	AiIdle(game, unit);
}
}

// groupFollowWaypointPathAsTeam (TEAM_FOLLOW_WAYPOINTS / SKIRMISH_FOLLOW_APPROACH_PATH as a team): the group is the
// team's members (getTeamAsAIGroup); each member with an AI, in that order, enters the state from `waypoint`
// (aiFollowWaypointPathAsTeam from the script): its team's current waypoint becomes it, its offset is where it stands from
// the group's centre, it goes at the group's speed and sets off for its goal (a leg unless the waypoint is the last).
inline void FollowWaypointPathAsTeam(GameWorld &game, std::span<const ecs::Entity> members, std::uint32_t waypoint)
{
	using namespace team_path_detail;
	if (waypoint == TeamWaypoints::None)
		return;
	std::vector<ecs::Entity> group;
	for (const ecs::Entity unit : members)
		if (InGroup(game, unit))
			group.push_back(unit);
	const std::optional<FixedVector2> center = GroupCenter(game, group);
	const std::optional<Fixed> speed = GroupSpeed(game, group);
	auto &waypoints = game.world.Resource<TeamWaypoints>();
	const auto *ais = game.world.FindResource<AiPlayers>();
	for (const ecs::Entity unit : group)
	{
		const auto *member = game.world.Get<gp::TeamMember>(unit);
		if (!HasAi(game, unit) || member == nullptr || game.world.Get<gp::MoveOrder>(unit) == nullptr)
			continue;
		waypoints.Set(member->team, waypoint);
		TeamPathFollow follow;
		follow.waypoint = waypoint;
		const FixedVector2 at = game.world.Get<gp::Transform>(unit)->position.XY();
		follow.offset = center ? at - *center : FixedVector2{};
		const auto *owner = game.world.Get<gp::Owner>(unit);
		const AiPlayer *ai = ais != nullptr && owner != nullptr ? ais->Of(owner->player) : nullptr;
		follow.skirmish = ai != nullptr && ai->skirmish ? 1 : 0;
		follow.goal = GoalAt(game, waypoint, follow.offset);
		const bool leg = HasNextWaypoint(game, waypoint, TeamWaypoints::None);
		OrderMove(game, unit, follow.goal, false, true, leg ? gp::GoalClaim::None : gp::GoalClaim::Adjust);
		auto *order = game.world.Get<gp::MoveOrder>(unit);
		if (order == nullptr || order->mode != gp::MoveMode::Point || order->destination != follow.goal)
			continue; // it could not take the move (locked, carried, immobile)
		order->goalLayer = GoalLayerAt(game, waypoint);
		game.world.Add<TeamPathFollow>(unit);
		*game.world.Get<TeamPathFollow>(unit) = follow;
		if (speed)
		{
			if (!game.world.Has<gp::DesiredSpeed>(unit))
				game.world.Add<gp::DesiredSpeed>(unit);
			*game.world.Get<gp::DesiredSpeed>(unit) = gp::DesiredSpeed{*speed};
		}
		// Its first update: a computer player's alert or aggressive member attack-follows the path as a team instead.
		AttackFollowIfMoody(game, unit);
	}
}

// The tick's follower events (TeamPathFollowSystem), in order (AIFollowWaypointPathState::update): another order ended
// the state; its team moved on (a new waypoint: its goal there; none: done); or it got there: the first there takes the
// team on to the next waypoint (a random link, never back) and heads for it; with no next waypoint it has completed the
// path (setCompletedWaypoint: the scripts see it) and goes idle. A member whose team was moved on earlier in the same
// tick follows the team instead.
inline void ApplyTeamPathFollows(GameWorld &game)
{
	using namespace team_path_detail;
	auto *events = game.world.FindResource<TeamPathEvents>();
	auto *waypoints = game.world.FindResource<TeamWaypoints>();
	if (events == nullptr || waypoints == nullptr)
		return;
	std::vector<TeamPathEvent> tick;
	events->AppendTo(tick);
	events->Reset(0);
	for (const TeamPathEvent &event : tick)
	{
		if (!game.world.IsAlive(event.unit) || !game.world.Has<TeamPathFollow>(event.unit))
			continue;
		const auto *member = game.world.Get<gp::TeamMember>(event.unit);
		if (event.kind == TeamPathEventKind::Replaced || member == nullptr)
		{
			detail::EndTeamPathFollow(game, event.unit);
			// Attack-following as a team, the attack-follow state is over too.
			if (game.world.Has<gp::AttackMoveResume>(event.unit))
			{
				game.world.Remove<gp::AttackMoveResume>(event.unit);
				if (game.world.Has<gp::AttackMove>(event.unit))
					game.world.Remove<gp::AttackMove>(event.unit);
			}
			continue;
		}
		TeamPathFollow &follow = *game.world.Get<TeamPathFollow>(event.unit);
		const std::uint32_t teamWaypoint = waypoints->Of(member->team);
		if (event.kind == TeamPathEventKind::MovedOn || follow.waypoint != teamWaypoint)
		{
			follow.prior = follow.waypoint;
			follow.waypoint = teamWaypoint;
			if (follow.waypoint == TeamWaypoints::None)
			{
				Finish(game, event.unit);
				continue;
			}
			SendTo(game, event.unit, follow);
			continue;
		}
		const std::uint32_t next = NextWaypoint(game, follow.waypoint, follow.prior);
		follow.prior = follow.waypoint;
		if (next == TeamWaypoints::None)
		{
			const gp::PathCompleted completed{follow.prior, 0, game.tick};
			if (!game.world.Has<gp::PathCompleted>(event.unit))
				game.world.Add<gp::PathCompleted>(event.unit);
			*game.world.Get<gp::PathCompleted>(event.unit) = completed;
			Finish(game, event.unit);
			continue;
		}
		follow.waypoint = next;
		waypoints->Set(member->team, next);
		SendTo(game, event.unit, follow);
	}
}
}
