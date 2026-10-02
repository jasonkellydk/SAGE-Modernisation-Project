export module engine.gameplay.rts.combat.components.sneaky_target;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// A target its attackers miss for a while (AIUpdateInterface::getSneakyTargetingOffset: the Aurora's JetAIUpdate,
// SneakyOffsetWhenAttacking and AttackersMissPersistTime): until `until` (exclusive), a shot at it aims `offset` off it
// (its SneakyOffsetWhenAttacking along its facing as the tick began, as its position is taken for the aim: behind it for a
// negative offset) at a position, not at it, and no projectile runs into it (shouldProjectileCollideWith). Its owner keeps
// it up to date each tick. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct SneakyTarget
{
	Engine::Math::FixedVector2 offset;
	std::uint64_t until{0};

	bool Active(std::uint64_t tick) const noexcept { return until != 0 && tick < until; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SneakyTarget>
{
	static constexpr std::string_view StableName = "engine.gameplay.sneaky_target";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
