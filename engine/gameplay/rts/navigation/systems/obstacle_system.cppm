export module engine.gameplay.rts.navigation.systems.obstacle_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.navigation.components.navigation;
export import engine.gameplay.rts.navigation.algorithms.grid_classification;
export import engine.gameplay.rts.navigation.algorithms.clearance;
export import engine.gameplay.common.spatial.components.transform;

// Keeps the grid's obstacles current, once a tick before anything routes:
// obstacles whose entities are gone (or no longer obstacles) come out, new
// ones go in, in entity order; the clearance planes are rebuilt around each
// change. What was stamped is recorded with its pose, so it comes out as it
// went in.
export namespace engine::gameplay
{
struct ObstacleSystem
{
	using Query = ecs::Query<ecs::Write<NavigationObstacle>, ecs::Read<Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<NavigationObstacle>>;
	using Resources = ecs::Resources<ecs::Write<NavigationGrid>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		NavigationGrid &grid = context.Write<NavigationGrid>();
		const auto lookup = context.Lookup<Lookup>();
		const auto refresh = [&](const std::optional<GridRegion> &region) {
			if (!region)
				return;
			for (ClearancePlane &plane : grid.Clearance())
				BuildClearance(grid, plane, (*region)[0], (*region)[1], (*region)[2], (*region)[3]);
		};
		auto &stamped = grid.Stamped();
		for (std::size_t index = 0; index < stamped.size();)
		{
			const StampedObstacle record = stamped[index];
			if (lookup.IsAlive(record.entity) && lookup.Get<NavigationObstacle>(record.entity) != nullptr)
			{
				++index;
				continue;
			}
			refresh(StampFootprint(grid, record.entity, record.footprint, record.position, record.orientation, false));
			stamped.erase(stamped.begin() + static_cast<std::ptrdiff_t>(index));
		}
		std::vector<std::pair<ecs::Entity, std::pair<NavigationObstacle *, const Transform *>>> fresh;
		query.ForEachChunk([&](auto chunk) {
			auto obstacles = chunk.template Get<NavigationObstacle>();
			const auto transforms = chunk.template Get<Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < obstacles.size(); ++row)
				if (!obstacles[row].stamped)
					fresh.push_back({entities[row], {&obstacles[row], &transforms[row]}});
		});
		std::sort(fresh.begin(), fresh.end(), [](const auto &a, const auto &b) { return a.first.index < b.first.index; });
		for (auto &[entity, parts] : fresh)
		{
			NavigationObstacle &obstacle = *parts.first;
			const Transform &transform = *parts.second;
			obstacle.stamped = true;
			refresh(StampFootprint(grid, entity, obstacle.footprint, transform.position.XY(), transform.facing, true));
			stamped.push_back({entity, obstacle.footprint, transform.position.XY(), transform.facing});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ObstacleSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.navigation_obstacles";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
