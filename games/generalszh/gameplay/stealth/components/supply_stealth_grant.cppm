export module games.generalszh.gameplay.stealth.components.supply_stealth_grant;
import std;

export import engine.ecs.core.component_registry;

// A supply centre's SupplyCenterDockUpdate GrantTemporaryStealth: the ticks of stealth it grants a harvester that brings
// it cash while it is itself stealthed. Definition data on the entity. Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct SupplyStealthGrant
{
	std::uint32_t deliveryTicks{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::SupplyStealthGrant>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.supply_stealth_grant";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
