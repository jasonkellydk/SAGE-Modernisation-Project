export module games.generalszh.content.production.production_content;
import std;

export import engine.config.binding.schema;
export import Engine.Core.Math.FixedVector;
export import games.generalszh.content.objects.object_definition;

// Building units as content: what each command set can build (its
// UNIT_BUILD buttons, Data/INI/CommandSet.ini and CommandButton.ini), and a
// factory's production (ProductionUpdate: how many it queues, which units
// come several for the price of one) and exit (the production exit
// modules' UnitCreatePoint and NaturalRallyPoint, in the factory's frame).
export namespace generalszh::content
{
// Command set -> the objects its buttons build.
using BuildLists = std::map<std::string, std::vector<std::string>, std::less<>>;

namespace production_detail
{
bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}
}

BuildLists BindBuildLists(const engine::config::Document &commandSets, const engine::config::Document &commandButtons)
{
	using production_detail::Same;
	std::map<std::string, std::string, std::less<>> builds; // button -> object
	for (const engine::config::Node &root : commandButtons.Roots())
	{
		if (root.key != "CommandButton")
			continue;
		const auto *command = root.Find("Command");
		const auto *object = root.Find("Object");
		if (command != nullptr && object != nullptr && (Same(command->Value(), "UNIT_BUILD") || Same(command->Value(), "DOZER_CONSTRUCT")))
			builds.insert_or_assign(std::string(root.Value()), std::string(object->Value()));
	}
	BuildLists lists;
	for (const engine::config::Node &root : commandSets.Roots())
	{
		if (root.key != "CommandSet")
			continue;
		std::vector<std::string> objects;
		for (const engine::config::Node &slot : root.children)
			if (const auto found = builds.find(slot.Value()); found != builds.end())
				objects.push_back(found->second);
		lists.insert_or_assign(std::string(root.Value()), std::move(objects));
	}
	return lists;
}

// Command set -> the upgrades its PLAYER_UPGRADE / OBJECT_UPGRADE buttons research
// (the original's Object::canProduceUpgrade: a button of its command set).
inline BuildLists BindResearchLists(const engine::config::Document &commandSets, const engine::config::Document &commandButtons)
{
	using production_detail::Same;
	std::map<std::string, std::string, std::less<>> researches; // button -> upgrade
	for (const engine::config::Node &root : commandButtons.Roots())
	{
		if (root.key != "CommandButton")
			continue;
		const auto *command = root.Find("Command");
		const auto *upgrade = root.Find("Upgrade");
		if (command != nullptr && upgrade != nullptr && (Same(command->Value(), "PLAYER_UPGRADE") || Same(command->Value(), "OBJECT_UPGRADE")))
			researches.insert_or_assign(std::string(root.Value()), std::string(upgrade->Value()));
	}
	BuildLists lists;
	for (const engine::config::Node &root : commandSets.Roots())
	{
		if (root.key != "CommandSet")
			continue;
		std::vector<std::string> upgrades;
		for (const engine::config::Node &slot : root.children)
			if (const auto found = researches.find(slot.Value()); found != researches.end())
				upgrades.push_back(found->second);
		lists.insert_or_assign(std::string(root.Value()), std::move(upgrades));
	}
	return lists;
}

struct ObjectProduction
{
	std::uint32_t capacity{9};
	// DisabledTypesToProcess (DisabledMaskType::parseFromINI: the disabled types by name, a bit each in the original's
	// order; default DISABLED_HELD).
	std::uint32_t runsWhileDisabled{1u << 3};
	std::vector<std::pair<std::string, std::uint32_t>> quantities; // built so many at once
	// Where units come out and first head, in the factory's frame.
	Engine::Math::FixedVector3 createPoint;
	Engine::Math::FixedVector3 rallyPoint;
	bool hasExit{false};
	bool supplyExit{false}; // SupplyCenterProductionExitUpdate: its trucks go harvesting
	std::uint32_t exitStealthTicks{0}; // SupplyCenterProductionExitUpdate GrantTemporaryStealth (parseDurationUnsignedInt)
	// QueueProductionExitUpdate: one unit out at a time, ExitDelay (ms up to whole ticks) apart after InitialBurst.
	bool queueExit{false};
	std::uint64_t exitDelayTicks{0};
	std::uint32_t initialBurst{0};
	// Its door animations (NumDoorAnimations and their times) and how long it shows it has finished a unit.
	std::uint32_t doors{0};
	std::uint64_t doorOpenTicks{0};
	std::uint64_t doorWaitTicks{0};
	std::uint64_t doorCloseTicks{0};
	std::uint64_t completeTicks{0};

	std::uint32_t QuantityOf(std::string_view object) const
	{
		for (const auto &[name, count] : quantities)
			if (production_detail::Same(name, object))
				return count;
		return 1;
	}
};

// QueueProductionExitUpdate on its own (a spawner's exit, with no ProductionUpdate: the Angry Mob nexus): ExitDelay (ms
// up to whole ticks) and InitialBurst.
struct QueueExit
{
	std::uint64_t exitDelayTicks{0};
	std::uint32_t initialBurst{0};
};

