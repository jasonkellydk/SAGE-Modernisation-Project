export module games.generalszh.hud.control_bar_view_model;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.hud.control_bar_state;
export import games.generalszh.hud.control_bar_portrait;
export import games.generalszh.commands.game_commands;
export import games.generalszh.hud.hot_keys;
export import games.generalszh.hud.build_refusal;
import games.generalszh.presentation.hud.algorithms.control_bar_timers;

// The control bar's logic without windows (the original's ControlBar populate / update / processCommandUI): what each
// command and queue button shows, the money readout, and what a click orders. Views bind to it; the host feeds it the
// frame's ControlBarState and sends what it orders.
export namespace generalszh::hud
{
class ControlBarViewModel
{
public:
	// `submit` sends an order onto the command bus; `formatMoney` writes the money readout (GUI:ControlBarMoneyDisplay).
	ControlBarViewModel(std::function<void(commands::GameCommand)> submit, std::function<std::u16string(std::int64_t)> formatMoney)
		: m_submit(std::move(submit)), m_formatMoney(std::move(formatMoney))
	{
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
			commandClicked[slot].SetAction([this, slot] { Press(slot); });
		for (std::size_t slot = 0; slot < QueueButtons; ++slot)
			queueClicked[slot].SetAction([this, slot] { Cancel(slot); });
		cancelConstructionClicked.SetAction([this] { PressContextButton(); });
		oclButtonClicked.SetAction([this] { PressContextButton(); });
		idleWorkerClicked.SetAction([this] {
			if (m_selectNextIdleWorker)
				m_selectNextIdleWorker();
		});
	}

	ControlBarViewModel(const ControlBarViewModel &) = delete;
	ControlBarViewModel &operator=(const ControlBarViewModel &) = delete;

