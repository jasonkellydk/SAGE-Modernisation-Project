export module games.generalszh.gameplay.combat.components.cooldown_creation;
import std;

export import engine.ecs.core.component_registry;

// The state of an object's FireOCLAfterWeaponCooldownUpdate modules (up to four, in module order): whether it was
// watching its slot firing (m_valid), the shots in a row it has seen and the tick the first came (m_startFrame).
export namespace generalszh::gameplay
{
struct CooldownCreationState
{
	std::uint64_t startTick{0};
	std::uint32_t shots{0};
	std::uint8_t valid{0};
	std::uint8_t reserved[3]{};
};

struct CooldownCreations
{
	std::array<CooldownCreationState, 4> modules{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::CooldownCreations>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cooldown_creations";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
