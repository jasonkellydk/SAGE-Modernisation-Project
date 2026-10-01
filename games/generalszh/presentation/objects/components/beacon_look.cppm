export module games.generalszh.presentation.objects.components.beacon_look;
import std;

import engine.ecs.core.component_registry;

// A multiplayer beacon as this client shows it (BeaconClientUpdate), a side table on the simulation's own beacon: its
// smoke in the particle world (0: none), the tick of its last radar pulse (the tick it was first seen, at first) and
// whether this client hides it (hideBeacon: an enemy's placed, or another's told to go). And its caption
// (Drawable::setCaptionText: MSG_SET_BEACON_TEXT; none: no caption).
export namespace generalszh::presentation
{
struct BeaconLook
{
	std::uint64_t smoke{0};
	std::uint64_t lastPulse{0};
	std::uint8_t hidden{0};
	std::uint8_t reserved[7]{};
};

struct BeaconCaption
{
	std::u16string text;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::BeaconLook>
{
	static constexpr std::string_view StableName = "generalszh.presentation.beacon_look";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
template<>
struct ComponentTraits<generalszh::presentation::BeaconCaption>
{
	static constexpr std::string_view StableName = "generalszh.presentation.beacon_caption";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