	// The bar shows for a player (an observer's is not here yet).
	engine::gui::mvvm::Observable<bool> shown{false};
	engine::gui::mvvm::Observable<std::u16string> money;
	// The player's power made and used (the power bar: W3DPowerDraw).
	engine::gui::mvvm::Observable<std::int32_t> powerProduced{0};
	engine::gui::mvvm::Observable<std::int32_t> powerConsumed{0};
	// The command panel (CommandWindow) shows while the selection has commands (ControlBar::switchToContext).
	engine::gui::mvvm::Observable<bool> commandsShown{false};
	// Each command button (ButtonCommand01..14): shown, enabled, its art; clicked.
	std::array<engine::gui::mvvm::Observable<bool>, CommandButtons> commandShown;
	std::array<engine::gui::mvvm::Observable<bool>, CommandButtons> commandEnabled;
	// A CHECK_LIKE button that is on (COMMAND_ACTIVE: GadgetCheckLikeButtonSetVisualCheck).
	std::array<engine::gui::mvvm::Observable<bool>, CommandButtons> commandChecked;
	std::array<engine::gui::mvvm::Observable<std::string>, CommandButtons> commandImage;
	// A charging power's inverse clock (COMMAND_NOT_READY), per mille; 1000: none.
	std::array<engine::gui::mvvm::Observable<std::uint32_t>, CommandButtons> commandClock;
	std::array<engine::gui::mvvm::Command, CommandButtons> commandClicked;
	// A command button a script made flash, lit now (CAMEO_FLASH: WIN_STATUS_FLASHING).
	std::array<engine::gui::mvvm::Observable<bool>, CommandButtons> commandFlashing;
	// Drawn over a button's art (GadgetButtonDrawOverlayImage: an inventory rider's rank); empty: none.
	std::array<engine::gui::mvvm::Observable<std::string>, CommandButtons> commandOverlay;
	// WIN_STATUS_ALWAYS_COLOR: disabled, it dims rather than greys (the structure inventory's buttons).
	std::array<engine::gui::mvvm::Observable<bool>, CommandButtons> commandAlwaysColor;
	// GadgetButtonSetBorder: the button's border colour by its kind (0xAARRGGBB; 0: none).
	std::array<engine::gui::mvvm::Observable<std::uint32_t>, CommandButtons> commandBorder;
	// The production queue (ButtonQueue01..09) shows while something is queued, in place of the portrait.
	engine::gui::mvvm::Observable<bool> queueShown{false};
	// The selection's portrait (WinUnitSelected: setPortraitByObject shows it for a portrait object; none,
	// setPortraitByObject(nullptr) hides it and the right HUD shows its own image): its SelectPortrait on CameoWindow with
	// its rank over it, and the upgrades it names (UnitUpgrade1..5: shown, their art, enabled once had).
	engine::gui::mvvm::Observable<bool> portraitShown{false};
	engine::gui::mvvm::Observable<std::string> portraitImage;
	engine::gui::mvvm::Observable<std::string> portraitOverlay;
	std::array<engine::gui::mvvm::Observable<bool>, UpgradeCameos> upgradeShown;
	std::array<engine::gui::mvvm::Observable<std::string>, UpgradeCameos> upgradeImage;
	std::array<engine::gui::mvvm::Observable<bool>, UpgradeCameos> upgradeEnabled;
	std::array<engine::gui::mvvm::Observable<bool>, QueueButtons> queueSlotShown;
	std::array<engine::gui::mvvm::Observable<std::string>, QueueButtons> queueImage;
	std::array<engine::gui::mvvm::Command, QueueButtons> queueClicked;
	// The front one's build clock, per mille.
	engine::gui::mvvm::Observable<std::uint32_t> buildProgress{0};
	// A builder's structure awaiting its place (DOZER_CONSTRUCT: InGameUI::placeBuildAvailable); empty: none.
	engine::gui::mvvm::Observable<std::string> placing;
	// Placing for a SPECIAL_POWER_CONSTRUCT button: its power and options (empty: a DOZER_CONSTRUCT build).
	std::string placingPower;
	std::uint32_t placingOptions{0};
	// A command button waiting for its target (InGameUI::setGUICommand: a special power needing a spot or an object):
	// the button's name; empty: none.
	engine::gui::mvvm::Observable<std::string> targeting;
	// CB_CONTEXT_UNDER_CONSTRUCTION (UnderConstructionWindow): ButtonCancelConstruction with Command_CancelConstruction's
	// art, and UnderConstructionDesc ("Building: n%").
	engine::gui::mvvm::Observable<bool> underConstructionShown{false};
	engine::gui::mvvm::Observable<std::u16string> constructionText;
	engine::gui::mvvm::Observable<std::string> cancelConstructionImage;
	engine::gui::mvvm::Command cancelConstructionClicked;
	// CB_CONTEXT_OCL_TIMER (OCLTimerWindow): OCLTimerSellButton (Sell, or a tech building's rally point; hidden without
	// one), OCLTimerStaticText ("Time Until Next Drop: m:ss") and OCLTimerProgressBar.
	engine::gui::mvvm::Observable<bool> oclTimerShown{false};
	engine::gui::mvvm::Observable<std::u16string> oclTimerText;
	engine::gui::mvvm::Observable<int> oclTimerProgress{0};
	engine::gui::mvvm::Observable<bool> oclButtonShown{false};
	engine::gui::mvvm::Observable<std::string> oclButtonImage;
	engine::gui::mvvm::Command oclButtonClicked;
	// ButtonIdleWorker: enabled while the player has an idle worker (updateIdleWorker); pressed, the next is selected
	// (selectNextIdleWorker).
	engine::gui::mvvm::Observable<bool> idleWorkerEnabled{false};
	engine::gui::mvvm::Command idleWorkerClicked;
	// The selected object whose commands show (none: no single one of the player's).
	ecs::Entity Selected() const noexcept { return m_state.selected; }
	// The button on a command window (ButtonCommand01..14 by slot; none: an empty slot): its build tooltip's
	// (GadgetButtonGetData).
	const content::CommandButtonContent *CommandButtonAt(std::size_t slot) const noexcept
	{
		return slot < CommandButtons ? m_state.slots[slot].button : nullptr;
	}

	// What selects the next idle worker and looks at it (InGameUI::selectNextIdleWorker: a selection and the view's).
	void SetSelectNextIdleWorker(std::function<void()> select) { m_selectNextIdleWorker = std::move(select); }

	// Generals.csf's text by label (CONTROLBAR:UnderConstructionDesc, CONTROLBAR:OCLTimerDesc...).
	void SetLabels(std::function<std::u16string(std::string_view)> labels) { m_labels = std::move(labels); }

	// ControlBar::updateCommanBarBorderColors (ControlBarScheme::init): the scheme's ButtonBorderBuildColor,
	// ButtonBorderUpgradeColor, ButtonBorderActionColor and ButtonBorderSystemColor (0xAARRGGBB; 0: none given).
	void SetBorderColors(std::uint32_t build, std::uint32_t upgrade, std::uint32_t action, std::uint32_t system)
	{
		m_borderColors = {build, upgrade, action, system};
	}

