export module engine.gameplay.common.appearance.components.draw_offset;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// Where the entity is drawn against where it is (the original's drawable
// instance transform): raised by `z` (below zero: sunk), shaking sideways
// by up to `shudder` either way. Simulation state the presentation reads.
export namespace engine::gameplay
{
struct DrawOffset
{
	Engine::Math::Fixed z;
	Engine::Math::Fixed shudder;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DrawOffset>
{
	static constexpr std::string_view StableName = "engine.gameplay.draw_offset";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
