export module games.generalszh.gameplay.crates.components.crate;
import std;

export import engine.ecs.core.component_registry;

// A crate: an object with a CrateCollide module (whoever runs into it may pick it up).
export namespace generalszh::gameplay
{
struct Crate
{
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Crate>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.crate";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
