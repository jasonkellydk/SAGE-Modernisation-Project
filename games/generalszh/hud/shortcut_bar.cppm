export module games.generalszh.hud.shortcut_bar;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.session.session_view;
export import games.generalszh.commands.game_commands;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.sciences.resources.player_sciences;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// The general's powers shortcut bar (the original's ControlBar::initSpecialPowershortcutBar /
// populateSpecialPowerShortcut / updateSpecialPowerShortcut / drawSpecialPowerShortcutMultiplierText /
// canShowSpecialPowerShortcut and processCommandUI's GUI_COMMAND_SPECIAL_POWER_FROM_SHORTCUT and
// GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE): read each frame from the session's view, headless. Views bind to the view
// model; the host feeds it the frame's state.
export namespace generalszh::hud
{
inline constexpr std::size_t ShortcutButtons = 11; // MAX_SPECIAL_POWER_SHORTCUTS

struct ShortcutSlot
{
	const content::CommandButtonContent *button{nullptr}; // none: the window stays hidden
	bool shown{false};
	bool enabled{false};
	bool alwaysColor{false};  // WIN_STATUS_ALWAYS_COLOR (COMMAND_CANT_AFFORD)
	std::uint32_t clock{1000}; // a charging power's inverse clock, per mille (1000: none)
	std::string image;
	std::u16string count;      // how many are ready, when more than one
};

struct ShortcutBarState
{
	bool hasPlayer{false};
	std::string layout; // SpecialPowerShortcutWinName (empty: the side has no bar)
	std::size_t used{0}; // SpecialPowerShortcutButtonCount (at most MAX_SPECIAL_POWER_SHORTCUTS)
	bool canShow{false}; // canShowSpecialPowerShortcut
	std::array<ShortcutSlot, ShortcutButtons> slots{};
};

namespace shortcut_bar_detail
{
inline const content::PlayerTemplateInfo *Faction(session::SessionView &view, std::uint32_t player)
{
	const std::string name = view.PlayerTemplateName(player);
	for (const content::PlayerTemplateInfo &info : view.Content().playerTemplates.templates)
		if (info.name == name)
			return &info;
	return nullptr;
}

// The purchase-science button (rank 1, 3 or 8 of the General's Powers screen) whose first science is `science`, for
// its art (CommandButton::copyImagesFrom); none: the button keeps its own.
inline const content::CommandButtonContent *PurchaseButtonFor(session::SessionView &view, const content::PlayerTemplateInfo &faction,
	std::string_view science)
{
	static constexpr std::array<std::size_t, 3> buttons{4, 15, 4}; // MAX_PURCHASE_SCIENCE_RANK_1 / 3 / 8
	const content::CommandCatalog &commands = view.Content().commands;
	std::array<std::optional<content::CommandSetContent>, 3> sets;
	for (std::size_t rank = 0; rank < 3; ++rank)
	{
		if (faction.purchaseScienceCommandSets[rank].empty())
			return nullptr;
		sets[rank] = generalszh::gameplay::EffectiveCommandSet(commands, view.World().FindResource<generalszh::gameplay::CommandBarOverrides>(),
			faction.purchaseScienceCommandSets[rank]);
		if (!sets[rank])
			return nullptr;
	}
	for (std::size_t rank = 0; rank < 3; ++rank)
		for (std::size_t slot = 0; slot < buttons[rank]; ++slot)
		{
			const content::CommandButtonContent *each = commands.Button(sets[rank]->buttons[slot]);
			if (each != nullptr && each->command == content::ButtonCommand::PurchaseScience && !each->sciences.empty() && each->sciences.front() == science)
				return each;
		}
	return nullptr;
}
}

// The bar for `player` (none: an observer): nothing unless its side names a bar, a button count and a command set that
// exists. Its buttons fill the windows in order, those it cannot show skipped: one needing an upgrade the player has
// not got; a power needing a science (NEED_SPECIAL_POWER_SCIENCE) with no object of the player's to fire it from or
// whose science the player lacks (its art then that of the best science of its Science list the player has, in order);
// a select-all with none of the type. Each then as ControlBar::getCommandAvailability finds it for the most ready of
// the player's objects with the power (none: hidden); a select-all as the first power button of its most ready
// object's command set (the clock on this button), else available.
inline ShortcutBarState ReadShortcutBar(session::SessionView &view, std::optional<std::uint32_t> player)
{
	using namespace shortcut_bar_detail;
	namespace gp = engine::gameplay;
	ShortcutBarState state;
	if (!player)
		return state;
	const content::PlayerTemplateInfo *faction = Faction(view, *player);
	if (faction == nullptr || faction->specialPowerShortcutButtonCount <= 0 || faction->specialPowerShortcutWinName.empty())
		return state;
	state.hasPlayer = true;
	state.layout = faction->specialPowerShortcutWinName;
	state.used = std::min<std::size_t>(static_cast<std::size_t>(faction->specialPowerShortcutButtonCount), ShortcutButtons);
	const content::GameContent &content = view.Content();
	auto &world = view.World();
	const auto set = faction->specialPowerShortcutCommandSet.empty()
		? std::nullopt
		: generalszh::gameplay::EffectiveCommandSet(content.commands, world.FindResource<generalszh::gameplay::CommandBarOverrides>(),
			  faction->specialPowerShortcutCommandSet);
	const auto *sciences = world.FindResource<gp::PlayerSciences>();
	const auto *upgrades = world.FindResource<gp::PlayerUpgrades>();
	const auto hasScience = [&](std::string_view name) {
		const auto bit = content.Science(name);
		return bit && sciences != nullptr && sciences->Has(*player, *bit);
	};
	const auto powerType = [&](const content::CommandButtonContent &button) -> std::optional<std::string_view> {
		const auto index = content.powers.Template(button.specialPower);
		return index ? std::optional<std::string_view>(content.powers.templates[*index].type) : std::nullopt;
	};
	std::size_t current = 0;
	bool anySelection = false;
	for (std::size_t index = 0; set && index < state.used; ++index)
	{
		const content::CommandButtonContent *button = content.commands.Button(set->buttons[index]);
		if (button == nullptr)
			continue;
		std::string image = button->buttonImage;
		if ((button->options & content::button_option::NeedUpgrade) != 0)
			if (const auto upgrade = content.upgrades.Find(button->upgrade); upgrade && (upgrades == nullptr || !upgrades->Completed(*player).Has(*upgrade)))
				continue;
		if ((button->options & content::button_option::NeedScience) != 0)
		{
			const auto type = powerType(*button);
			if (!type || !view.ShortcutPowerSource(*player, *type))
				continue;
			const auto index = content.powers.Template(button->specialPower);
			const std::string &required = content.powers.templates[*index].requiredScience;
			if (!required.empty())
			{
				if (!hasScience(required))
					continue;
				std::optional<std::size_t> best;
				for (std::size_t at = 0; at < button->sciences.size() && hasScience(button->sciences[at]); ++at)
					best = at;
				if (best)
					if (const content::CommandButtonContent *art = PurchaseButtonFor(view, *faction, button->sciences[*best]))
						image = art->buttonImage;
			}
		}
		else if (button->commandName == "SELECT_ALL_UNITS_OF_TYPE" && !view.AnyObjectOfType(*player, button->object))
			continue;
		ShortcutSlot &slot = state.slots[current++];
		slot.button = button;
		slot.image = std::move(image);
	}
	for (std::size_t index = 0; index < current; ++index)
	{
		ShortcutSlot &slot = state.slots[index];
		const content::CommandButtonContent &button = *slot.button;
		gameplay::ButtonState availability = gameplay::ButtonState::Restricted;
		if (const auto type = powerType(button))
		{
			const auto source = view.ShortcutPowerSource(*player, *type);
			availability = source ? view.CommandAvailability(*source, button) : gameplay::ButtonState::Hidden;
			if (availability == gameplay::ButtonState::NotReady)
				slot.clock = view.CommandClock(*source, button);
			if (const std::int32_t ready = view.ReadyShortcutPowers(*player, *type); ready > 1)
			{
				const std::string digits = std::to_string(ready);
				slot.count.assign(digits.begin(), digits.end());
			}
		}
		else if (button.commandName == "SELECT_ALL_UNITS_OF_TYPE")
		{
			anySelection = true;
			availability = gameplay::ButtonState::Hidden;
			if (view.AnyObjectOfType(*player, button.object))
			{
				availability = gameplay::ButtonState::Available;
				if (const auto source = view.MostReadyPowerOfType(*player, button.object))
					if (const auto *its = content.commands.Set(view.CommandSetOf(*source)))
						for (const std::string &name : its->buttons)
							if (const content::CommandButtonContent *power = content.commands.Button(name);
								power != nullptr && power->commandName == "SPECIAL_POWER")
							{
								availability = view.CommandAvailability(*source, *power);
								if (availability == gameplay::ButtonState::NotReady)
									slot.clock = view.CommandClock(*source, *power);
								break;
							}
			}
		}
		slot.shown = availability != gameplay::ButtonState::Hidden;
		slot.enabled = availability == gameplay::ButtonState::Available || availability == gameplay::ButtonState::Active;
		slot.alwaysColor = availability == gameplay::ButtonState::Done;
	}
	state.canShow = anySelection || view.HasAnyShortcutPower(*player);
	return state;
}

// What a press acts on, found as the press happens: the power's source (Player::findMostReadyShortcutSpecialPowerOfType),
// or the objects a select-all selects.
struct ShortcutSources
{
	std::optional<ecs::Entity> power;
	std::vector<ecs::Entity> objects;
};

class ShortcutBarViewModel
{
public:
	// `submit` sends an order onto the command bus; `target` starts the wait for a power's target (the button's name and
	// the object it fires from); `select` makes a new selection of the objects given.
	ShortcutBarViewModel(std::function<void(commands::GameCommand)> submit, std::function<void(std::string, ecs::Entity)> target,
		std::function<void(std::vector<ecs::Entity>)> select, std::function<ShortcutSources(const content::CommandButtonContent &)> sources)
		: m_submit(std::move(submit)), m_target(std::move(target)), m_select(std::move(select)), m_sources(std::move(sources))
	{
		for (std::size_t slot = 0; slot < ShortcutButtons; ++slot)
			clicked[slot].SetAction([this, slot] { Press(slot); });
	}

