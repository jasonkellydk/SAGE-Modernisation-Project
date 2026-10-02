export module games.generalszh.content.upgrades.upgrade_content;
import std;

export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
export import engine.gameplay.common.health.algorithms.max_health;
export import games.generalszh.content.objects.object_definition;
import games.generalszh.content.healing.healing_content;

// Zero Hour's upgrades: "Upgrade" blocks (the original's UpgradeCenter, one
// bit each in the order they are defined, after the three veterancy
// "upgrades" UpgradeCenter::init makes first), and each object's upgrade
// triggers (every module with the UpgradeMux fields: TriggeredBy,
// ConflictsWith, RemovesUpgrades, RequiresAllTriggers) with what they do.
export namespace generalszh::content
{
struct UpgradeContent
{
	std::string name;
	bool player{true};            // Type PLAYER (default) or OBJECT
	std::int64_t cost{0};         // BuildCost
	std::uint64_t buildTicks{0};  // BuildTime (seconds)
	std::string displayName;      // DisplayName
	std::string buttonImage;      // ButtonImage
	std::string researchSound;    // ResearchSound
	std::string unitSpecificSound; // UnitSpecificSound
	// AcademyClassify (parseIndexList over TheAcademyClassificationTypeNames: ACT_NONE 0, ACT_UPGRADE_RADAR 1, ACT_SUPERPOWER 2).
	std::uint8_t academyClassification{0};
};

// TheAcademyClassificationTypeNames' index of `name` (ACT_NONE for a name it does not hold).
inline std::uint8_t AcademyClassification(std::string_view name) noexcept
{
	constexpr std::array<std::string_view, 3> names{"ACT_NONE", "ACT_UPGRADE_RADAR", "ACT_SUPERPOWER"};
	for (std::uint8_t index = 0; index < names.size(); ++index)
		if (names[index] == name)
			return index;
	return 0;
}

struct UpgradeCatalog
{
	std::vector<UpgradeContent> upgrades; // by bit
	std::map<std::string, std::uint32_t, std::less<>> byName;

	std::optional<std::uint32_t> Find(std::string_view name) const
	{
		if (const auto found = byName.find(name); found != byName.end())
			return found->second;
		return std::nullopt;
	}
};

namespace upgrade_detail
{
inline bool SameText(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index)
	{
		const char x = a[index] >= 'a' && a[index] <= 'z' ? static_cast<char>(a[index] - 32) : a[index];
		const char y = b[index] >= 'a' && b[index] <= 'z' ? static_cast<char>(b[index] - 32) : b[index];
		if (x != y)
			return false;
	}
	return true;
}
}

inline UpgradeCatalog BuildUpgradeCatalog(const engine::config::Document &document, std::uint64_t ticksPerSecond)
{
	UpgradeCatalog catalog;
	const auto define = [&](std::string name) -> UpgradeContent & {
		if (const auto found = catalog.byName.find(name); found != catalog.byName.end())
			return catalog.upgrades[found->second];
		catalog.byName.emplace(name, static_cast<std::uint32_t>(catalog.upgrades.size()));
		UpgradeContent &upgrade = catalog.upgrades.emplace_back();
		upgrade.name = std::move(name);
		return upgrade;
	};
	// UpgradeCenter::init: veterancy "upgrades" are per object and never built.
	for (const char *level : {"Upgrade_Veterancy_VETERAN", "Upgrade_Veterancy_ELITE", "Upgrade_Veterancy_HEROIC"})
		define(level).player = false;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Upgrade" || root.values.empty())
			continue;
		UpgradeContent &upgrade = define(std::string(root.Value()));
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "Type")
				upgrade.player = !upgrade_detail::SameText(field.Value(), "OBJECT");
			else if (field.key == "BuildCost")
				upgrade.cost = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{}).Floor();
			else if (field.key == "BuildTime")
			{
				const auto seconds = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{});
				upgrade.buildTicks = BuildFrames(seconds, static_cast<std::uint64_t>(ticksPerSecond)); // UpgradeTemplate::calcTimeToBuild
			}
			else if (field.key == "DisplayName")
				upgrade.displayName = std::string(field.Value());
			else if (field.key == "ButtonImage")
				upgrade.buttonImage = std::string(field.Value());
			else if (field.key == "ResearchSound")
				upgrade.researchSound = std::string(field.Value());
			else if (field.key == "UnitSpecificSound")
				upgrade.unitSpecificSound = std::string(field.Value());
			else if (field.key == "AcademyClassify")
				upgrade.academyClassification = AcademyClassification(field.Value());
		}
	}
	return catalog;
}

