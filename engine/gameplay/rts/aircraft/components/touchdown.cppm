export module engine.gameplay.rts.aircraft.components.touchdown;
import std;

import engine.ecs.core.component_registry;

// The landing on which a jet last touched down (JetTakeoffOrLandingState::m_landingSoundPlayed): the tick that landing
// began (its Jet::since); a jet whose landing began on another tick has not touched down on it yet. Added the first
// time it touches down.
export namespace engine::gameplay
{
struct Touchdown
{
	std::uint64_t landing{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Touchdown>
{
	static constexpr std::string_view StableName = "engine.gameplay.touchdown";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Touchdown &value, StateHasher &hasher) noexcept { hasher.AppendU64(value.landing); }
};
}
