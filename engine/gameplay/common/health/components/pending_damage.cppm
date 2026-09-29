export module engine.gameplay.common.health.components.pending_damage;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Damage dealt to an entity by a rule rather than a weapon (losing a parachute, landing in water), queued where the
// rule runs and taken with the next damage pass: from whom, how much, and its damage and death types.
export namespace engine::gameplay
{
struct PendingDamage
{
	ecs::Entity source;
	Engine::Math::Fixed amount;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
};

// The original's HUGE_DAMAGE_AMOUNT: enough to kill anything.
inline Engine::Math::Fixed HugeDamage() noexcept { return Engine::Math::Fixed::FromInt(999999); }
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PendingDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.pending_damage";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
