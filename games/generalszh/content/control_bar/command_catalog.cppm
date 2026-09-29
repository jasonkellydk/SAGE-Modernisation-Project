export module games.generalszh.content.control_bar.command_catalog;
import std;

export import engine.config.binding.schema;

// The control bar's commands as content (Data/INI/CommandButton.ini and CommandSet.ini, the original's
// ControlBar::parseCommandButtonDefinition / parseCommandSetDefinition): each button's command (GUI_COMMAND_*), what it
// builds, researches or fires, its art and labels and options; each set's buttons by slot (the set's line N is slot
// N - 1, shown on ButtonCommandNN; 18 slots, the last ones script-only).
export namespace generalszh::content
{
enum class ButtonCommand : std::uint8_t
{
	None,
	UnitBuild,        // UNIT_BUILD: queue the Object at the selected factory
	DozerConstruct,   // DOZER_CONSTRUCT: place the Object for the selected builder
	CancelUnitBuild,  // CANCEL_UNIT_BUILD
	PlayerUpgrade,    // PLAYER_UPGRADE: research the Upgrade for the player
	ObjectUpgrade,    // OBJECT_UPGRADE: research the Upgrade for the selected object
	CancelUpgrade,    // CANCEL_UPGRADE
	Sell,             // SELL
	Stop,             // STOP
	SpecialPower,     // SPECIAL_POWER (and its variants)
	PurchaseScience,  // PURCHASE_SCIENCE: buy the first of its Science the player may
	ToggleOvercharge, // TOGGLE_OVERCHARGE: switch the selected power plant's overcharge
	SwitchWeapon,     // SWITCH_WEAPON: lock the selection to WeaponSlot (a demo trap's modes)
	Other,            // everything else (attack move, guard, evacuate, fire weapon, ...): its raw name kept
};

// CommandButton Options bits (the original's CommandOption).
namespace button_option
{
inline constexpr std::uint32_t NeedTarget = 1u << 0;        // NEED_TARGET_ENEMY_OBJECT / NEUTRAL / ALLY / POS
inline constexpr std::uint32_t CheckLike = 1u << 1;         // CHECK_LIKE
inline constexpr std::uint32_t ScriptOnly = 1u << 2;        // SCRIPT_ONLY: never shown
inline constexpr std::uint32_t NeedScience = 1u << 3;       // NEED_SPECIAL_POWER_SCIENCE
inline constexpr std::uint32_t OkForMultiSelect = 1u << 4;  // OK_FOR_MULTI_SELECT
inline constexpr std::uint32_t NeedTargetEnemy = 1u << 5;   // NEED_TARGET_ENEMY_OBJECT
inline constexpr std::uint32_t NeedTargetNeutral = 1u << 6; // NEED_TARGET_NEUTRAL_OBJECT
inline constexpr std::uint32_t NeedTargetAlly = 1u << 7;    // NEED_TARGET_ALLY_OBJECT
inline constexpr std::uint32_t NeedTargetPos = 1u << 8;     // NEED_TARGET_POS
inline constexpr std::uint32_t AttackObjectsPosition = 1u << 9; // ATTACK_OBJECTS_POSITION
inline constexpr std::uint32_t ContextModeCommand = 1u << 10; // CONTEXTMODE_COMMAND: a context command (handleGuiCommand)
inline constexpr std::uint32_t NeedUpgrade = 1u << 11;        // NEED_UPGRADE: only with its Upgrade
inline constexpr std::uint32_t MustBeStopped = 1u << 12;      // MUST_BE_STOPPED: not while the object moves
inline constexpr std::uint32_t IgnoresUnderpowered = 1u << 13; // IGNORES_UNDERPOWERED: usable when only underpowered
inline constexpr std::uint32_t NotQueueable = 1u << 14;       // NOT_QUEUEABLE: not while anything is in production
inline constexpr std::uint32_t OptionOne = 1u << 15;          // OPTION_ONE (a Strategy Center's bombardment)
inline constexpr std::uint32_t OptionTwo = 1u << 16;          // OPTION_TWO (hold the line)
inline constexpr std::uint32_t OptionThree = 1u << 17;        // OPTION_THREE (search and destroy)
// COMMAND_OPTION_NEED_OBJECT_TARGET: any of the object targets.
inline constexpr std::uint32_t NeedObjectTarget = NeedTargetEnemy | NeedTargetNeutral | NeedTargetAlly;
}

struct CommandButtonContent
{
	std::string name;
	ButtonCommand command{ButtonCommand::None};
	std::string commandName; // its GUI_COMMAND_* name as written
	std::string object;
	std::string upgrade;
	std::string specialPower;
	std::string buttonImage;
	std::string textLabel;
	std::string descriptLabel;
	std::string borderType;
	// The cursor while it waits for its target, over a valid one and otherwise (CursorName / InvalidCursorName: a
	// MouseCursor name; unknown or none: CROSS).
	std::string cursorName;
	std::string invalidCursorName;
	std::uint32_t options{0};
	// MaxShotsToFire (0: no limit, the original's NO_MAX_SHOTS_LIMIT).
	std::uint32_t maxShotsToFire{0};
	std::uint8_t weaponSlot{0}; // WeaponSlot: PRIMARY 0, SECONDARY 1, TERTIARY 2
	std::vector<std::string> sciences; // Science (a list)
};

inline constexpr std::size_t CommandSlots = 18;

struct CommandSetContent
{
	std::string name;
	std::array<std::string, CommandSlots> buttons{}; // button names by slot (empty: none)
};

struct CommandCatalog
{
	std::map<std::string, CommandButtonContent, std::less<>> buttons;
	std::map<std::string, CommandSetContent, std::less<>> sets;

