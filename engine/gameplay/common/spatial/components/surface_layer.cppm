export module engine.gameplay.common.spatial.components.surface_layer;
import std;

export import engine.ecs.core.component_registry;

// The deck a thing stands on (Object::m_layer past LAYER_GROUND: a DeckSurfaces layer); none: the ground.
export namespace engine::gameplay
{
struct SurfaceLayer
{
	std::uint8_t layer{0};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::SurfaceLayer>
{
	static constexpr std::string_view StableName = "engine.gameplay.surface_layer";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
