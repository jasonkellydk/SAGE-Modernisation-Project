export module games.generalszh.gameplay.crates.components.hijacker;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A hijacker (HijackerUpdate): the vehicle it drives (m_targetID), where that vehicle last was (m_ejectPos), whether it
// is updating and in a vehicle (m_update, m_isInVehicle), and whether the vehicle was last significantly above the
// ground (m_wasTargetAirborne: it comes out by parachute). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct Hijacker
{
	ecs::Entity target;
	Engine::Math::FixedVector3 ejectPosition;
	std::uint8_t update{0};
	std::uint8_t inVehicle{0};
	std::uint8_t wasTargetAirborne{0};
	std::uint8_t reserved[5]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Hijacker>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hijacker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
