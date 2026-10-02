export module engine.gameplay.common.health.components.damage_scalar;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// ActiveBody::m_damageScalar: what the damage a body takes is multiplied by, once its armor has weighed it (not for the
// damage types that go around armor bonuses: the catalog's unscaled types, nor for handled or subdual damage).
// applyDamageScalar multiplies it; bodies without the component take 1. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct DamageScalar
{
	Engine::Math::Fixed scalar{Engine::Math::Fixed::One()};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DamageScalar>
{
	static constexpr std::string_view StableName = "engine.gameplay.damage_scalar";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::DamageScalar &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.scalar.Raw()));
	}
};
}
