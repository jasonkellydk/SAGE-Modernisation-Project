export module engine.gameplay.common.appearance.components.draw_hidden;
import std;

export import engine.ecs.core.component_registry;

// The original's Drawable::setDrawableHidden(true) on the entity: it is still in the world, but nothing of it is drawn
// (a guided missile gone off, holding its KILL_SELF state). Simulation state the presentation reads.
export namespace engine::gameplay
{
struct DrawHidden
{
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DrawHidden>
{
	static constexpr std::string_view StableName = "engine.gameplay.draw_hidden";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