	// ControlBar::updateSlotExitImage (ControlBarScheme::init, the scheme's CommandMarkerImage; none: left as they are): the
	// art of Command_StructureExit, Command_TransportExit and Command_BunkerExit becomes that image.
	void SetSlotExitImage(std::string image) { m_exitImage = std::move(image); }

	// A command button's art as it shows: an exit button's own art is the scheme's exit image when it gave one (an
	// occupied slot shows its rider instead).
	std::string ButtonArt(const CommandSlot &command) const
	{
		if (command.button != nullptr && !m_exitImage.empty() && command.image == command.button->buttonImage &&
			(command.button->name == "Command_StructureExit" || command.button->name == "Command_TransportExit" ||
				command.button->name == "Command_BunkerExit"))
			return m_exitImage;
		return command.image;
	}

	// ControlBar::setCommandBarBorder: a command button's border in its ButtonBorderType's colour (BUILD, UPGRADE, ACTION,
	// SYSTEM); NONE or an unknown kind: no border (GadgetButtonSetBorder(GAME_COLOR_UNDEFINED, FALSE)).
	std::uint32_t CommandBorderColor(std::string_view borderType) const noexcept
	{
		static constexpr std::array<std::string_view, 4> kinds{"BUILD", "UPGRADE", "ACTION", "SYSTEM"};
		for (std::size_t kind = 0; kind < kinds.size(); ++kind)
			if (borderType == kinds[kind])
				return m_borderColors[kind];
		return 0u;
	}

	// processCommandUI's canMakeUnit check on a build-type press, and how a refusal is told (hud::BuildRefusalHooks).
	void SetBuildRefusal(BuildRefusalHooks hooks) { m_refusal = std::move(hooks); }

	// What plays an interface sound (HotKeyManager::executeHotKey's GUIClick / GUIClickDisabled).
	void SetSound(std::function<void(std::string_view)> sound) { m_sound = std::move(sound); }

	// HotKeyTranslator / HotKeyManager::executeHotKey for a key released with no modifier (`key`: its printable character):
	// the command window its key marks (the first registered: populateCommand's setControlCommand, the transport's exit
	// buttons first, then the set's slots in order, none for a SCRIPT_ONLY one), unless hidden: enabled, it is clicked
	// (GBM_SELECTED) with a GUIClick and the key is used; disabled, a GUIClickDisabled and the key goes on.
	bool PressHotKey(char key)
	{
		const std::optional<std::size_t> slot = m_hotKeys.Find(key);
		if (!slot)
			return false;
		return ExecuteHotKey(commandShown[*slot].Get(), commandEnabled[*slot].Get(), [&] { commandClicked[*slot].Execute(); },
			[this](std::string_view sound) {
				if (m_sound)
					m_sound(sound);
			});
	}
	// Whether a command window registered `key` (the shortcut bar's and science screen's registrations come after).
	bool HasHotKey(char key) const noexcept { return m_hotKeys.Find(key).has_value(); }

	// Told of each command button pressed (its window and name): ControlBar::processCommandUI stops its flash.
	void SetPressed(std::function<void(std::size_t, std::string_view)> pressed) { m_pressed = std::move(pressed); }

	// CommandXlat's MSG_META_TOGGLE_CONTROL_BAR (F9) through ToggleControlBar: ControlBarParent (and with it the special
	// power shortcut bar) hidden when shown, shown when hidden; never while a replay plays back (`replay`).
	void ToggleHidden(bool replay)
	{
		if (replay)
			return;
		m_hidden = !m_hidden;
		shown.Set(m_state.hasPlayer && !m_hidden);
	}
	bool Hidden() const noexcept { return m_hidden; }

