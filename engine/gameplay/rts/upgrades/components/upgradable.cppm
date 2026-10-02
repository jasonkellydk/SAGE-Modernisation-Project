export module engine.gameplay.rts.upgrades.components.upgradable;
import std;

export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
export import engine.ecs.core.component_registry;

// An object with upgrade triggers: its own completed upgrades (the original's
// Object::m_objectUpgradesCompleted), which of its triggers have gone (each
// UpgradeMux's m_upgradeExecuted, by trigger index), and when it last looked
// at them: its owner and the owner's grant count then, and whether it must
// look again anyway (it was just made, or given an upgrade of its own).
export namespace engine::gameplay
{
struct Upgradable
{
	UpgradeMask completed;
	std::uint32_t executed{0};
	std::uint32_t seenGrants{0};
	std::uint32_t seenPlayer{0};
	std::uint32_t stale{1};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Upgradable>
{
	static constexpr std::string_view StableName = "engine.gameplay.upgradable";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Upgradable &value, StateHasher &hasher) noexcept
	{
		for (const std::uint64_t word : value.completed.bits)
			hasher.AppendU64(word);
		hasher.AppendU64((static_cast<std::uint64_t>(value.executed) << 32) | value.seenGrants);
		hasher.AppendU64((static_cast<std::uint64_t>(value.seenPlayer) << 32) | value.stale);
	}
};
}
