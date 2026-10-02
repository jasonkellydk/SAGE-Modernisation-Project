export module engine.gameplay.rts.collision.components.collider;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// A body that takes up room (its geometry's radius) and how it crushes and
// is crushed (the original's CrusherLevel / CrushableLevel: 0 crushes
// nothing; 255 is crushed by nothing).
export namespace engine::gameplay
{
struct Collider
{
	Engine::Math::Fixed radius;
	std::uint32_t crusherLevel{0};
	std::uint32_t crushableLevel{255};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Collider>
{
	static constexpr std::string_view StableName = "engine.gameplay.collider";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