	void Apply(ControlBarState state)
	{
		m_state = std::move(state);
		shown.Set(m_state.hasPlayer && !m_hidden);
		money.Set(m_state.hasPlayer ? m_formatMoney(m_state.money) : std::u16string{});
		powerProduced.Set(m_state.powerProduced);
		powerConsumed.Set(m_state.powerConsumed);
		bool anyCommand = false;
		for (const CommandSlot &command : m_state.slots)
			anyCommand = anyCommand || (command.button != nullptr && command.state != gameplay::ButtonState::Hidden);
		commandsShown.Set(anyCommand);
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			const CommandSlot &command = m_state.slots[slot];
			const bool visible = command.button != nullptr && command.state != gameplay::ButtonState::Hidden;
			commandShown[slot].Set(visible);
			const bool pressable = command.state == gameplay::ButtonState::Available || command.state == gameplay::ButtonState::Active;
			commandEnabled[slot].Set(visible && pressable);
			commandChecked[slot].Set(visible && command.state == gameplay::ButtonState::Active &&
				(command.button->options & content::button_option::CheckLike) != 0);
			commandImage[slot].Set(visible ? ButtonArt(command) : std::string{});
			commandClock[slot].Set(visible ? command.clock : 1000u);
			commandClicked[slot].enabled.Set(visible && pressable);
			commandFlashing[slot].Set(visible && command.flashing);
			commandOverlay[slot].Set(visible ? command.overlay : std::string{});
			commandAlwaysColor[slot].Set(visible && command.alwaysColor);
			commandBorder[slot].Set(visible ? CommandBorderColor(command.button->borderType) : 0u);
		}
		// setControlCommand's hotkeys (searchHotKey(TextLabel), addHotKey keeping the first of a key): the transport's
		// exit buttons as doTransportInventoryUI sets them, then the set's other slots in order, SCRIPT_ONLY ones never.
		m_hotKeys.Clear();
		const auto addHotKey = [&](std::size_t slot) {
			const content::CommandButtonContent &button = *m_state.slots[slot].button;
			m_hotKeys.Add(button.textLabel.empty() || !m_labels ? char{0} : HotKeyOf(m_labels(button.textLabel)), slot);
		};
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
			if (m_state.slots[slot].button != nullptr && m_state.slots[slot].button->commandName == "EXIT_CONTAINER")
				addHotKey(slot);
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
			if (const content::CommandButtonContent *button = m_state.slots[slot].button;
				button != nullptr && button->commandName != "EXIT_CONTAINER" && (button->options & content::button_option::ScriptOnly) == 0)
				addHotKey(slot);
		queueShown.Set(!m_state.queue.empty());
		for (std::size_t slot = 0; slot < QueueButtons; ++slot)
		{
			const bool filled = slot < m_state.queue.size();
			queueSlotShown[slot].Set(filled);
			queueImage[slot].Set(filled ? m_state.queue[slot].image : std::string{});
			queueClicked[slot].enabled.Set(filled);
		}
		buildProgress.Set(m_state.queue.empty() ? 0u : m_state.queue.front().progress);
		const auto label = [this](std::string_view name) { return m_labels ? m_labels(name) : std::u16string(name.begin(), name.end()); };
		const std::string buttonArt = m_state.contextButton != nullptr ? m_state.contextButton->buttonImage : std::string{};
		const bool building = m_state.context == ControlBarContext::UnderConstruction;
		underConstructionShown.Set(building);
		constructionText.Set(building ? presentation::FormatConstructionPercent(label("CONTROLBAR:UnderConstructionDesc"), m_state.constructionPercent)
									  : std::u16string{});
		cancelConstructionImage.Set(building ? buttonArt : std::string{});
		cancelConstructionClicked.enabled.Set(building && m_state.contextButton != nullptr);
		const bool timer = m_state.context == ControlBarContext::OclTimer;
		oclTimerShown.Set(timer);
		oclTimerText.Set(timer ? presentation::FormatOclTimer(label("CONTROLBAR:OCLTimerDesc"), label("CONTROLBAR:OCLTimerDescWithPadding"), m_state.oclRemaining)
							   : std::u16string{});
		oclTimerProgress.Set(timer ? presentation::OclTimerBarProgress(m_state.oclRemaining, m_state.oclTotal) : 0);
		oclButtonShown.Set(timer && m_state.contextButton != nullptr);
		oclButtonImage.Set(timer ? buttonArt : std::string{});
		oclButtonClicked.enabled.Set(timer && m_state.contextButton != nullptr);
		idleWorkerEnabled.Set(m_state.idleWorkers > 0);
		idleWorkerClicked.enabled.Set(m_state.idleWorkers > 0);
	}

