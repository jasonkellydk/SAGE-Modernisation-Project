export module games.generalszh.gameplay.production.components.cost_modifying;
import std;

export import engine.ecs.core.component_registry;

// An object with CostModifierUpgrade modules: which of its upgrade triggers they are (bit per trigger index). While
// one has gone (UpgradeMux executed) its player's matching builds cost its Percentage more or less (CostToBuild).
export namespace generalszh::gameplay
{
struct CostModifying
{
	std::uint32_t triggers{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::CostModifying>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cost_modifying";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
