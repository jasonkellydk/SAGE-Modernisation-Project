export module engine.gameplay.common.spatial.components.carried;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
import engine.ecs.system.system;

// On the map but carried by another entity that places it each tick (a rider hanging from its parachute: the
// original's containReactToTransformChange / positionRider): it does not move itself (its locomotor neither steers
// nor holds it to the ground) until let go.
export namespace engine::gameplay
{
struct Carried
{
	ecs::Entity carrier;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Carried>
{
	static constexpr std::string_view StableName = "engine.gameplay.carried";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
