export module engine.gameplay.common.health.components.second_life;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;

// A body whose first death is a change of state (the original's UndeadBody): until it has used it, a health-damaging
// hit (not unresistable) the armor would let through for all its health is cut to leave it one point, and then it
// starts its second life: its maximum becomes `maximum`, fully healed (SecondLifeMaxHealth). The game hears of it
// (SecondLives) to carry out the rest. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct SecondLife
{
	Engine::Math::Fixed maximum{Engine::Math::Fixed::One()};
	std::uint32_t started{0};
	std::uint32_t reserved{0}; // no padding: checkpoints hold its bytes
};

// UndeadBody::startSecondLife this tick: who, and the hit's death type and source (its DamageInfo).
struct SecondLifeStart
{
	ecs::Entity entity;
	ecs::Entity source;
	std::uint32_t deathType{0};
	std::uint32_t damageType{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SecondLife>
{
	static constexpr std::string_view StableName = "engine.gameplay.second_life";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::SecondLife &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.maximum.Raw()));
		hasher.AppendU64(value.started);
	}
};
}
