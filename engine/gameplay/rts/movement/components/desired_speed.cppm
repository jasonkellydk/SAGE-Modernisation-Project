export module engine.gameplay.rts.movement.components.desired_speed;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// The speed its AI asks it to go at (AIUpdateInterface::setDesiredSpeed, m_desiredSpeed: a group moving as a team goes at
// its slowest member's speed, AIGroup::getSpeed): its locomotor's maximum is capped by it, along a route and straight at a
// point alike (doLocomotor). None: as fast as it can (FAST_AS_POSSIBLE). Whoever asks for it sets and removes it.
// Simulation state: checkpointed.
export namespace engine::gameplay
{
struct DesiredSpeed
{
	Engine::Math::Fixed speed;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DesiredSpeed>
{
	static constexpr std::string_view StableName = "engine.gameplay.desired_speed";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::DesiredSpeed &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.speed.Raw()));
	}
};
}
