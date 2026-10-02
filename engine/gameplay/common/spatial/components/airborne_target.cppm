export module engine.gameplay.common.spatial.components.airborne_target;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// Whether a thing counts as an airborne target (the original's OBJECT_STATUS_AIRBORNE_TARGET, which only a unit's
// locomotor sets: AIUpdateInterface::doLocomotor, after its movement each frame, higher over the ground (or deck) under
// it than its locomotor's AirborneTargetingHeight; unset: never). `height` is that threshold (a locomotor's own while it
// has one), `airborne` the status as the last tick left it. The spatial index classes a target airborne or on the ground
// by it; a thing without one is classed by its height alone.
export namespace engine::gameplay
{
struct AirborneTarget
{
	Engine::Math::Fixed height{Engine::Math::Fixed::FromInt(std::numeric_limits<std::int32_t>::max())};
	std::uint8_t airborne{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AirborneTarget>
{
	static constexpr std::string_view StableName = "engine.gameplay.airborne_target";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