// What an upgrade trigger does (its module's upgradeImplementation); Other: a module not ported yet
// (the trigger still goes once, as the original's).
enum class UpgradeEffectKind : std::uint32_t
{
	Other,
	MaxHealth,      // MaxHealthUpgrade: AddMaxHealth, ChangeType
	PassengersFire, // PassengersFireUpgrade: its contain lets passengers fire
	Stealth,        // StealthUpgrade: it may stealth
	Armor,          // ArmorUpgrade: its PLAYER_UPGRADE armor set
	WeaponSet,      // WeaponSetUpgrade: its PLAYER_UPGRADE weapon set
	ObjectCreation, // ObjectCreationUpgrade: runs UpgradeObject (an object creation list) from it
	WeaponBonus,    // WeaponBonusUpgrade: WEAPONBONUSCONDITION_PLAYER_UPGRADE
	ExperienceScalar, // ExperienceScalarUpgrade: AddXPScalar onto its experience scalar
	PowerPlant,     // PowerPlantUpgrade: its EnergyBonus joins its production (control rods)
	Radar,          // RadarUpgrade: its radar dish extends (RadarUpdate::extendRadar)
	CommandSet,     // CommandSetUpgrade: its command set becomes CommandSet (CommandSetAlt once TriggerAlt is had)
	UnpausePower,   // UnpauseSpecialPowerUpgrade: its module for SpecialPowerTemplate unpauses once
	ModelCondition, // ModelConditionUpgrade: its ConditionFlag model condition is set
	LocomotorSet,   // LocomotorSetUpgrade: it moves on its SET_NORMAL_UPGRADED locomotor
	GrantScience,   // GrantScienceUpgrade: its player is granted GrantScience
	Minefield,      // GenerateMinefieldBehavior (an upgrade mux): it lays its minefield
	SubObjects,     // SubObjectsUpgrade: its ShowSubObjects / HideSubObjects (override set `ordinal`) over its model
	SpyVision,      // SpyVisionUpdate (an upgrade mux): its `ordinal`-th spy vision turns on for SelfPoweredDuration
	Countermeasures, // CountermeasuresBehavior (an upgrade mux): its flares are ready to use
	CostModifier,   // CostModifierUpgrade: its player's builds of EffectKindOf cost Percentage more (CostToBuild)
	ReplaceObject,  // ReplaceObjectUpgrade: it is deleted and ReplaceObject made in its place, as if just built
	AutoHeal,       // AutoHealBehavior (an upgrade mux): it heals from its next update (`ordinal`: its area program, or with SelfHeal its self program)
};

struct UpgradeEffectContent
{
	UpgradeEffectKind kind{UpgradeEffectKind::Other};
	std::string module;
	Engine::Math::Fixed amount;
	engine::gameplay::MaxHealthChange change{engine::gameplay::MaxHealthChange::SameCurrent};
	std::string armor;                  // the PLAYER_UPGRADE armor set's armor
	std::array<std::string, 3> weapons; // the PLAYER_UPGRADE weapon set's weapons (empty: none in that slot)
	std::string creation;               // the object creation list it runs
	std::string commandSet;             // CommandSetUpgrade: CommandSet, and CommandSetAlt once TriggerAlt is had
	std::string commandSetAlt;
	std::optional<std::uint32_t> triggerAlt;
	std::string power;                  // UnpauseSpecialPowerUpgrade: SpecialPowerTemplate
	std::string condition;              // ModelConditionUpgrade: ConditionFlag
	std::string science;                // GrantScienceUpgrade: GrantScience
	bool disableProof{false};           // RadarUpgrade: DisableProof (radar kept short of power)
	std::uint32_t ordinal{0};           // SubObjectsUpgrade: which of the object's SubObjectsUpgrade modules (ReadPartOverrideSets)
	static constexpr std::uint32_t SelfHeal = 0x80000000u; // AutoHeal: set on the ordinal of a self heal (its n-th self program)
	KindOfMask kinds{};                 // CostModifierUpgrade: EffectKindOf
	std::int64_t share{0};              // CostModifierUpgrade: Percentage, in hundredths of a percent
	std::string replacement;            // ReplaceObjectUpgrade: ReplaceObject
};

struct ObjectUpgradeContent
{
	engine::gameplay::UpgradeTrigger trigger;
	UpgradeEffectContent effect;
};

