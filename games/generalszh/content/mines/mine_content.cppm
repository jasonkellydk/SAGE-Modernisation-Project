export module games.generalszh.content.mines.mine_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
export import Engine.Core.Math.Fixed;
export import games.generalszh.content.global.game_data;
import engine.config.binding.values;

// A mine's MinefieldBehavior (MinefieldBehaviorModuleData, defaults as there): DetonationWeapon, DetonatedBy (ENEMIES
// NEUTRAL), StopsRegenAfterCreatorDies (Yes), Regenerates (No), WorkersDetonate (No), CreatorDeathCheckRate (a
// second), ScootFromStartingPointTime (none), NumVirtualMines (1), RepeatDetonateMoveThresh (1),
// DegenPercentPerSecondAfterCreatorDies (in percent points: none) and CreationList.
export namespace generalszh::content
{
struct MinefieldContent
{
	std::string weapon;
	std::uint8_t detonatedBy{2 | 4}; // allies 1, enemies 2, neutral 4
	bool stopsRegen{true};
	bool regenerates{false};
	bool workersDetonate{false};
	std::uint64_t checkRate{30};
	std::uint64_t scootTicks{0};
	std::uint32_t virtualMines{1};
	Engine::Math::Fixed repeatThreshold{Engine::Math::Fixed::One()};
	Engine::Math::Fixed drainPercent;
	std::string creationList;
};

inline std::optional<MinefieldContent> ReadMinefield(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "MinefieldBehavior")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		MinefieldContent mine;
		mine.checkRate = step.TicksPerSecond();
		const auto flag = [&](std::string_view key, bool fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
		};
		const auto text = [&](std::string_view key) -> std::string {
			const auto *node = block.Find(key);
			return node != nullptr && !node->Value().empty() && node->Value() != "None" ? std::string(node->Value()) : std::string{};
		};
		mine.weapon = text("DetonationWeapon");
		mine.creationList = text("CreationList");
		if (const auto *node = block.Find("DetonatedBy"))
		{
			mine.detonatedBy = 0;
			for (const std::string_view relation : node->values)
				mine.detonatedBy |= relation == "ALLIES" ? 1 : relation == "ENEMIES" ? 2 : relation == "NEUTRAL" ? 4 : 0;
		}
		mine.stopsRegen = flag("StopsRegenAfterCreatorDies", true);
		mine.regenerates = flag("Regenerates", false);
		mine.workersDetonate = flag("WorkersDetonate", false);
		if (const auto *node = block.Find("CreatorDeathCheckRate"))
			mine.checkRate = engine::config::ReadDurationTicks(*node, bind).value_or(mine.checkRate);
		if (const auto *node = block.Find("ScootFromStartingPointTime"))
			mine.scootTicks = engine::config::ReadDurationTicks(*node, bind).value_or(0);
		if (const auto *node = block.Find("NumVirtualMines"))
			mine.virtualMines = static_cast<std::uint32_t>(std::max<std::int64_t>(1, engine::config::values::ParseInt(node->Value()).value_or(1)));
		if (const auto *node = block.Find("RepeatDetonateMoveThresh"))
			mine.repeatThreshold = engine::config::values::ParseFixed(node->Value()).value_or(mine.repeatThreshold);
		if (const auto *node = block.Find("DegenPercentPerSecondAfterCreatorDies"))
		{
			std::string_view value = node->Value();
			if (!value.empty() && value.back() == '%')
				value.remove_suffix(1);
			mine.drainPercent = engine::config::values::ParseFixed(value).value_or(Engine::Math::Fixed{});
		}
		return mine;
	}
	return std::nullopt;
}

// GenerateMinefieldBehaviorModuleData (defaults as there): MineName, UpgradedMineName, GenerationFX,
// DistanceAroundObject and MinesPerSquareFoot (GameData's StandardMinefieldDistance / Density), GenerateOnlyOnDeath,
// BorderOnly (Yes), SmartBorder, SmartBorderSkipInterior (Yes), AlwaysCircular, Upgradable, RandomJitter,
// SkipIfThisMuchUnderStructure (33%). (UpgradedTriggeredBy is read and unused: the original always asks for
// Upgrade_ChinaEMPMines.)
struct MinefieldGeneratorContent
{
	std::string mine;
	std::string upgradedMine;
	std::string effect;
	Engine::Math::Fixed distance;
	Engine::Math::Fixed density;
	bool onDeath{false};
	bool borderOnly{true};
	bool smartBorder{false};
	bool smartBorderSkipInterior{true};
	bool alwaysCircular{false};
	bool upgradable{false};
	Engine::Math::Fixed jitter;
	Engine::Math::Fixed underStructure{Engine::Math::Fixed::FromRatio(33, 100)};
};

