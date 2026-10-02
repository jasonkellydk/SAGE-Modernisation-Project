export module games.generalszh.gameplay.orders.algorithms.group_scatter;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.group_formations;
import games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
import engine.gameplay.rts.navigation.resources.goal_cells;
import engine.gameplay.rts.navigation.components.pathfind_goal;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.content.objects.object_definition;
import games.generalszh.gameplay.combat_drop.components.combat_drop;
import games.generalszh.gameplay.combat_drop.resources.deferred_orders;
import games.generalszh.commands.game_commands;
import engine.gameplay.common.identity.components.owner;

// AIGroup::groupScatter (GameLogicDispatch MSG_DO_SCATTER, CMD_FROM_PLAYER): the group's centre is its members' with an
// AI that are not held (getMinMaxAndCenter); each member not held, not IMMOBILE and with an AI lets its goal go
// (removeGoal) and is put in a SimpleObjectIterator by its squared distance from that centre (each insert at the
// iterator's front), sorted far to near (a stable merge sort); then, for each in turn, the centre's x moves 0.01 west
// and the member moves (aiMoveToPosition) four bounding circle radii on from where it stands, straight away from that
// centre (a zero offset stays put: Coord2D::normalize leaves it).
export namespace generalszh::gameplay
{
struct ScatterMember
{
	ecs::Entity unit;
	Engine::Math::FixedVector2 at;
	Engine::Math::Fixed radius; // GeometryInfo::getBoundingCircleRadius
};

// Where each eligible member (in the group's order) is sent, in the order they are sent.
inline std::vector<std::pair<ecs::Entity, Engine::Math::FixedVector2>> ScatterDestinations(Engine::Math::FixedVector2 centre,
	std::span<const ScatterMember> members)
{
	using Engine::Math::Fixed;
	struct Clump
	{
		const ScatterMember *member;
		Fixed numeric;
	};
	std::vector<Clump> clumps;
	clumps.reserve(members.size());
	for (const ScatterMember &member : members)
	{
		const Fixed dx = member.at.x - centre.x, dy = member.at.y - centre.y;
		clumps.insert(clumps.begin(), Clump{&member, dx * dx + dy * dy});
	}
	std::ranges::stable_sort(clumps, [](const Clump &a, const Clump &b) { return a.numeric > b.numeric; });
	std::vector<std::pair<ecs::Entity, Engine::Math::FixedVector2>> out;
	out.reserve(clumps.size());
	const Fixed step = Fixed::FromRatio(1, 100);
	for (const Clump &clump : clumps)
	{
		centre.x = centre.x - step;
		const Engine::Math::FixedVector2 delta = Engine::Math::Normalize(clump.member->at - centre);
		const Fixed reach = Fixed::FromInt(4) * clump.member->radius;
		out.push_back({clump.member->unit, {clump.member->at.x + delta.x * reach, clump.member->at.y + delta.y * reach}});
	}
	return out;
}

// The group's scatter, its members in the group's order.
inline void GroupScatter(GameWorld &game, std::uint32_t player, std::span<const ecs::Entity> group)
{
	namespace gp = engine::gameplay;
	namespace detail = group_formation_detail;
	const auto centre = detail::CentreOf(game, group);
	if (centre.count == 0)
		return;
	auto *cells = game.world.FindResource<gp::GoalCells>();
	std::vector<ScatterMember> members;
	for (const ecs::Entity unit : group)
	{
		if (!game.world.IsAlive(unit) || detail::Held(game, unit))
			continue;
		const auto *reference = game.world.Get<gp::DefinitionRef>(unit);
		const auto *transform = game.world.Get<gp::Transform>(unit);
		if (reference == nullptr || transform == nullptr)
			continue;
		const content::ObjectDefinition &definition = game.templates.DefinitionAt(reference->index);
		if (definition.Is("IMMOBILE") || !detail::HasAi(game, unit))
			continue;
		// TheAI->pathfinder()->removeGoal.
		if (auto *claim = game.world.Get<gp::PathfindGoal>(unit); claim != nullptr && cells != nullptr)
			if (const auto *agent = game.world.Get<gp::NavigationAgent>(unit))
				gp::RemoveGoal(*cells, unit, *claim, FootprintOf(*agent));
		members.push_back({unit, transform->position.XY(), content::BoundingCircleRadius(definition.geometry)});
	}
	// ChinookAIUpdate::aiDoCommand: a transport of the player's busy with a combat drop keeps this move (to the place the
	// whole group's centre gave it) for afterwards; any other member takes it now and forgets what was kept for it.
	auto *deferred = game.world.FindResource<DeferredOrders>();
	for (const auto &[unit, destination] : ScatterDestinations(centre.at, members))
	{
		const auto *owner = game.world.Get<gp::Owner>(unit);
		const CombatDrop *drop = game.world.Get<CombatDrop>(unit);
		if (deferred != nullptr && owner != nullptr && owner->player == player && drop != nullptr && drop->stage == CombatDropStage::Dropping)
		{
			deferred->Keep(unit, player, commands::Encode(commands::MoveTo{{unit}, destination, game.ground.At(destination)}));
			continue;
		}
		if (deferred != nullptr)
			deferred->Forget(unit);
		OrderMove(game, unit, destination, true);
	}
}
}
