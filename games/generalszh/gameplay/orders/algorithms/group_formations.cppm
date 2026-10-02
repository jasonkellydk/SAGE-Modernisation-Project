export module games.generalszh.gameplay.orders.algorithms.group_formations;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.group_columns;
import games.generalszh.gameplay.ai.algorithms.team_path_follows;
import games.generalszh.gameplay.orders.components.formation_move;
import engine.ecs.query.query;
import engine.gameplay.rts.movement.components.formation_member;
import engine.gameplay.rts.movement.components.desired_speed;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.containment.components.transport;

// User formations (EA AIGroup.cpp; Object::m_formationID / m_formationOffset as the units' FormationMember side table):
// - GroupCreateFormation (groupCreateFormation): a group that is a formation already (its first member with an AI not
//   held has a formation, and it has two or more such members; or a lone member in one) breaks it (NO_FORMATION_ID);
//   otherwise it becomes a new one (AI::getNextFormationID: an id no unit has: one past the highest any has). Either way
//   each member with an AI keeps its offset from the group's centre (getMinMaxAndCenter: the members with an AI not held).
// - GroupFormationId (getMinMaxAndCenter's isFormation): the formation the group moves as, if any.
// - MoveFormationToPos (friend_moveFormationToPos): each member not held keeps its offset, along the group's ground path
//   (its nodes six cells on from the start to the last six cells short of the end, when those come in that order) to the
//   path's end, its goal adjusted and claimed at once (aiFollowPath), or with no such path straight to the end (the
//   destination without a ground path) offset (aiMoveToPosition); a member in a formation goes at the group's speed
//   (setDesiredSpeed(getSpeed): its slowest member's) for that move (FormationMove).
// - BreakFormation (setFormationID(NO_FORMATION_ID)): a member moved on its own by a group move.
export namespace generalszh::gameplay
{
namespace group_formation_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

inline bool HasAi(GameWorld &game, ecs::Entity unit)
{
	return game.world.Get<gp::MoveOrder>(unit) != nullptr || game.world.Get<gp::Locomotion>(unit) != nullptr;
}

inline bool Held(GameWorld &game, ecs::Entity unit)
{
	const auto *off = game.world.Get<gp::Disabled>(unit);
	return game.world.Has<gp::Passenger>(unit) || (off != nullptr && (off->mask & gp::disabled_type::Held) != 0);
}

inline std::uint32_t FormationOf(GameWorld &game, ecs::Entity unit)
{
	const auto *member = game.world.IsAlive(unit) ? game.world.Get<gp::FormationMember>(unit) : nullptr;
	return member != nullptr ? member->id : 0u;
}

inline FixedVector2 OffsetOf(GameWorld &game, ecs::Entity unit)
{
	const auto *member = game.world.Get<gp::FormationMember>(unit);
	return member != nullptr ? member->offset : FixedVector2{};
}

inline void SetFormation(GameWorld &game, ecs::Entity unit, std::uint32_t id, FixedVector2 offset)
{
	if (!game.world.Has<gp::FormationMember>(unit))
		game.world.Add<gp::FormationMember>(unit);
	*game.world.Get<gp::FormationMember>(unit) = {id, 0, offset};
}

// getMinMaxAndCenter: the centre of the members with an AI not held, their count and the first one's formation.
struct Centre
{
	FixedVector2 at;
	std::int64_t count{0};
	std::uint32_t id{0};
};

inline Centre CentreOf(GameWorld &game, std::span<const ecs::Entity> group)
{
	Centre centre;
	FixedVector2 sum;
	for (const ecs::Entity unit : group)
	{
		if (!game.world.IsAlive(unit) || Held(game, unit) || !HasAi(game, unit) || game.world.Get<gp::Transform>(unit) == nullptr)
			continue;
		if (centre.count == 0)
			centre.id = FormationOf(game, unit);
		sum += game.world.Get<gp::Transform>(unit)->position.XY();
		++centre.count;
	}
	if (centre.count > 0)
		centre.at = sum / Fixed::FromInt(centre.count);
	return centre;
}
}

// getMinMaxAndCenter's isFormation: the first member's formation (with an AI, not held), when two or more such move.
inline std::uint32_t GroupFormationId(GameWorld &game, std::span<const ecs::Entity> group)
{
	const auto centre = group_formation_detail::CentreOf(game, group);
	return centre.count >= 2 ? centre.id : 0u;
}

