export module engine.gameplay.common.appearance.algorithms.special_model_states;
import std;

export import engine.ecs.core.world;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.appearance.components.special_model_state;

// Object::setSpecialModelConditionState(set, frames) on tick `now`: the special state it held goes
// (clearSpecialModelConditionStates), `bit` is set, and it holds until now + frames (at least one frame).
export namespace engine::gameplay
{
inline void SetSpecialModelState(ecs::World &world, ecs::Entity entity, std::uint32_t bit, std::uint64_t now, std::uint64_t frames)
{
	if (!world.IsAlive(entity))
		return;
	auto *look = world.Get<Appearance>(entity);
	if (look == nullptr)
	{
		world.Add<Appearance>(entity);
		look = world.Get<Appearance>(entity);
	}
	if (const auto *held = world.Get<SpecialModelState>(entity))
		look->Set(held->bit, false);
	look->Set(bit, true);
	if (!world.Has<SpecialModelState>(entity))
		world.Add<SpecialModelState>(entity);
	*world.Get<SpecialModelState>(entity) = SpecialModelState{now + (std::max<std::uint64_t>)(frames, 1), bit};
}
}
