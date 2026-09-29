export module games.generalszh.content.crates.crate_content;
import std;

export import games.generalszh.content.objects.object_definition;
import engine.config.binding.values;

// Crates as content:
//   CrateData (Crate.ini): the chance one is made when something dies
//   (CreationChance), what must be true of the dead (VeterancyLevel) and its
//   killer (KilledByType: every kind; KillerScience), whether it belongs to
//   the dead one's player (OwnedByMaker), and which crate object it is
//   (CrateObject = name chance: the first whose running total passes a roll);
//   a crate object's CrateCollide module (the kind: SalvageCrateCollide,
//   MoneyCrateCollide, VeterancyCrateCollide, ...): who may pick it up
//   (RequiredKindOf, ForbiddenKindOf, ForbidOwnerPlayer, HumanOnly,
//   PickupScience, BuildingPickup), what plays (ExecuteFX), and a salvage
//   crate's chances (WeaponChance, LevelChance, MoneyChance) and money
//   (MinMoney..MaxMoney); a money crate's MoneyProvided and the more a
//   player's upgrade adds to it (UpgradedBoost = UpgradeType:name Boost:n).
export namespace generalszh::content
{
struct CrateTemplate
{
	Engine::Math::Fixed creationChance;
	std::optional<std::uint32_t> veterancyLevel; // REGULAR 0 .. HEROIC 3
	KindOfMask killedByKinds{};
	std::string killerScience;
	bool ownedByMaker{false};
	std::vector<std::pair<std::string, Engine::Math::Fixed>> crates;
};

using CrateTemplates = std::map<std::string, CrateTemplate, std::less<>>;

enum class CrateKind : std::uint8_t
{
	Other,
	Salvage,
	Money,
	Veterancy,
	Unit,
	CarBomb, // ConvertToCarBombCrateCollide: the terrorist is the crate, the vehicle it enters collects it
	Hijack,  // ConvertToHijackedVehicleCrateCollide: the hijacker is the crate, the vehicle it touches collects it
};

struct CrateCollideContent
{
	CrateKind kind{CrateKind::Other};
	KindOfMask required{};
	KindOfMask forbidden{};
	bool forbidOwner{false};
	bool humanOnly{false};
	bool buildingPickup{false};
	std::string pickupScience;
	std::string executeFX;
	std::string fxList; // ConvertToCarBombCrateCollide's FXList, on the vehicle
	// Its world animation where it was picked up (ExecuteAnimation, ExecuteAnimationTime s, ExecuteAnimationZRise a
	// second, ExecuteAnimationFades).
	std::string executeAnimation;
	Engine::Math::Fixed executeAnimationSeconds;
	Engine::Math::Fixed executeAnimationRise;
	bool executeAnimationFades{true};
	Engine::Math::Fixed weaponChance{Engine::Math::Fixed::One()};
	Engine::Math::Fixed levelChance{Engine::Math::Fixed::FromRatio(1, 4)};
	Engine::Math::Fixed moneyChance{Engine::Math::Fixed::FromRatio(3, 4)};
	std::int64_t minMoney{25};
	std::int64_t maxMoney{75};
	std::int64_t moneyProvided{0};
	std::vector<std::pair<std::string, std::int64_t>> upgradeBoosts;
	// VeterancyCrateCollide: its own levels (AddsOwnerVeterancy, else one), within EffectRange of what it touches (0:
	// only that), and IsPilot (the vehicle must be its player's and not flying).
	bool addsOwnerVeterancy{false};
	bool isPilot{false};
	Engine::Math::Fixed effectRange;
	// UnitCrateCollide: UnitCount of UnitName for the collector.
	std::string unitName;
	std::uint32_t unitCount{0};
};

// PilotFindVehicleUpdate: a computer player's idle pilot looks every ScanRate within ScanRange for its own vehicle at
// least MinHealth healthy to climb into.
struct PilotFindVehicleContent
{
	std::uint64_t scanTicks{0};
	Engine::Math::Fixed range;
	Engine::Math::Fixed minHealth;
};

inline std::optional<PilotFindVehicleContent> ReadPilotFindVehicle(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "PilotFindVehicleUpdate")
			continue;
		PilotFindVehicleContent pilot;
		const auto fixed = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
		};
		// INI::parseDurationUnsignedInt: milliseconds to frames, rounded up.
		pilot.scanTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(0, (fixed("ScanRate") * Engine::Math::Fixed::FromInt(30) / Engine::Math::Fixed::FromInt(1000)).Ceil()));
		pilot.range = fixed("ScanRange");
		pilot.minHealth = fixed("MinHealth");
		return pilot;
	}
	return std::nullopt;
}

namespace crate_detail
{
inline KindOfMask Kinds(const engine::config::Node *node)
{
	KindOfMask mask{};
	if (node != nullptr)
		for (const std::string_view name : node->values)
			if (const std::size_t bit = KindOfBit(name); bit < KindOfNames.size())
				mask[bit / 64] |= std::uint64_t{1} << (bit % 64);
	return mask;
}

inline Engine::Math::Fixed Share(std::string_view text, Engine::Math::Fixed fallback)
{
	const bool percent = !text.empty() && text.back() == '%';
	if (percent)
		text.remove_suffix(1);
	const auto value = engine::config::values::ParseFixed(text);
	if (!value)
		return fallback;
	return percent ? *value / Engine::Math::Fixed::FromInt(100) : *value;
}
}

