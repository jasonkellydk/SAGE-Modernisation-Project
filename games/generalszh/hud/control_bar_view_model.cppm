export module games.generalszh.hud.control_bar_view_model;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.hud.control_bar_state;
export import games.generalszh.hud.control_bar_portrait;
export import games.generalszh.commands.game_commands;
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

	// What selects the next idle worker and looks at it (InGameUI::selectNextIdleWorker: a selection and the view's).
	void SetSelectNextIdleWorker(std::function<void()> select) { m_selectNextIdleWorker = std::move(select); }

	// Generals.csf's text by label (CONTROLBAR:UnderConstructionDesc, CONTROLBAR:OCLTimerDesc...).
	void SetLabels(std::function<std::u16string(std::string_view)> labels) { m_labels = std::move(labels); }

	// Told of each command button pressed (its window and name): ControlBar::processCommandUI stops its flash.
	void SetPressed(std::function<void(std::size_t, std::string_view)> pressed) { m_pressed = std::move(pressed); }

	void Apply(ControlBarState state)
	{
		m_state = std::move(state);
		shown.Set(m_state.hasPlayer);
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
			commandImage[slot].Set(visible ? command.image : std::string{});
			commandClock[slot].Set(visible ? command.clock : 1000u);
			commandClicked[slot].enabled.Set(visible && pressable);
			commandFlashing[slot].Set(visible && command.flashing);
			commandOverlay[slot].Set(visible ? command.overlay : std::string{});
			commandAlwaysColor[slot].Set(visible && command.alwaysColor);
		}
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
		using content::ButtonCommand;
		switch (button.command)
		{
		case ButtonCommand::UnitBuild: m_submit(commands::QueueUnit{selected, button.object}); break;
		case ButtonCommand::PlayerUpgrade:
		case ButtonCommand::ObjectUpgrade: m_submit(commands::ResearchUpgrade{selected, button.upgrade}); break;
		case ButtonCommand::CancelUpgrade: m_submit(commands::CancelResearch{selected, button.upgrade}); break;
		case ButtonCommand::Sell: m_submit(commands::Sell{selected}); break;
		case ButtonCommand::Stop: m_submit(commands::Stop{group}); break;
		case ButtonCommand::ToggleOvercharge: m_submit(commands::ToggleOvercharge{group}); break;
		case ButtonCommand::SwitchWeapon: m_submit(commands::SwitchWeapon{group, button.weaponSlot}); break;
		case ButtonCommand::DozerConstruct: placing.Set(button.object); break;
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
			if ((button.options & (content::button_option::NeedTargetPos | content::button_option::NeedObjectTarget)) != 0)
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
};
}
