export module engine.gameplay.common.appearance.components.indicator_color;
import std;

export import engine.ecs.core.component_registry;

// A colour a script gave the object in place of its player's (Object::m_indicatorColor, 0xAARRGGBB; none: 0, its
// player's colour). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct IndicatorColor
{
	std::uint32_t argb{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::IndicatorColor>
{
	static constexpr std::string_view StableName = "engine.gameplay.indicator_color";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
