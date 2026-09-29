export module engine.gameplay.common.appearance.components.model_override;
import std;

export import engine.ecs.core.component_registry;

// A model chosen for this entity instead of its definition's (debris picks
// one of several chunks): an id into the game's model names, 0 = none.
export namespace engine::gameplay
{
struct ModelOverride
{
	std::uint32_t model{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ModelOverride>
{
	static constexpr std::string_view StableName = "engine.gameplay.model_override";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
