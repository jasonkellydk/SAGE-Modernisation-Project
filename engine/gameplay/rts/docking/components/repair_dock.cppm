export module engine.gameplay.rts.docking.components.repair_dock;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// A dock that repairs those it takes (the original's RepairDockUpdate): each tick of its business it gives the
// docker `healthPerTick`, worked out as that docker's turn began (what it lacked then over `fullHealTicks`,
// TimeForFullHeal), until it is whole. `lastRepair` is the docker it worked that out for.
export namespace engine::gameplay
{
struct RepairDock
{
	Engine::Math::Fixed fullHealTicks{Engine::Math::Fixed::One()};
	ecs::Entity lastRepair;
	Engine::Math::Fixed healthPerTick;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::RepairDock>
{
	static constexpr std::string_view StableName = "engine.gameplay.repair_dock";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::RepairDock &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.fullHealTicks.Raw()));
		hasher.AppendU64((std::uint64_t{value.lastRepair.index} << 32) | value.lastRepair.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.healthPerTick.Raw()));
	}
};
}