inline std::optional<QueueExit> ReadQueueExit(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "QueueProductionExitUpdate")
			continue;
		QueueExit exit;
		if (const auto *delay = module.block->Find("ExitDelay"))
			exit.exitDelayTicks = engine::config::ReadDurationTicks(*delay, bind).value_or(0);
		if (const auto *burst = module.block->Find("InitialBurst"))
			exit.initialBurst = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(*burst, bind).value_or(0)));
		return exit;
	}
	return std::nullopt;
}

// A building's DefaultProductionExitUpdate on its own (it may have no ProductionUpdate: a Tech Reinforcement Pad): its
// UnitCreatePoint and NaturalRallyPoint in its frame, and UseSpawnRallyPoint (what its deliveries drop goes out by it).
struct ProductionExit
{
	Engine::Math::FixedVector3 createPoint;
	Engine::Math::FixedVector3 rallyPoint;
	bool useSpawnRallyPoint{false};
};

inline std::optional<ProductionExit> ReadProductionExit(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "DefaultProductionExitUpdate")
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, engine::time::FixedStep{30}};
		ProductionExit exit;
		if (const auto *create = module.block->Find("UnitCreatePoint"))
			exit.createPoint = engine::config::ReadVec3(*create, bind).value_or(Engine::Math::FixedVector3{});
		if (const auto *rally = module.block->Find("NaturalRallyPoint"))
			exit.rallyPoint = engine::config::ReadVec3(*rally, bind).value_or(Engine::Math::FixedVector3{});
		if (const auto *spawn = module.block->Find("UseSpawnRallyPoint"))
			exit.useSpawnRallyPoint = engine::config::values::ParseBool(spawn->Value()).value_or(false);
		return exit;
	}
	return std::nullopt;
}

std::optional<ObjectProduction> ReadObjectProduction(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using production_detail::Same;
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	std::optional<ObjectProduction> production;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr)
			continue;
		const engine::config::Node &block = *module.block;
		if (module.type == "ProductionUpdate")
		{
			if (!production)
				production.emplace();
			if (const auto *types = block.Find("DisabledTypesToProcess"))
			{
				constexpr std::string_view names[] = {"DISABLED_DEFAULT", "DISABLED_HACKED", "DISABLED_EMP", "DISABLED_HELD", "DISABLED_PARALYZED",
					"DISABLED_UNMANNED", "DISABLED_UNDERPOWERED", "DISABLED_FREEFALL", "DISABLED_AWESTRUCK", "DISABLED_BRAINWASHED", "DISABLED_SUBDUED",
					"DISABLED_SCRIPT_DISABLED", "DISABLED_SCRIPT_UNDERPOWERED"};
				production->runsWhileDisabled = 0;
				for (const std::string_view name : types->values)
					for (std::size_t bit = 0; bit < std::size(names); ++bit)
						if (Same(name, names[bit]))
							production->runsWhileDisabled |= 1u << bit;
			}
			if (const auto *entries = block.Find("MaxQueueEntries"))
				production->capacity = static_cast<std::uint32_t>(std::clamp<std::int64_t>(engine::config::ReadInt(*entries, bind).value_or(9), 1, 9));
			const auto ticks = [&](std::string_view key) -> std::uint64_t {
				const auto *node = block.Find(key);
				return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
			};
			if (const auto *doors = block.Find("NumDoorAnimations"))
				production->doors = static_cast<std::uint32_t>(std::clamp<std::int64_t>(engine::config::ReadInt(*doors, bind).value_or(0), 0, 4));
			production->doorOpenTicks = ticks("DoorOpeningTime");
			production->doorWaitTicks = ticks("DoorWaitOpenTime");
			production->doorCloseTicks = ticks("DoorCloseTime");
			production->completeTicks = ticks("ConstructionCompleteDuration");
			for (const engine::config::Node &field : block.children)
				if (Same(field.key, "QuantityModifier") && field.values.size() >= 2)
					production->quantities.emplace_back(std::string(field.values[0]),
						static_cast<std::uint32_t>(std::max<std::int64_t>(1, engine::config::values::ParseInt(field.values[1]).value_or(1))));
		}
	}
	if (!production)
		return std::nullopt;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || !std::string_view(module.type).ends_with("ProductionExitUpdate"))
			continue;
		if (const auto *create = module.block->Find("UnitCreatePoint"))
			production->createPoint = engine::config::ReadVec3(*create, bind).value_or(Engine::Math::FixedVector3{});
		if (const auto *rally = module.block->Find("NaturalRallyPoint"))
			production->rallyPoint = engine::config::ReadVec3(*rally, bind).value_or(Engine::Math::FixedVector3{});
		production->hasExit = true;
		production->supplyExit = module.type == "SupplyCenterProductionExitUpdate";
		if (production->supplyExit)
			if (const auto *grant = module.block->Find("GrantTemporaryStealth"))
				production->exitStealthTicks = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*grant, bind).value_or(0));
		production->queueExit = module.type == "QueueProductionExitUpdate";
		if (production->queueExit)
		{
			if (const auto *delay = module.block->Find("ExitDelay"))
				production->exitDelayTicks = engine::config::ReadDurationTicks(*delay, bind).value_or(0);
			if (const auto *burst = module.block->Find("InitialBurst"))
				production->initialBurst = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(*burst, bind).value_or(0)));
		}
		break;
	}
	return production;
}
}
