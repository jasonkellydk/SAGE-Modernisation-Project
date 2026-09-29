export module games.generalszh.gameplay.combat.resources.cooldown_creation_catalog;
import std;

export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;

// Each kind of object's FireOCLAfterWeaponCooldownUpdate modules (definition data, indexed by DefinitionRef): the slot
// watched, its creation list (a DeathEffectKind::Objects id), the shots it takes, the lifetime per second of firing
// (ms) and its cap (ticks), and its upgrade conditions. Immutable.
export namespace generalszh::gameplay
{
struct CooldownCreationConfig
{
	std::uint32_t slot{0};
	std::uint32_t creation{0xFFFFFFFFu};
	std::uint32_t minShots{1};
	std::uint32_t lifetimePerSecond{1000};
	std::uint64_t maxTicks{1000};
	engine::gameplay::UpgradeMask activation;
	engine::gameplay::UpgradeMask conflicting;
	bool requiresAll{false};

	// UpgradeMux::testUpgradeConditions: nothing it conflicts with, and (with triggers) any, or with
	// RequiresAllTriggers every, trigger.
	bool Active(const engine::gameplay::UpgradeMask &key) const noexcept
	{
		if (key.AnyOf(conflicting))
			return false;
		if (!activation.Any())
			return true;
		return requiresAll ? key.AllOf(activation) : key.AnyOf(activation);
	}
};

struct CooldownCreationCatalog
{
	static constexpr std::size_t MaxModules = 4;
	std::vector<std::vector<CooldownCreationConfig>> byDefinition;

	std::span<const CooldownCreationConfig> Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() ? std::span<const CooldownCreationConfig>(byDefinition[definition]) : std::span<const CooldownCreationConfig>{};
	}
};
}
