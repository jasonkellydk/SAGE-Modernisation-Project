export module engine.gameplay.common.appearance.components.part_overrides;
import std;

export import engine.ecs.core.component_registry;

// The part overrides an object has taken on, in the order it took them (the original's SubObjectsUpgrade: each module's
// ShowSubObjects / HideSubObjects, W3DModelDraw::showSubObject, kept over its model states): each an index among its
// definition's override sets (the game's own). Presentation draws its model's parts by them. Simulation state:
// checkpointed.
export namespace engine::gameplay
{
struct PartOverrides
{
	static constexpr std::size_t Capacity = 15;
	std::array<std::uint8_t, Capacity> applied{};
	std::uint8_t count{0};

	void Apply(std::uint8_t set) noexcept
	{
		if (count < Capacity)
			applied[count++] = set;
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PartOverrides>
{
	static constexpr std::string_view StableName = "engine.gameplay.part_overrides";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
