export module engine.gameplay.common.lifetime.components.lifetime;
import std;

export import engine.ecs.core.component_registry;

// A limited life: at `expiresTick` the entity is killed (it dies as if
// killed outright, playing its die behaviours: the original's
// LifetimeUpdate) or, when `deletes`, simply removed (DeletionUpdate).
export namespace engine::gameplay
{
struct Lifetime
{
	std::uint64_t expiresTick{0};
	std::uint32_t deletes{0};
	std::uint32_t deathType{0}; // how it dies when killed (the game's death types)
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Lifetime>
{
	static constexpr std::string_view StableName = "engine.gameplay.lifetime";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
