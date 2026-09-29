export module engine.gameplay.common.health.components.subdual;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// A body that can be subdued (the original's ActiveBody with a SubdualDamageCap above 0, and its SubdualDamageHelper):
// subdual damage it has taken instead of health (0..cap), how much it sheds and how often (SubdualDamageHealAmount every
// SubdualDamageHealRate ticks), the helper's countdown to the next shedding, whether the helper is awake (from a
// subdual hit until nothing is left), the tick of the last subdual hit (the helper wakes the tick after), the tick of
// the last subdual damage of any amount (attemptDamage weighs whether it is subdued then), and whether it is subdued
// (its damage reached its maximum health when last weighed: DISABLED_SUBDUED), and whether it is gaining subdual
// damage (Object::notifySubdualDamage: set by a subdual hit of more than none, cleared by any other subdual damage or
// shedding; its drawable's TINT_STATUS_GAINING_SUBDUAL_DAMAGE). Simulation state: checkpointed.
// SubdualChanges: this tick's bodies becoming subdued or free again, in order (the game carries out the rest of
// onSubdualChange: passengers idled, projectiles jammed). Cleared as each tick starts.
export namespace engine::gameplay
{
struct Subdual
{
	Engine::Math::Fixed damage;
	Engine::Math::Fixed cap;
	Engine::Math::Fixed healAmount;
	std::uint64_t healTicks{0};
	std::uint64_t countdown{0};
	std::uint64_t hitTick{0};
	std::uint64_t touchTick{0};
	std::uint8_t awake{0};
	std::uint8_t subdued{0};
	std::uint8_t gaining{0};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};

struct SubdualChange
{
	ecs::Entity entity;
	bool subdued{false};
	bool projectile{false};
};

struct SubdualChanges
{
	std::vector<SubdualChange> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Subdual>
{
	static constexpr std::string_view StableName = "engine.gameplay.subdual";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::SubdualChanges>
{
	static constexpr std::string_view StableName = "engine.gameplay.subdual_changes";
};
}
