export module engine.gameplay.common.identity.components.captured;
import std;

export import engine.ecs.core.component_registry;

// An object taken over from whoever had it (the original's Object::setCaptured: the CAPTURED private status, kept for
// good), for the rules that ask whether a player's object was captured.
export namespace engine::gameplay
{
struct Captured
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Captured>
{
	static constexpr std::string_view StableName = "engine.gameplay.captured";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
