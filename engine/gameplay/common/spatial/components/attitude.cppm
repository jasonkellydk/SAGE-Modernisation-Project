export module engine.gameplay.common.spatial.components.attitude;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.TurnAngle;

// Tilt beyond facing: pitch (nose up/down) and roll (around the forward
// axis), for things that tumble (flung debris, wrecks). Entities without it
// stand upright.
export namespace engine::gameplay
{
struct Attitude
{
	Engine::Math::TurnAngle pitch;
	Engine::Math::TurnAngle roll;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Attitude>
{
	static constexpr std::string_view StableName = "engine.gameplay.attitude";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
