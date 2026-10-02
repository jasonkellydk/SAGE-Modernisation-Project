export module games.generalszh.gameplay.teams.components.tech_building;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A tech building (TechBuildingBehavior): CAPTURED while a playable side holds it, and left to the neutral team (no
// one's bonus from it) once it dies. (No shipped tech building sets PulseFX.)
export namespace generalszh::gameplay
{
struct TechBuilding
{
	std::uint8_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::TechBuilding>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.tech_building";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
