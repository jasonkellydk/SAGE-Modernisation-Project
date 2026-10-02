export module engine.gameplay.rts.containment.resources.drop_settings;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// How riders released from the air come down: their fall rate per tick.
export namespace engine::gameplay
{
struct DropSettings
{
	Engine::Math::Fixed fallRate{Engine::Math::Fixed::One()};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DropSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.drop_settings";
};
}
