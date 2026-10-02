export module engine.gameplay.rts.movement.algorithms.move_paths;
import std;

export import engine.ecs.core.world;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.resources.path_points;

// The goal path's own operations (setGoalPath, addToGoalPath, getGoalPathPosition), between ticks or in batch work that
// owns the world: a path's points kept in PathPoints, the thing's MovePath pointing at them.
export namespace engine::gameplay
{
// The path's `index`-th point.
inline Engine::Math::FixedVector2 MovePathPoint(const PathPoints &points, const MovePath &path, std::uint32_t index) noexcept
{
	return points.At(path.block, index);
}

// setGoalPath: `entity` follows `route` from its `next`-th point on, as `kind` (a block of its own; the one it had is
// left for the sweep).
inline void SetMovePath(ecs::World &world, ecs::Entity entity, std::span<const Engine::Math::FixedVector2> route, std::uint32_t next,
	MovePathKind kind)
{
	auto *points = world.FindResource<PathPoints>();
	if (points == nullptr || !world.IsAlive(entity))
		return;
	const std::uint32_t block = points->Allocate(entity, static_cast<std::uint32_t>(route.size()));
	for (std::uint32_t index = 0; index < route.size(); ++index)
		points->Set(block, index, route[index]);
	if (!world.Has<MovePath>(entity))
		world.Add<MovePath>(entity);
	*world.Get<MovePath>(entity) = MovePath{block, static_cast<std::uint32_t>(route.size()), next, kind};
}

// addToGoalPath: one more point on the end of the path `entity` follows (its block grown as it needs).
inline void AppendMovePathPoint(ecs::World &world, ecs::Entity entity, Engine::Math::FixedVector2 point)
{
	auto *points = world.FindResource<PathPoints>();
	MovePath *path = world.IsAlive(entity) ? world.Get<MovePath>(entity) : nullptr;
	if (points == nullptr || path == nullptr)
		return;
	path->block = points->Reserve(path->block, path->count, path->count + 1);
	points->Set(path->block, path->count++, point);
}
}
