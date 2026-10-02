export module engine.gameplay.common.physics.components.bounce_sound;
import std;

export import engine.ecs.core.component_registry;

// The sound a body makes each time it lands (the original's PhysicsBehavior
// bounce sound, given to thrown debris): an id into the game's sounds.
export namespace engine::gameplay
{
struct BounceSound
{
	std::uint32_t sound{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BounceSound>
{
	static constexpr std::string_view StableName = "engine.gameplay.bounce_sound";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
