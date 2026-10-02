export module engine.gameplay.common.status.components.status_damage;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// The status STATUS damage gave (the original's StatusDamageHelper): which object status bit and the tick it heals
// (NoStatus: none). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct StatusDamage
{
	static constexpr std::uint32_t NoStatus = 0xFFFFFFFFu;
	std::uint64_t healTick{0};
	std::uint32_t status{NoStatus};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::StatusDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.status_damage";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
