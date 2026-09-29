export module engine.gameplay.rts.containment.components.cargo_size;
import std;

export import engine.ecs.core.component_registry;

// How many transport slots an entity takes when it rides.
export namespace engine::gameplay
{
struct CargoSize
{
	std::uint32_t slots{1};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::CargoSize>
{
	static constexpr std::string_view StableName = "engine.gameplay.cargo_size";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
