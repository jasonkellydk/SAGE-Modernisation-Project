export module games.generalszh.presentation.hud.resources.radar_sounds;
import std;

import engine.ecs.system.system;

// Player::addRadar / removeRadar / enableRadar / disableRadar's sounds: MiscAudio's RadarNotifyOnlineSound and
// RadarNotifyOfflineSound (RadarSounds, from the content), and which players had radar when the presentation last
// looked (RadarHeard: presentation state, not saved; the first look only takes note).
export namespace generalszh::presentation
{
struct RadarSounds
{
	std::string online;  // MiscAudio RadarNotifyOnlineSound
	std::string offline; // MiscAudio RadarNotifyOfflineSound
};

struct RadarHeard
{
	std::vector<std::uint8_t> had; // by player
	bool seen{false};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RadarSounds>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_sounds";
};
template<>
struct ResourceTraits<generalszh::presentation::RadarHeard>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_heard";
};
}