inline CrateTemplates BindCrateTemplates(const engine::config::Document &document)
{
	using namespace crate_detail;
	CrateTemplates templates;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "CrateData" || root.values.empty())
			continue;
		CrateTemplate &crate = templates[std::string(root.Value())];
		crate = {};
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "CreationChance")
				crate.creationChance = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{});
			else if (field.key == "VeterancyLevel")
			{
				constexpr std::array<std::string_view, 4> levels{"REGULAR", "VETERAN", "ELITE", "HEROIC"};
				for (std::uint32_t level = 0; level < levels.size(); ++level)
					if (field.Value() == levels[level])
						crate.veterancyLevel = level;
			}
			else if (field.key == "KilledByType")
				crate.killedByKinds = Kinds(&field);
			else if (field.key == "KillerScience")
				crate.killerScience = std::string(field.Value());
			else if (field.key == "OwnedByMaker")
				crate.ownedByMaker = engine::config::values::ParseBool(field.Value()).value_or(false);
			else if (field.key == "CrateObject" && field.values.size() >= 2)
				crate.crates.emplace_back(std::string(field.Value(0)), engine::config::values::ParseFixed(field.Value(1)).value_or(Engine::Math::Fixed{}));
		}
	}
	return templates;
}

inline std::optional<CrateCollideContent> ReadCrateCollide(const ObjectDefinition &object)
{
	using namespace crate_detail;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || !std::string_view(module.type).ends_with("CrateCollide"))
			continue;
		const engine::config::Node &block = *module.block;
		CrateCollideContent crate;
		crate.kind = module.type == "SalvageCrateCollide" ? CrateKind::Salvage : module.type == "MoneyCrateCollide" ? CrateKind::Money
			: module.type == "VeterancyCrateCollide" ? CrateKind::Veterancy : module.type == "UnitCrateCollide" ? CrateKind::Unit
			: module.type == "ConvertToCarBombCrateCollide" ? CrateKind::CarBomb
			: module.type == "ConvertToHijackedVehicleCrateCollide" ? CrateKind::Hijack : CrateKind::Other;
		crate.required = Kinds(block.Find("RequiredKindOf"));
		crate.forbidden = Kinds(block.Find("ForbiddenKindOf"));
		const auto flag = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
		};
		crate.forbidOwner = flag("ForbidOwnerPlayer");
		crate.humanOnly = flag("HumanOnly");
		crate.buildingPickup = flag("BuildingPickup");
		if (const auto *node = block.Find("PickupScience"))
			crate.pickupScience = std::string(node->Value());
		if (const auto *node = block.Find("ExecuteFX"); node != nullptr && node->Value() != "None")
			crate.executeFX = std::string(node->Value());
		if (const auto *node = block.Find("FXList"); node != nullptr && node->Value() != "None")
			crate.fxList = std::string(node->Value());
		if (const auto *node = block.Find("ExecuteAnimation"); node != nullptr && node->Value() != "None")
			crate.executeAnimation = std::string(node->Value());
		if (const auto *node = block.Find("ExecuteAnimationTime"))
			crate.executeAnimationSeconds = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
		if (const auto *node = block.Find("ExecuteAnimationZRise"))
			crate.executeAnimationRise = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
		if (const auto *node = block.Find("ExecuteAnimationFades"))
			crate.executeAnimationFades = engine::config::values::ParseBool(node->Value()).value_or(true);
		if (const auto *node = block.Find("WeaponChance"))
			crate.weaponChance = Share(node->Value(), crate.weaponChance);
		if (const auto *node = block.Find("LevelChance"))
			crate.levelChance = Share(node->Value(), crate.levelChance);
		if (const auto *node = block.Find("MoneyChance"))
			crate.moneyChance = Share(node->Value(), crate.moneyChance);
		const auto integer = [&](std::string_view key, std::int64_t fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseInt(node->Value()).value_or(fallback) : fallback;
		};
		crate.minMoney = integer("MinMoney", crate.minMoney);
		crate.maxMoney = integer("MaxMoney", crate.maxMoney);
		crate.moneyProvided = integer("MoneyProvided", 0);
		if (const auto *node = block.Find("UnitName"))
			crate.unitName = std::string(node->Value());
		crate.unitCount = static_cast<std::uint32_t>(std::max<std::int64_t>(integer("UnitCount", 0), 0));
		crate.addsOwnerVeterancy = flag("AddsOwnerVeterancy");
		crate.isPilot = flag("IsPilot");
		if (const auto *node = block.Find("EffectRange"))
			crate.effectRange = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
		for (const engine::config::Node &field : block.children)
		{
			if (field.key != "UpgradedBoost")
				continue;
			std::string upgrade;
			std::int64_t boost = 0;
			for (const std::string_view token : field.values)
			{
				if (token.starts_with("UpgradeType:"))
					upgrade = std::string(token.substr(12));
				else if (token.starts_with("Boost:"))
					boost = engine::config::values::ParseInt(token.substr(6)).value_or(0);
			}
			if (!upgrade.empty())
				crate.upgradeBoosts.emplace_back(std::move(upgrade), boost);
		}
		return crate;
	}
	return std::nullopt;
}
}
