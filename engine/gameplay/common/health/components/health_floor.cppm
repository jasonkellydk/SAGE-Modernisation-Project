export module engine.gameplay.common.health.components.health_floor;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// Health that damage cannot take below one point: each hit is cut to leave one
// point (before armor weighs it), unless it is of the damage type `exempt`,
// which still kills. Without an exempt type nothing kills it, a kill request
// included (an immortal body).
export namespace engine::gameplay
{
struct HealthFloor
{
	static constexpr std::uint32_t NoExemptType = 0xFFFFFFFFu;
	std::uint32_t exempt{NoExemptType};
	std::uint32_t reserved{0};
	// How much it keeps: a point (the original's highlander and immortal bodies), or less (a regenerating mine keeps
	// MinefieldBehavior's MIN_HEALTH 0.1: its onDamage raises it before its death is judged).
	Engine::Math::Fixed least{Engine::Math::Fixed::One()};

	bool Immortal() const noexcept { return exempt == NoExemptType; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::HealthFloor>
{
	static constexpr std::string_view StableName = "engine.gameplay.health_floor";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::HealthFloor &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.exempt);
		hasher.AppendU64(static_cast<std::uint64_t>(value.least.Raw()));
	}
};
}