	ShortcutBarViewModel(const ShortcutBarViewModel &) = delete;
	ShortcutBarViewModel &operator=(const ShortcutBarViewModel &) = delete;

	// GenPowersShortcutBarParent: shown while the bar may show (and the control bar does).
	engine::gui::mvvm::Observable<bool> shown{false};
	std::array<engine::gui::mvvm::Observable<bool>, ShortcutButtons> slotShown;
	std::array<engine::gui::mvvm::Observable<bool>, ShortcutButtons> slotEnabled;
	std::array<engine::gui::mvvm::Observable<bool>, ShortcutButtons> slotAlwaysColor;
	std::array<engine::gui::mvvm::Observable<std::uint32_t>, ShortcutButtons> slotClock;
	std::array<engine::gui::mvvm::Observable<std::string>, ShortcutButtons> slotImage;
	std::array<engine::gui::mvvm::Observable<std::u16string>, ShortcutButtons> slotCount;
	std::array<engine::gui::mvvm::Command, ShortcutButtons> clicked;

	void Apply(ShortcutBarState state)
	{
		m_state = std::move(state);
		shown.Set(m_state.hasPlayer && m_state.canShow);
		for (std::size_t slot = 0; slot < ShortcutButtons; ++slot)
		{
			const ShortcutSlot &each = m_state.slots[slot];
			const bool visible = each.button != nullptr && each.shown;
			slotShown[slot].Set(visible);
			slotEnabled[slot].Set(visible && each.enabled);
			slotAlwaysColor[slot].Set(visible && each.alwaysColor);
			slotClock[slot].Set(visible ? each.clock : 1000u);
			slotImage[slot].Set(visible ? each.image : std::string{});
			slotCount[slot].Set(visible ? each.count : std::u16string{});
			clicked[slot].enabled.Set(visible && each.enabled);
		}
	}

private:
	// processCommandUI: a power from the shortcut fires from the player's most ready object with it, at once (no target
	// needed: MSG_DO_SPECIAL_POWER with that source) or once its target is picked (setGUICommand); a select-all selects
	// every one of the player's objects of its type, anew.
	void Press(std::size_t slot)
	{
		const ShortcutSlot &each = m_state.slots[slot];
		if (each.button == nullptr)
			return;
		const content::CommandButtonContent &button = *each.button;
		const ShortcutSources found = m_sources(button);
		if (button.commandName == "SELECT_ALL_UNITS_OF_TYPE")
		{
			m_select(found.objects);
			return;
		}
		if (button.command != content::ButtonCommand::SpecialPower || !found.power)
			return;
		if ((button.options & (content::button_option::NeedTargetPos | content::button_option::NeedObjectTarget)) != 0)
			m_target(button.name, *found.power);
		else
			m_submit(commands::UseSpecialPower{*found.power, button.specialPower, {}, false, button.options});
	}

	std::function<void(commands::GameCommand)> m_submit;
	std::function<void(std::string, ecs::Entity)> m_target;
	std::function<void(std::vector<ecs::Entity>)> m_select;
	std::function<ShortcutSources(const content::CommandButtonContent &)> m_sources;
	ShortcutBarState m_state;
};
}