// setFormationID(NO_FORMATION_ID): out of its formation (its place kept).
inline void BreakFormation(GameWorld &game, ecs::Entity unit)
{
	if (auto *member = game.world.IsAlive(unit) ? game.world.Get<engine::gameplay::FormationMember>(unit) : nullptr)
		member->id = 0;
}

// AIGroup::groupCreateFormation.
inline void GroupCreateFormation(GameWorld &game, std::span<const ecs::Entity> group)
{
	using namespace group_formation_detail;
	const Centre centre = CentreOf(game, group);
	bool isFormation = centre.count >= 2 && centre.id != 0;
	std::int64_t count = 0;
	std::uint32_t countId = 0;
	for (const ecs::Entity unit : group)
	{
		++count;
		countId = FormationOf(game, unit);
	}
	if (count == 1 && countId != 0)
		isFormation = true;
	// AI::getNextFormationID: an id nobody has.
	std::uint32_t id = 0;
	if (!isFormation)
	{
		ecs::Query<ecs::Read<gp::FormationMember>> members(game.world);
		std::uint32_t highest = 0;
		members.ForEachChunk([&](auto chunk) {
			for (const gp::FormationMember &member : chunk.template Get<gp::FormationMember>())
				highest = std::max(highest, member.id);
		});
		id = highest + 1;
	}
	for (const ecs::Entity unit : group)
		if (game.world.IsAlive(unit) && HasAi(game, unit) && game.world.Get<gp::Transform>(unit) != nullptr)
			SetFormation(game, unit, id, game.world.Get<gp::Transform>(unit)->position.XY() - centre.at);
}

// AIGroup::friend_moveFormationToPos, with the group's ground path (friend_computeGroundPath), if it found one.
inline void MoveFormationToPos(GameWorld &game, std::span<const ecs::Entity> group, const std::optional<std::vector<Engine::Math::FixedVector2>> &ground,
	Engine::Math::FixedVector2 pos, bool fromPlayer)
{
	using namespace group_formation_detail;
	auto &world = game.world;
	if (CentreOf(game, group).count == 0)
		return;
	const Fixed farEnough = Fixed::FromInt(gp::PathfindCellSize) * Fixed::FromInt(6);
	const Fixed farEnoughSqr = farEnough * farEnough;
	std::optional<std::size_t> startNode, endNode;
	FixedVector2 endPoint = pos;
	if (ground && !ground->empty())
	{
		const auto &nodes = *ground;
		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (Engine::Math::DistanceSquared(nodes[index], nodes.front()) > farEnoughSqr)
			{
				startNode = index;
				break;
			}
		endPoint = nodes.back();
		for (std::size_t index = 0; index < nodes.size(); ++index)
			if (Engine::Math::DistanceSquared(nodes[index], endPoint) > farEnoughSqr)
				endNode = index;
		// The start node at or after the end node: none.
		if (startNode && endNode && *startNode >= *endNode)
			endNode.reset();
		if (!startNode || !endNode)
		{
			startNode.reset();
			endNode.reset();
		}
	}
	// AIGroup::getSpeed: the slowest member's (not immobile, held or slowed by its damage).
	const std::optional<Fixed> speed = team_path_detail::GroupSpeed(game, group);
	for (const ecs::Entity unit : group)
	{
		if (!world.IsAlive(unit) || Held(game, unit) || !HasAi(game, unit))
			continue;
		const FixedVector2 offset = OffsetOf(game, unit);
		FixedVector2 goal = endPoint + offset;
		if (startNode)
		{
			std::vector<FixedVector2> path;
			for (std::size_t index = *startNode; index <= *endNode; ++index)
				path.push_back((*ground)[index] + offset);
			goal = group_column_detail::ClaimGoal(game, unit, goal);
			path.push_back(goal);
			group_column_detail::FollowGroupPath(game, unit, path, fromPlayer);
		}
		else
			OrderMoveTo(game, unit, goal, fromPlayer);
		// AIMoveToState / AIFollowPathState onEnter: a unit in a formation goes at its group's speed.
		const auto *order = world.Get<gp::MoveOrder>(unit);
		if (FormationOf(game, unit) == 0 || !speed || order == nullptr || order->mode != gp::MoveMode::Point)
			continue;
		if (!world.Has<gp::DesiredSpeed>(unit))
			world.Add<gp::DesiredSpeed>(unit);
		*world.Get<gp::DesiredSpeed>(unit) = gp::DesiredSpeed{*speed};
		if (!world.Has<FormationMove>(unit))
			world.Add<FormationMove>(unit);
		*world.Get<FormationMove>(unit) = FormationMove{goal};
	}
}
}
