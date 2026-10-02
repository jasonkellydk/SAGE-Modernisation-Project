export module engine.gameplay.common.health.components.inactive_body;
import std;

export import engine.ecs.core.component_registry;

// A body that is never hurt (the original's InactiveBody: rocks, props): no health, always pristine, and effectively
// dead from the start (setEffectivelyDead in its constructor), so nothing counts it among the living. Only an outright
// kill (unresistable damage: Object::kill) makes it die, once. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct InactiveBody
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::InactiveBody>
{
	static constexpr std::string_view StableName = "engine.gameplay.inactive_body";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
