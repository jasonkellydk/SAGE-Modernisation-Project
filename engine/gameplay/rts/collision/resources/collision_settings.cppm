export module engine.gameplay.rts.collision.resources.collision_settings;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Bodies against what stands firm (GameData): how much of its speed a body keeps bouncing off (StructureStiffness,
// kept within 0.01 and 0.99), and the height above which one falling into a structure is lost in it
// (DefaultStructureRubbleHeight).
export namespace engine::gameplay
{
struct CollisionSettings
{
	Engine::Math::Fixed structureStiffness{Engine::Math::Fixed::FromRatio(3, 10)};
	Engine::Math::Fixed rubbleHeight{Engine::Math::Fixed::FromInt(10)};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::CollisionSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.collision_settings";
};
}
