export module games.generalszh.presentation.objects.components.wave_guide_effects;
import std;

import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A flood wave's client effects (WaveGuideUpdate's particle systems riding on it), as a side table on the wave's own
// entity: each system's id in the particle world and where it sits in the wave's frame (unscaled, unturned), and
// whether it is one of its front's sprays (doShapeEffects lowers those to the ground under them); `built` once its
// sprays are made (initWaveGuide). WaveGuideCuesPlayed: the last simulation tick whose cues (WaveGuideCues) were played,
// so a tick's are played once however many frames show it.
export namespace generalszh::presentation
{
struct WaveGuideEffects
{
	std::vector<std::uint64_t> systems;
	std::vector<std::array<float, 3>> local;
	std::vector<std::uint8_t> spray;
	std::uint8_t built{0};
};

struct WaveGuideCuesPlayed
{
	std::uint64_t tick{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::WaveGuideEffects>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_effects";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};

template<>
struct ResourceTraits<generalszh::presentation::WaveGuideCuesPlayed>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_cues_played";
};
}
