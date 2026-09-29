export module engine.gameplay.common.identity.components.definition_ref;
import std;

export import engine.ecs.core.component_registry;

// Which data definition (object template) an entity was built from, as an
// index into the session's definition list. The game owns what it indexes.
export namespace engine::gameplay
{
struct DefinitionRef
{
	std::uint32_t index{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DefinitionRef>
{
	static constexpr std::string_view StableName = "engine.gameplay.definition_ref";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