	const CommandButtonContent *Button(std::string_view name) const
	{
		const auto found = buttons.find(name);
		return found == buttons.end() ? nullptr : &found->second;
	}
	const CommandSetContent *Set(std::string_view name) const
	{
		const auto found = sets.find(name);
		return found == sets.end() ? nullptr : &found->second;
	}
};

namespace command_catalog_detail
{
inline bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

inline ButtonCommand CommandOf(std::string_view name)
{
	static constexpr std::array<std::pair<std::string_view, ButtonCommand>, 12> known{{
		{"PURCHASE_SCIENCE", ButtonCommand::PurchaseScience},
		{"UNIT_BUILD", ButtonCommand::UnitBuild},
		{"DOZER_CONSTRUCT", ButtonCommand::DozerConstruct},
		{"CANCEL_UNIT_BUILD", ButtonCommand::CancelUnitBuild},
		{"PLAYER_UPGRADE", ButtonCommand::PlayerUpgrade},
		{"OBJECT_UPGRADE", ButtonCommand::ObjectUpgrade},
		{"CANCEL_UPGRADE", ButtonCommand::CancelUpgrade},
		{"SELL", ButtonCommand::Sell},
		{"STOP", ButtonCommand::Stop},
		{"SPECIAL_POWER", ButtonCommand::SpecialPower},
		{"TOGGLE_OVERCHARGE", ButtonCommand::ToggleOvercharge},
		{"SWITCH_WEAPON", ButtonCommand::SwitchWeapon},
	}};
	for (const auto &[key, command] : known)
		if (Same(name, key))
			return command;
	if (name.starts_with("SPECIAL_POWER"))
		return ButtonCommand::SpecialPower;
	return name.empty() ? ButtonCommand::None : ButtonCommand::Other;
}
}

// Later definitions of a button or set replace earlier ones (the retail files are read after the defaults).
inline CommandCatalog BindCommandCatalog(const engine::config::Document &commandSets, const engine::config::Document &commandButtons)
{
	using namespace command_catalog_detail;
	CommandCatalog catalog;
	for (const engine::config::Node &root : commandButtons.Roots())
	{
		if (root.key != "CommandButton" || root.values.empty())
			continue;
		CommandButtonContent button;
		button.name = std::string(root.Value());
		const auto text = [&](std::string_view key) -> std::string {
			const auto *node = root.Find(key);
			return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
		};
		button.commandName = text("Command");
		button.command = CommandOf(button.commandName);
		button.object = text("Object");
		button.upgrade = text("Upgrade");
		button.specialPower = text("SpecialPower");
		button.buttonImage = text("ButtonImage");
		button.cursorName = text("CursorName");
		button.invalidCursorName = text("InvalidCursorName");
		button.textLabel = text("TextLabel");
		button.descriptLabel = text("DescriptLabel");
		button.borderType = text("ButtonBorderType");
		if (const std::string slot = text("WeaponSlot"); !slot.empty())
			button.weaponSlot = slot == "SECONDARY" ? 1 : slot == "TERTIARY" ? 2 : 0;
		if (const auto *sciences = root.Find("Science"))
			for (const std::string_view science : sciences->values)
				if (science != "None")
					button.sciences.emplace_back(science);
		if (const auto *shots = root.Find("MaxShotsToFire"); shots != nullptr && !shots->values.empty())
			std::from_chars(shots->Value().data(), shots->Value().data() + shots->Value().size(), button.maxShotsToFire);
		if (const auto *options = root.Find("Options"))
			for (const std::string_view option : options->values)
			{
				if (option.starts_with("NEED_TARGET"))
				{
					button.options |= button_option::NeedTarget;
					if (Same(option, "NEED_TARGET_ENEMY_OBJECT"))
						button.options |= button_option::NeedTargetEnemy;
					else if (Same(option, "NEED_TARGET_NEUTRAL_OBJECT"))
						button.options |= button_option::NeedTargetNeutral;
					else if (Same(option, "NEED_TARGET_ALLY_OBJECT"))
						button.options |= button_option::NeedTargetAlly;
					else if (Same(option, "NEED_TARGET_POS"))
						button.options |= button_option::NeedTargetPos;
				}
				else if (Same(option, "ATTACK_OBJECTS_POSITION"))
					button.options |= button_option::AttackObjectsPosition;
				else if (Same(option, "CHECK_LIKE"))
					button.options |= button_option::CheckLike;
				else if (Same(option, "SCRIPT_ONLY"))
					button.options |= button_option::ScriptOnly;
				else if (Same(option, "NEED_SPECIAL_POWER_SCIENCE"))
					button.options |= button_option::NeedScience;
				else if (Same(option, "NEED_UPGRADE"))
					button.options |= button_option::NeedUpgrade;
				else if (Same(option, "MUST_BE_STOPPED"))
					button.options |= button_option::MustBeStopped;
				else if (Same(option, "IGNORES_UNDERPOWERED"))
					button.options |= button_option::IgnoresUnderpowered;
				else if (Same(option, "NOT_QUEUEABLE"))
					button.options |= button_option::NotQueueable;
				else if (Same(option, "OK_FOR_MULTI_SELECT"))
					button.options |= button_option::OkForMultiSelect;
				else if (Same(option, "CONTEXTMODE_COMMAND"))
					button.options |= button_option::ContextModeCommand;
				else if (Same(option, "OPTION_ONE"))
					button.options |= button_option::OptionOne;
				else if (Same(option, "OPTION_TWO"))
					button.options |= button_option::OptionTwo;
				else if (Same(option, "OPTION_THREE"))
					button.options |= button_option::OptionThree;
			}
		catalog.buttons.insert_or_assign(button.name, std::move(button));
	}
	for (const engine::config::Node &root : commandSets.Roots())
	{
		if (root.key != "CommandSet" || root.values.empty())
			continue;
		CommandSetContent set;
		set.name = std::string(root.Value());
		for (const engine::config::Node &slot : root.children)
		{
			int number = 0;
			const std::string_view key = slot.key;
			const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), number);
			if (error != std::errc{} || end != key.data() + key.size() || number < 1 || number > static_cast<int>(CommandSlots) || slot.values.empty())
				continue;
			set.buttons[static_cast<std::size_t>(number - 1)] = std::string(slot.Value());
		}
		catalog.sets.insert_or_assign(set.name, std::move(set));
	}
	return catalog;
}
}
