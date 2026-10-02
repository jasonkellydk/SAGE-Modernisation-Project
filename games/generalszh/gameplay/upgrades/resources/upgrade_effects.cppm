export module games.generalszh.gameplay.upgrades.resources.upgrade_effects;
import std;

export import games.generalszh.content.upgrades.upgrade_content;
export import engine.ecs.core.entity;
export import engine.gameplay.rts.movement.definitions.locomotor;
import engine.ecs.system.system;

// What each kind of object's upgrade triggers do, parallel to the engine's
// UpgradeTriggers (definition data, indexed by DefinitionRef, then trigger):
// the effect's kind and its values, with armors and weapons as catalog
// indices. A trigger's reaction is its effect's kind.
export namespace generalszh::gameplay
{
struct UpgradeEffect
{
	content::UpgradeEffectKind kind{content::UpgradeEffectKind::Other};
	Engine::Math::Fixed amount;
	engine::gameplay::MaxHealthChange change{engine::gameplay::MaxHealthChange::SameCurrent};
	std::uint32_t armor{0xFFFFFFFFu};                                  // none: keep its armor
	std::array<std::uint32_t, 3> weapons{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}; // none: an empty slot
	bool hasWeapons{false};
	std::uint32_t creation{0xFFFFFFFFu}; // its object creation list (a DeathEffectKind::Objects id; none: 0xFFFFFFFF)
	std::uint32_t commandSet{0xFFFFFFFFu};    // CommandSetUpgrade: command set ids (ObjectTemplates::CommandSet)
	std::uint32_t commandSetAlt{0xFFFFFFFFu};
	std::uint32_t triggerAlt{0xFFFFFFFFu};    // the upgrade bit choosing the alternative (none: 0xFFFFFFFF)
	std::uint32_t power{0xFFFFFFFFu};         // UnpauseSpecialPowerUpgrade: its special power template (none: 0xFFFFFFFF)
	bool disableProof{false};                 // RadarUpgrade: DisableProof
	std::uint32_t condition{0xFFFFFFFFu};     // ModelConditionUpgrade: the model condition bit (none: 0xFFFFFFFF)
	std::optional<engine::gameplay::LocomotorDefinition> locomotor; // LocomotorSetUpgrade: its SET_NORMAL_UPGRADED locomotor
	std::uint32_t science{0xFFFFFFFFu};       // GrantScienceUpgrade: the science's bit (none: 0xFFFFFFFF)
	std::uint32_t parts{0};                   // SubObjectsUpgrade: its override set
	engine::gameplay::UpgradeMask conflicting; // its ConflictsWith (SubObjectsUpgrade checks it again as it goes)
	content::KindOfMask kinds{};              // CostModifierUpgrade: EffectKindOf
	std::int64_t share{0};                    // CostModifierUpgrade: Percentage, in hundredths of a percent
	std::uint32_t replacement{0xFFFFFFFFu};   // ReplaceObjectUpgrade: its ReplaceObject (a DeathEffectKind::Spawn id; none: 0xFFFFFFFF)
};

// The tick's object creation lists to run from upgraded objects, and sciences to grant their players (the session
// carries them out after the tick, as it does deaths' lists).
struct UpgradeCreation
{
	ecs::Entity entity;
	std::uint32_t creation{0xFFFFFFFFu};
	std::uint32_t science{0xFFFFFFFFu};
	bool minefield{false}; // GenerateMinefieldBehavior::upgradeImplementation: lay its minefield
	std::uint32_t replacement{0xFFFFFFFFu}; // ReplaceObjectUpgrade::upgradeImplementation: what takes its place
};

struct UpgradeCreations
{
	std::vector<UpgradeCreation> list;
};

struct UpgradeEffects
{
	std::vector<std::vector<UpgradeEffect>> byDefinition;

	const UpgradeEffect *Of(std::uint32_t definition, std::uint32_t trigger) const noexcept
	{
		return definition < byDefinition.size() && trigger < byDefinition[definition].size() ? &byDefinition[definition][trigger] : nullptr;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::UpgradeCreations>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.upgrade_creations";
};

template<>
struct ResourceTraits<generalszh::gameplay::UpgradeEffects>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.upgrade_effects";
};
}
