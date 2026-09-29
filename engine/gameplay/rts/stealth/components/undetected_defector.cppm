export module engine.gameplay.rts.stealth.components.undetected_defector;
import std;

export import engine.ecs.core.component_registry;

// An undetected defector (the original's Object::goInvulnerable / ObjectDefectionHelper): until `until`, everyone
// treats it as their own (never picked as a target) and it treats everyone as neutral (it looks for no one), so its AI
// does not give it away. Its cover is blown when the time is up, it fires, or it dies.
export namespace engine::gameplay
{
struct UndetectedDefector
{
	std::uint64_t until{0}; // the tick its cover ends
	std::uint8_t fx{0};     // it shows (m_doDefectorFX: a defector; not one merely invulnerable a while)
	std::uint8_t reserved[7]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::UndetectedDefector>
{
	static constexpr std::string_view StableName = "engine.gameplay.undetected_defector";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