// DemoTrapUpdateModuleData (defaults as there): DefaultProximityMode, DetonationWeaponSlot / ProximityModeWeaponSlot /
// ManualModeWeaponSlot (PRIMARY 0, SECONDARY 1, TERTIARY 2), TriggerDetonationRange, IgnoreTargetTypes (as target
// classes), ScanRate, AutoDetonationWithFriendsInvolved, DetonationWeapon, DetonateWhenKilled.
struct DemoTrapContent
{
	bool proximityByDefault{false};
	std::uint8_t detonationSlot{0};
	std::uint8_t proximitySlot{0};
	std::uint8_t manualSlot{0};
	Engine::Math::Fixed range;
	std::vector<std::string> ignoreKinds;
	std::uint64_t scanTicks{0};
	bool friendlyDetonation{false};
	std::string weapon;
	bool detonateWhenKilled{false};
};

inline std::uint8_t WeaponSlotOf(std::string_view name) noexcept { return name == "SECONDARY" ? 1 : name == "TERTIARY" ? 2 : 0; }

inline std::optional<DemoTrapContent> ReadDemoTrap(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "DemoTrapUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		DemoTrapContent trap;
		const auto flag = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
		};
		const auto slot = [&](std::string_view key) {
			const auto *node = block.Find(key);
			return node != nullptr ? WeaponSlotOf(node->Value()) : std::uint8_t{0};
		};
		trap.proximityByDefault = flag("DefaultProximityMode");
		trap.detonationSlot = slot("DetonationWeaponSlot");
		trap.proximitySlot = slot("ProximityModeWeaponSlot");
		trap.manualSlot = slot("ManualModeWeaponSlot");
		if (const auto *node = block.Find("TriggerDetonationRange"))
			trap.range = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
		if (const auto *node = block.Find("IgnoreTargetTypes"))
			for (const std::string_view kind : node->values)
				trap.ignoreKinds.emplace_back(kind);
		if (const auto *node = block.Find("ScanRate"))
			trap.scanTicks = engine::config::ReadDurationTicks(*node, bind).value_or(0);
		trap.friendlyDetonation = flag("AutoDetonationWithFriendsInvolved");
		if (const auto *node = block.Find("DetonationWeapon"); node != nullptr && !node->Value().empty() && node->Value() != "None")
			trap.weapon = std::string(node->Value());
		trap.detonateWhenKilled = flag("DetonateWhenKilled");
		return trap;
	}
	return std::nullopt;
}

inline std::optional<MinefieldGeneratorContent> ReadMinefieldGenerator(const ObjectDefinition &object, const GameData &gameData)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "GenerateMinefieldBehavior")
			continue;
		const engine::config::Node &block = *module.block;
		MinefieldGeneratorContent generator;
		generator.distance = gameData.standardMinefieldDistance;
		generator.density = gameData.standardMinefieldDensity;
		const auto flag = [&](std::string_view key, bool fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
		};
		const auto text = [&](std::string_view key) -> std::string {
			const auto *node = block.Find(key);
			return node != nullptr && !node->Value().empty() && node->Value() != "None" ? std::string(node->Value()) : std::string{};
		};
		const auto percent = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const auto *node = block.Find(key);
			if (node == nullptr)
				return fallback;
			std::string_view value = node->Value();
			if (!value.empty() && value.back() == '%')
				value.remove_suffix(1);
			return engine::config::values::ParseFixed(value).value_or(Engine::Math::Fixed{}) / Engine::Math::Fixed::FromInt(100);
		};
		generator.mine = text("MineName");
		generator.upgradedMine = text("UpgradedMineName");
		generator.effect = text("GenerationFX");
		if (const auto *node = block.Find("DistanceAroundObject"))
			generator.distance = engine::config::values::ParseFixed(node->Value()).value_or(generator.distance);
		if (const auto *node = block.Find("MinesPerSquareFoot"))
			generator.density = engine::config::values::ParseFixed(node->Value()).value_or(generator.density);
		generator.onDeath = flag("GenerateOnlyOnDeath", false);
		generator.borderOnly = flag("BorderOnly", true);
		generator.smartBorder = flag("SmartBorder", false);
		generator.smartBorderSkipInterior = flag("SmartBorderSkipInterior", true);
		generator.alwaysCircular = flag("AlwaysCircular", false);
		generator.upgradable = flag("Upgradable", false);
		generator.jitter = percent("RandomJitter", Engine::Math::Fixed{});
		generator.underStructure = percent("SkipIfThisMuchUnderStructure", generator.underStructure);
		return generator;
	}
	return std::nullopt;
}
}
