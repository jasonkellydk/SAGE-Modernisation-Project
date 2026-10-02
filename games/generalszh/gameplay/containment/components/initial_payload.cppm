export module games.generalszh.gameplay.containment.components.initial_payload;
import std;

export import engine.ecs.core.component_registry;

// A TransportContain's InitialPayload not made yet (TransportContain::m_payloadCreated false): what (a definition index)
// and how many; or an OverlordContain's PayloadTemplateName (`ownTeam`: made on the carrier's own team, as
// OverlordContain::createPayload does, not its player's default team). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct InitialPayload
{
	std::uint32_t definition{0};
	std::uint32_t count{0};
	std::uint32_t ownTeam{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::InitialPayload>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.initial_payload";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