// An object's upgrade triggers, in its modules' order. Unknown upgrade names are left out of the masks.
inline std::vector<ObjectUpgradeContent> ReadObjectUpgrades(const ObjectDefinition &object, const UpgradeCatalog &catalog)
{
	using namespace upgrade_detail;
	std::vector<ObjectUpgradeContent> out;
	std::uint32_t partSets = 0;
	const auto mask = [&](const engine::config::Node *node) {
		engine::gameplay::UpgradeMask bits;
		if (node != nullptr)
			for (const std::string_view name : node->values)
				if (const auto bit = catalog.Find(name))
					bits.Set(*bit);
		return bits;
	};
	// The object's set with exactly PLAYER_UPGRADE conditions.
	const auto upgradedSet = [](const std::vector<const engine::config::Node *> &sets) -> const engine::config::Node * {
		for (const engine::config::Node *set : sets)
			if (const engine::config::Node *conditions = set->Find("Conditions");
				conditions != nullptr && conditions->values.size() == 1 && SameText(conditions->Value(), "PLAYER_UPGRADE"))
				return set;
		return nullptr;
	};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		const engine::config::Node *by = module.block->Find("TriggeredBy");
		if (by == nullptr)
			continue;
		ObjectUpgradeContent entry;
		entry.trigger.activation = mask(by);
		entry.trigger.conflicting = mask(module.block->Find("ConflictsWith"));
		entry.trigger.removal = mask(module.block->Find("RemovesUpgrades"));
		if (const engine::config::Node *all = module.block->Find("RequiresAllTriggers"))
			entry.trigger.requiresAll = engine::config::values::ParseBool(all->Value()).value_or(false);
		UpgradeEffectContent &effect = entry.effect;
		effect.module = module.type;
		const std::string_view type = module.type;
		if (type == "MaxHealthUpgrade")
		{
			effect.kind = UpgradeEffectKind::MaxHealth;
			if (const engine::config::Node *add = module.block->Find("AddMaxHealth"))
				effect.amount = engine::config::values::ParseFixed(add->Value()).value_or(Engine::Math::Fixed{});
			if (const engine::config::Node *change = module.block->Find("ChangeType"))
			{
				constexpr std::array<std::string_view, 4> names{"SAME_CURRENTHEALTH", "PRESERVE_RATIO", "ADD_CURRENT_HEALTH_TOO", "FULLY_HEAL"};
				for (std::size_t index = 0; index < names.size(); ++index)
					if (SameText(change->Value(), names[index]))
						effect.change = static_cast<engine::gameplay::MaxHealthChange>(index);
			}
		}
		else if (type == "PassengersFireUpgrade")
			effect.kind = UpgradeEffectKind::PassengersFire;
		else if (type == "PowerPlantUpgrade")
			effect.kind = UpgradeEffectKind::PowerPlant;
		else if (type == "RadarUpgrade")
		{
			effect.kind = UpgradeEffectKind::Radar;
			if (const engine::config::Node *proof = module.block->Find("DisableProof"))
				effect.disableProof = engine::config::values::ParseBool(proof->Value()).value_or(false);
		}
		else if (type == "CommandSetUpgrade")
		{
			effect.kind = UpgradeEffectKind::CommandSet;
			if (const engine::config::Node *set = module.block->Find("CommandSet"))
				effect.commandSet = std::string(set->Value());
			if (const engine::config::Node *alt = module.block->Find("CommandSetAlt"))
				effect.commandSetAlt = std::string(alt->Value());
			if (const engine::config::Node *trigger = module.block->Find("TriggerAlt"))
				effect.triggerAlt = catalog.Find(trigger->Value());
		}
		else if (type == "WeaponBonusUpgrade")
			effect.kind = UpgradeEffectKind::WeaponBonus;
		else if (type == "ExperienceScalarUpgrade")
		{
			effect.kind = UpgradeEffectKind::ExperienceScalar;
			if (const engine::config::Node *add = module.block->Find("AddXPScalar"))
				effect.amount = engine::config::values::ParseFixed(add->Value()).value_or(Engine::Math::Fixed{});
		}
		else if (type == "StealthUpgrade")
			effect.kind = UpgradeEffectKind::Stealth;
		else if (type == "GrantScienceUpgrade")
		{
			effect.kind = UpgradeEffectKind::GrantScience;
			if (const engine::config::Node *science = module.block->Find("GrantScience"); science != nullptr && !science->values.empty())
				effect.science = std::string(science->Value());
		}
		else if (type == "GenerateMinefieldBehavior")
			effect.kind = UpgradeEffectKind::Minefield;
		else if (type == "CountermeasuresBehavior")
			effect.kind = UpgradeEffectKind::Countermeasures;
		else if (type == "AutoHealBehavior")
		{
			// AutoHealBehavior::upgradeImplementation: wakes now. Which of its heals: itself, or its n-th area program
			// (ReadObjectHealing's order).
			effect.kind = UpgradeEffectKind::AutoHeal;
			const bool others = AutoHealsOthers(*module.block);
			std::uint32_t ordinal = 0;
			for (const ModuleEntry &other : object.modules)
			{
				if (&other == &module)
					break;
				if (other.slot == ModuleSlot::Behavior && other.block != nullptr && other.type == "AutoHealBehavior" && AutoHealsOthers(*other.block) == others)
					++ordinal;
			}
			effect.ordinal = others ? ordinal : (ordinal | UpgradeEffectContent::SelfHeal);
		}
		else if (type == "ReplaceObjectUpgrade")
		{
			effect.kind = UpgradeEffectKind::ReplaceObject;
			if (const engine::config::Node *name = module.block->Find("ReplaceObject"); name != nullptr && !name->values.empty())
				effect.replacement = std::string(name->Value());
		}
		else if (type == "CostModifierUpgrade")
		{
			effect.kind = UpgradeEffectKind::CostModifier;
			if (const engine::config::Node *kinds = module.block->Find("EffectKindOf"))
				for (const std::string_view name : kinds->values)
					if (const std::size_t bit = KindOfBit(name); bit < KindOfNames.size())
						effect.kinds[bit / 64] |= std::uint64_t{1} << (bit % 64);
			// parsePercentToReal: "-10%" (or -10) is -0.1.
			if (const engine::config::Node *percent = module.block->Find("Percentage"))
			{
				std::string_view text = percent->Value();
				if (!text.empty() && text.back() == '%')
					text.remove_suffix(1);
				const Engine::Math::Fixed value = engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{}) * Engine::Math::Fixed::FromInt(100);
				effect.share = (value + Engine::Math::Fixed::FromRatio(1, 2)).Floor();
			}
		}
		else if (type == "SpyVisionUpdate")
		{
			// SpyVisionUpdate::upgradeImplementation (NeedsUpgrade): which of the object's SpyVisionUpdate modules.
			effect.kind = UpgradeEffectKind::SpyVision;
			std::uint32_t ordinal = 0;
			for (const ModuleEntry &other : object.modules)
			{
				if (&other == &module)
					break;
				if (other.type == "SpyVisionUpdate")
					++ordinal;
			}
			effect.ordinal = ordinal;
		}
		else if (type == "SubObjectsUpgrade")
		{
			effect.kind = UpgradeEffectKind::SubObjects;
			effect.ordinal = partSets++;
		}
		else if (type == "LocomotorSetUpgrade")
			effect.kind = UpgradeEffectKind::LocomotorSet;
		else if (type == "ModelConditionUpgrade")
		{
			effect.kind = UpgradeEffectKind::ModelCondition;
			if (const engine::config::Node *flag = module.block->Find("ConditionFlag"); flag != nullptr && !flag->values.empty())
				effect.condition = std::string(flag->Value());
		}
		else if (type == "UnpauseSpecialPowerUpgrade")
		{
			effect.kind = UpgradeEffectKind::UnpausePower;
			if (const engine::config::Node *power = module.block->Find("SpecialPowerTemplate"); power != nullptr && !power->values.empty())
				effect.power = std::string(power->Value());
		}
		else if (type == "ObjectCreationUpgrade")
		{
			effect.kind = UpgradeEffectKind::ObjectCreation;
			if (const engine::config::Node *list = module.block->Find("UpgradeObject"))
				effect.creation = std::string(list->Value());
		}
		else if (type == "ArmorUpgrade")
		{
			effect.kind = UpgradeEffectKind::Armor;
			if (const engine::config::Node *set = upgradedSet(object.armorSets))
				if (const engine::config::Node *armor = set->Find("Armor"))
					effect.armor = std::string(armor->Value());
		}
		else if (type == "WeaponSetUpgrade")
		{
			effect.kind = UpgradeEffectKind::WeaponSet;
			if (const engine::config::Node *set = upgradedSet(object.weaponSets))
				for (const engine::config::Node &child : set->children)
					if (SameText(child.key, "Weapon") && child.values.size() >= 2 && !SameText(child.Value(1), "None"))
					{
						const std::string_view slot = child.Value(0);
						const std::size_t index = SameText(slot, "PRIMARY") ? 0 : SameText(slot, "SECONDARY") ? 1 : SameText(slot, "TERTIARY") ? 2 : 3;
						if (index < 3)
							effect.weapons[index] = std::string(child.Value(1));
					}
		}
		out.push_back(std::move(entry));
	}
	return out;
}
// An object's SubObjectsUpgrade modules (those with TriggeredBy, as ReadObjectUpgrades counts them), in module order:
// each its sub-objects to show then to hide (ShowSubObjects, HideSubObjects: names appended), as (name, shown) pairs.
inline std::vector<std::vector<std::pair<std::string, bool>>> ReadPartOverrideSets(const ObjectDefinition &object)
{
	std::vector<std::vector<std::pair<std::string, bool>>> sets;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "SubObjectsUpgrade" || module.block->Find("TriggeredBy") == nullptr)
			continue;
		std::vector<std::pair<std::string, bool>> set;
		for (const bool show : {true, false})
			for (const engine::config::Node &child : module.block->children)
				if (child.key == (show ? "ShowSubObjects" : "HideSubObjects"))
					for (const std::string_view name : child.values)
						set.emplace_back(std::string(name), show);
		sets.push_back(std::move(set));
	}
	return sets;
}
}
