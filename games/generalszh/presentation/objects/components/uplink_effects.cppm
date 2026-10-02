export module games.generalszh.presentation.objects.components.uplink_effects;
import std;

import engine.ecs.core.component_registry;

// A Particle Cannon uplink's client effects (ParticleUplinkCannonUpdate's), as a side table on the uplink's own
// entity. Once a tick, following the simulation's status changes in order:
//   its logical status and orbital beam as last followed; the status its client effects are for (setClientStatus),
//   bumped on each call, made on a reveal or not, and the logic frames since; hidden while its viewer cannot see it
//   clearly (removeAllEffects each frame shrouded); whether it was shrouded last frame; which of its four sound loops
//   should play, each start bumped (addAudioEvent), and where the ground annihilation loop sounds (the beam's drawable).
// Each drawn frame, as the effects stand: the client status they were made for; the particle systems made for it
// (outer node, connector and laser base flares); the bone places, cached as the original caches them (the outer nodes
// the first time any effects are made, the connector and fire bones the first time connector lasers are); how long
// the lasers made for it and the orbital beam have shown (their textures scroll); the orbital beam's own LaserUpdate
// particle systems.
export namespace generalszh::presentation
{
struct UplinkEffects
{
	static constexpr std::uint32_t Invalid = 2; // a cache the model could not fill (m_invalidSettings)
	// Once a tick.
	std::uint8_t logical{0}; // CannonStatus
	std::uint8_t beam{0};    // the orbital beam shows (born, not yet gone)
	std::uint8_t client{0};  // CannonStatus
	std::uint8_t reveal{0};
	std::uint8_t hidden{0};
	std::uint8_t shroudedLast{0};
	std::uint32_t clientSerial{0};
	std::uint32_t clientAge{0}; // logic frames since the client status was set
	std::array<std::uint8_t, 4> want{};
	std::array<std::uint32_t, 4> started{};
	std::array<float, 3> annihilationAt{};
	// Each drawn frame.
	std::uint32_t builtSerial{0};
	std::uint8_t built{0};
	std::uint8_t outerCached{0};
	std::uint8_t upCached{0};
	std::vector<std::uint64_t> systems;
	std::vector<std::array<float, 3>> outer;
	std::vector<float> outerYaw;
	std::array<float, 3> connector{};
	std::array<float, 3> origin{};
	float age{0.0f};
	float orbitAge{0.0f};
	std::array<std::uint64_t, 2> orbitSystems{};
	std::uint32_t seenFrame{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::UplinkEffects>
{
	static constexpr std::string_view StableName = "generalszh.presentation.uplink_effects";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
