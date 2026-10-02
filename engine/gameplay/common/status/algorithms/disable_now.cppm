export module engine.gameplay.common.status.algorithms.disable_now;
import std;

export import engine.ecs.core.world;
export import engine.gameplay.common.status.components.disabled_until;

// Object::setDisabledUntil at once, for game logic outside the systems (what DisableApplySystem does with a request):
// the type set, ending on `until` (DisabledForever: until cleared). Only on a live entity.
export namespace engine::gameplay
{
inline void DisableNow(ecs::World &world, ecs::Entity entity, std::uint32_t type, std::uint64_t until)
{
	if (!world.IsAlive(entity) || type == 0)
		return;
	if (!world.Has<Disabled>(entity))
		world.Add<Disabled>(entity);
	if (!world.Has<DisabledUntil>(entity))
		world.Add<DisabledUntil>(entity);
	world.Get<Disabled>(entity)->mask |= type;
	world.Get<DisabledUntil>(entity)->until[static_cast<std::size_t>(std::countr_zero(type))] = until;
}
}
