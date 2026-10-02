export module games.generalszh.gameplay.movement.algorithms.goal_claim_rules;
import std;

export import engine.gameplay.rts.navigation.algorithms.goal_claims;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.collision.components.collider;
export import engine.gameplay.rts.collision.components.squishable;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.rts.navigation.resources.goal_cells;
export import engine.gameplay.rts.navigation.components.pathfind_goal;
export import engine.ecs.core.world;
export import engine.gameplay.rts.navigation.components.ignored_obstacle;

// Whose goal claims a mover respects (Pathfinder::checkDestination, AIPathfind.cpp), read through `Access` (the world,
// or a system's lookup: IsAlive and Get<T>):
//   a claimant that is gone, dying (ActiveBody::onDie: removeObjectFromPathfindMap) or inside something
//   (OpenContain::addToContain: removeObjectFromPathfindMap) claims nothing;
//   an ally's claim (as the mover regards it: Object::getRelationship, its team's view first) is never usurped;
//   a claimant standing on its own claim (UNIT_PRESENT_FIXED: its position cells, the footprint about the cell it
//   stands in, cover the cell) keeps it unless the mover can crush or squish it (Object::canCrushOrSquish,
//   TEST_CRUSH_OR_SQUISH: not unmanned, not an ally, a crusher, and the other squishable or of a lower crushable level).
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

inline gp::GoalFootprint FootprintOf(const gp::NavigationAgent &agent) noexcept { return {agent.radius, agent.centered != 0}; }

template<typename Access>
struct GoalClaimRules
{
	const Access &access;
	const gp::Relationships &relationships;
	ecs::Entity self;

	std::uint32_t PlayerOf(ecs::Entity entity) const
	{
		const auto *owner = access.template Get<gp::Owner>(entity);
		return owner != nullptr ? owner->player : 0;
	}
	std::uint32_t TeamOf(ecs::Entity entity) const
	{
		const auto *member = access.template Get<gp::TeamMember>(entity);
		return member != nullptr ? member->team : gp::Relationships::NoTeam;
	}
	bool Present(ecs::Entity owner) const
	{
		return access.IsAlive(owner) && access.template Get<gp::Dying>(owner) == nullptr && access.template Get<gp::Passenger>(owner) == nullptr &&
			access.template Get<gp::OffMap>(owner) == nullptr;
	}
	bool Allied(ecs::Entity owner) const { return relationships.Allies(TeamOf(self), PlayerOf(self), TeamOf(owner), PlayerOf(owner)); }

	bool Standing(ecs::Entity owner, std::int32_t x, std::int32_t y) const
	{
		if (!Present(owner))
			return false;
		const auto *transform = access.template Get<gp::Transform>(owner);
		const auto *agent = access.template Get<gp::NavigationAgent>(owner);
		if (transform == nullptr || agent == nullptr)
			return false;
		const gp::GoalFootprint footprint = FootprintOf(*agent);
		const auto [cellX, cellY] = gp::ClaimCell(transform->position.XY(), footprint.centered);
		return gp::FootprintCovers(cellX, cellY, footprint, x, y);
	}

	bool CanCrushOrSquish(ecs::Entity other) const
	{
		if (const auto *disabled = access.template Get<gp::Disabled>(self); disabled != nullptr && (disabled->mask & gp::disabled_type::Unmanned) != 0)
			return false;
		if (Allied(other))
			return false;
		const auto *mine = access.template Get<gp::Collider>(self);
		const std::uint32_t crusher = mine != nullptr ? mine->crusherLevel : 0;
		if (crusher == 0)
			return false;
		if (access.template Get<gp::Squishable>(other) != nullptr)
			return true;
		const auto *theirs = access.template Get<gp::Collider>(other);
		return crusher > (theirs != nullptr ? theirs->crushableLevel : 255u);
	}

	bool Blocks(ecs::Entity owner, std::int32_t x, std::int32_t y) const
	{
		if (!Present(owner))
			return false;
		if (Allied(owner))
			return true; // don't usurp your allies' goals
		return Standing(owner, x, y) && !CanCrushOrSquish(owner);
	}
};

// m_logicalExtent: the playable area's cells (TerrainLogic::getExtent over the cell size; its high edge less one).
inline gp::LogicalExtent LogicalExtentOf(const gp::GroundHeight &ground) noexcept
{
	const auto extent = ground.Extent();
	const auto cell = [](Engine::Math::Fixed value) {
		return static_cast<std::int32_t>((value / Engine::Math::Fixed::FromInt(gp::PathfindCellSize)).Floor());
	};
	return {cell(extent[0].x), cell(extent[0].y), cell(extent[1].x) - 1, cell(extent[1].y) - 1};
}

// Whether the mover's player is a human's (a computer's units may end outside the playable area).
inline bool HumanMover(const gp::TeamRoster &roster, std::uint32_t player)
{
	return player >= roster.PlayerCount() || roster.PlayerAt(player).human;
}

// Pathfinder::adjustDestination for a ground mover, from the world as it stands (a unit just made or placed): where
// `destination` really ends for it; nothing when nothing within reach will do, or it is no ground mover.
inline std::optional<Engine::Math::FixedVector2> AdjustDestinationFor(ecs::World &world, ecs::Entity entity, Engine::Math::FixedVector2 destination)
{
	const auto *transform = world.Get<gp::Transform>(entity);
	const auto *agent = world.Get<gp::NavigationAgent>(entity);
	auto *grid = world.FindResource<gp::NavigationGrid>();
	auto *cells = world.FindResource<gp::GoalCells>();
	if (transform == nullptr || agent == nullptr || grid == nullptr || cells == nullptr)
		return std::nullopt;
	cells->Fit(grid->Width(), grid->Height());
	const auto *owner = world.Get<gp::Owner>(entity);
	const auto *ignored = world.Get<gp::IgnoredObstacle>(entity);
	const gp::GoalSeeker seeker{entity, ignored != nullptr ? ignored->obstacle : ecs::Entity{}, FootprintOf(*agent), agent->surfaces,
		owner == nullptr || HumanMover(world.Resource<gp::TeamRoster>(), owner->player), transform->position.XY()};
	const GoalClaimRules<ecs::World> rules{world, world.Resource<gp::Relationships>(), entity};
	return gp::AdjustDestination(*grid, *cells, seeker, LogicalExtentOf(world.Resource<gp::GroundHeight>()), destination, rules);
}

// GameLogic::startNewGame, for each map object with an AI that is not immobile (a ground mover here): its place adjusted
// off the claims of those placed before it (adjustDestination), claimed (updateGoal) and taken (setPosition); left
// where it is when nothing will do.
inline void ClaimStartingGoal(ecs::World &world, ecs::Entity entity)
{
	auto *transform = world.Get<gp::Transform>(entity);
	const auto *agent = world.Get<gp::NavigationAgent>(entity);
	auto *claim = world.Get<gp::PathfindGoal>(entity);
	if (transform == nullptr || agent == nullptr || claim == nullptr)
		return;
	const auto adjusted = AdjustDestinationFor(world, entity, transform->position.XY());
	if (!adjusted)
		return;
	gp::UpdateGoal(world.Resource<gp::GoalCells>(), entity, *claim, FootprintOf(*agent), *adjusted);
	// adjustCoordToCell: on the ground at the cell's point.
	transform->position = {adjusted->x, adjusted->y, world.Resource<gp::GroundHeight>().At(*adjusted)};
}
}
