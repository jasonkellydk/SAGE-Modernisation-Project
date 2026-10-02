export module engine.gameplay.rts.radar.components.radar_provider;
import std;

export import engine.ecs.core.component_registry;

// An object giving its player radar (RadarUpgrade, once upgraded: Player::addRadar), and whether it keeps doing so while
// its player is short of power (DisableProof). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct RadarProvider
{
	std::uint8_t disableProof{0};
	std::array<std::uint8_t, 3> reserved{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::RadarProvider>
{
	static constexpr std::string_view StableName = "engine.gameplay.radar_provider";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