	// The frame's portrait (ReadPortrait).
	void ApplyPortrait(const PortraitState &portrait)
	{
		portraitShown.Set(portrait.shown);
		portraitImage.Set(portrait.shown ? portrait.image : std::string{});
		portraitOverlay.Set(portrait.shown ? portrait.overlay : std::string{});
		for (std::size_t slot = 0; slot < UpgradeCameos; ++slot)
		{
			const UpgradeCameo &cameo = portrait.upgrades[slot];
			upgradeShown[slot].Set(portrait.shown && cameo.shown);
			upgradeImage[slot].Set(portrait.shown && cameo.shown ? cameo.image : std::string{});
			upgradeEnabled[slot].Set(portrait.shown && cameo.shown && cameo.enabled);
		}
	}

private:
	// ControlBar::processCommandUI: what a command button orders for the selected object.
	void Press(std::size_t slot)
	{
		const CommandSlot &command = m_state.slots[slot];
		if (command.button == nullptr || m_state.selected == ecs::Entity{})
			return;
		const content::CommandButtonContent &button = *command.button;
		// "if the button is flashing, tell it to stop flashing".
		if (m_pressed)
			m_pressed(slot, button.name);
		const ecs::Entity selected = m_state.selected;
		// processCommandUI's orders to the selection (a group: every selected object that counts).
		const std::vector<ecs::Entity> group = m_state.group.empty() ? std::vector<ecs::Entity>{selected} : m_state.group;
		// "Play any available unit specific sound for button" (for the local player).
		if (m_sound && !button.unitSpecificSound.empty())
			m_sound(button.unitSpecificSound);
		// A button needing a target with USES_MINE_CLEARING_WEAPONSET: MSG_SET_MINE_CLEARING_DETAIL first.
		if ((button.options & content::button_option::NeedTarget) != 0 && (button.options & content::button_option::UsesMineClearingWeaponSet) != 0)
			m_submit(commands::SetMineClearingDetail{group});
		using content::ButtonCommand;
		switch (button.command)
		{
		case ButtonCommand::UnitBuild:
			// canMakeUnit(factory, Object): refused (no money, queue full, parking full, maxed out), nothing is queued.
			if (!m_refusal.Refuses(selected, button))
				m_submit(commands::QueueUnit{selected, button.object});
			break;
		case ButtonCommand::PlayerUpgrade:
		case ButtonCommand::ObjectUpgrade: m_submit(commands::ResearchUpgrade{selected, button.upgrade}); break;
		case ButtonCommand::CancelUpgrade: m_submit(commands::CancelResearch{selected, button.upgrade}); break;
		case ButtonCommand::Sell: m_submit(commands::Sell{selected}); break;
		case ButtonCommand::Stop: m_submit(commands::Stop{group}); break;
		case ButtonCommand::ToggleOvercharge: m_submit(commands::ToggleOvercharge{group}); break;
		case ButtonCommand::SwitchWeapon: m_submit(commands::SwitchWeapon{group, button.weaponSlot}); break;
		case ButtonCommand::DozerConstruct:
			// "Make sure we have enough CASH to build it WHEN we click the button" (canMakeUnit): refused, no placement.
			if (m_refusal.Refuses(selected, button))
				break;
			placingPower.clear();
			placingOptions = 0;
			placing.Set(button.object);
			break;
		// GUI_COMMAND_SPECIAL_POWER: needing a target, it waits for one (setGUICommand); else it goes at once
		// (MSG_DO_SPECIAL_POWER, no target).
		// GUARD (GUI_COMMAND_GUARD, and GUARD_WITHOUT_PURSUIT, GUARD_FLYING_UNITS_ONLY): always waits for its target (setGUICommand).
		case ButtonCommand::Other:
			if (button.commandName.starts_with("GUARD") || button.commandName == "SET_RALLY_POINT" || button.commandName == "COMBATDROP" ||
				(button.commandName == "ATTACK_MOVE" && (button.options & content::button_option::NeedTargetPos) != 0))
				targeting.Set(button.name);
			// GUI_COMMAND_FIRE_WEAPON: needing a target, it waits for one (setGUICommand); else MSG_DO_WEAPON at once (the
			// weapon in its slot fired where each stands: a bomb truck's detonation).
			else if (button.commandName == "FIRE_WEAPON")
			{
				if ((button.options & (content::button_option::NeedTargetPos | content::button_option::NeedObjectTarget)) != 0)
					targeting.Set(button.name);
				else
					m_submit(commands::FireWeapon{group, button.weaponSlot, 0, button.maxShotsToFire, {}, {}});
			}
			// GUI_COMMAND_EVACUATE without NEED_TARGET_POS (every shipped one): MSG_EVACUATE at once.
			else if (button.commandName == "EVACUATE" && (button.options & content::button_option::NeedTargetPos) == 0)
				m_submit(commands::Evacuate{group});
			// GUI_COMMAND_HACK_INTERNET: MSG_INTERNET_HACK at once.
			else if (button.commandName == "HACK_INTERNET")
				m_submit(commands::HackInternet{group});
			// GUI_COMMAND_EXECUTE_RAILED_TRANSPORT: MSG_EXECUTE_RAILED_TRANSPORT.
			else if (button.commandName == "EXECUTE_RAILED_TRANSPORT")
				m_submit(commands::ExecuteRailedTransport{group});
			// GUI_COMMAND_EXIT_CONTAINER: the rider this window shows (m_containData) asks out, MSG_EXIT; an empty slot
			// orders nothing.
			else if (button.commandName == "EXIT_CONTAINER")
			{
				if (command.rider != ecs::Entity{})
					m_submit(commands::Exit{command.rider, selected});
			}
			break;
		case ButtonCommand::SpecialPower:
			// GUI_COMMAND_SPECIAL_POWER_CONSTRUCT (the sneak attack): its Object placed as a build is
			// (placeBuildAvailable), the selected object noting the button (setSpecialPowerConstructionCommandButton).
			if (button.commandName == "SPECIAL_POWER_CONSTRUCT")
			{
				// canMakeUnit(selected, Object), answering OK while it already places that Object for a power
				// (getSpecialPowerConstructionCommandButton's template): refused, no placement.
				if (!(placing.Get() == button.object && !placingPower.empty()) && m_refusal.Refuses(selected, button))
					break;
				placingPower = button.specialPower;
				placingOptions = button.options;
				placing.Set(button.object);
			}
			else if ((button.options & (content::button_option::NeedTargetPos | content::button_option::NeedObjectTarget)) != 0)
				targeting.Set(button.name);
			else
				m_submit(commands::UseSpecialPower{selected, button.specialPower, {}, false, button.options});
			break;
		default: break;
		}
	}

