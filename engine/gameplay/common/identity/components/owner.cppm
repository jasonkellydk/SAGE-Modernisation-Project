export module engine.gameplay.common.identity.components.owner;
import std;

export import engine.ecs.core.component_registry;

// The player an entity belongs to (an index into the session's players).
export namespace engine::gameplay
{
struct Owner
{
	std::uint32_t player{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Owner>
{
	static constexpr std::string_view StableName = "engine.gameplay.owner";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
