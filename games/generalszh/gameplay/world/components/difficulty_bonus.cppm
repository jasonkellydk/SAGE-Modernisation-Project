export module games.generalszh.gameplay.world.components.difficulty_bonus;
import std;

export import engine.ecs.core.component_registry;

// An object that has its player's single-player difficulty bonus (Object::m_isReceivingDifficultyBonus). Simulation
// state: checkpointed.
export namespace generalszh::gameplay
{
struct DifficultyBonus
{
	std::uint8_t receiving{0};
	std::uint8_t reserved[3]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::DifficultyBonus>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.difficulty_bonus";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