	// The under-construction or OCL timer panel's button (processCommandUI): DOZER_CONSTRUCT_CANCEL cancels the
	// construction (MSG_DOZER_CANCEL_CONSTRUCT), SELL sells, SET_RALLY_POINT waits for its spot.
	void PressContextButton()
	{
		const content::CommandButtonContent *button = m_state.contextButton;
		if (button == nullptr || m_state.selected == ecs::Entity{})
			return;
		if (button->commandName == "DOZER_CONSTRUCT_CANCEL")
			m_submit(commands::CancelConstruction{m_state.selected});
		else if (button->command == content::ButtonCommand::Sell)
			m_submit(commands::Sell{m_state.selected});
		else if (button->commandName == "SET_RALLY_POINT")
			targeting.Set(button->name);
	}

	// A queue button cancels what it shows (Command_CancelUnitCreate / Command_CancelUpgradeCreate).
	void Cancel(std::size_t slot)
	{
		if (slot >= m_state.queue.size() || m_state.selected == ecs::Entity{})
			return;
		const QueueSlot &entry = m_state.queue[slot];
		if (entry.unit)
			m_submit(commands::CancelUnit{m_state.selected, entry.productionId});
		else
			m_submit(commands::CancelResearch{m_state.selected, entry.upgrade});
	}

	std::function<void(commands::GameCommand)> m_submit;
	std::function<std::u16string(std::int64_t)> m_formatMoney;
	std::function<void(std::size_t, std::string_view)> m_pressed;
	std::function<std::u16string(std::string_view)> m_labels;
	std::function<void()> m_selectNextIdleWorker;
	ControlBarState m_state;
	bool m_hidden{false}; // ControlBarParent hidden by the player (ToggleControlBar)
	std::array<std::uint32_t, 4> m_borderColors{}; // build, upgrade, action, system (0: GAME_COLOR_UNDEFINED)
	std::function<void(std::string_view)> m_sound;
	BuildRefusalHooks m_refusal; // processCommandUI's canMakeUnit check (none set: every press goes on)
	HotKeys m_hotKeys; // HotKeyManager's map: key to command window, first kept
	std::string m_exitImage;                       // the scheme's CommandMarkerImage (none: the buttons' own art)
};
}
