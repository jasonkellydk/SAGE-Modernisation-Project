export module engine.gameplay.rts.containment.components.drop_homing;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;

// A dropped payload that steers itself on to where its delivery aimed (the original's SmartBombTargetHomingUpdate: the
// MOAB): the spot (m_target, told as its carrier drops it: SetTargetPosition with the delivery's move-to spot), whether
// it was told (m_targetReceived) and how much of its own course it keeps each tick (CourseCorrectionScalar, 0 to 1: 1 no
// homing, 0 snapping on). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct DropHoming
{
	Engine::Math::FixedVector2 target;
	Engine::Math::Fixed keep{Engine::Math::Fixed::One()};
	std::uint32_t received{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DropHoming>
{
	static constexpr std::string_view StableName = "engine.gameplay.drop_homing";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
