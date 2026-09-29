export module engine.gameplay.rts.movement.components.wanderer;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A body that never stands still (the original's WanderAIUpdate: a burning man running about): whenever it is idle it
// heads off somewhere near.
export namespace engine::gameplay
{
struct Wanderer
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Wanderer>
{
	static constexpr std::string_view StableName = "engine.gameplay.wanderer";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
