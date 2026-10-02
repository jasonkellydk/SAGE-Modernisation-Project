export module games.generalszh.presentation.objects.components.bridge_look;
import std;

export import engine.ecs.core.component_registry;

// A map-drawn bridge as the bridge buffer draws it (W3DBridge's m_curDamageState and the model it loaded): the damage
// state it last took from the terrain logic's bridge (`shown`), and the state whose model it shows (`model`: the same,
// or the state before when the new state's model could not load; NoBridgeModel when neither could). A new bridge shows
// its pristine model (W3DBridgeBuffer::addBridge: load(BODY_PRISTINE)).
export namespace generalszh::presentation
{
struct BridgeLook
{
	std::uint8_t shown{0};
	std::uint8_t model{0};
	std::uint8_t reserved[2]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::BridgeLook>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bridge_look";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
