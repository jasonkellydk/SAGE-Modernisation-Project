export module engine.gameplay.rts.collision.components.squishable;
import std;

import engine.ecs.core.component_registry;

// Squished by any crusher that runs over it (infantry: the original's SquishCollide).
export namespace engine::gameplay
{
struct Squishable
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Squishable>
{
	static constexpr std::string_view StableName = "engine.gameplay.squishable";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
