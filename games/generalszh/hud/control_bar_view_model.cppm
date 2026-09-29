export module games.generalszh.hud.control_bar_view_model;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.hud.control_bar_state;
export import games.generalszh.commands.game_commands;

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
	// The production queue (ButtonQueue01..09) shows while something is queued, in place of the portrait.
	engine::gui::mvvm::Observable<bool> queueShown{false};
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
	// The selected object whose commands show (none: no single one of the player's).
	ecs::Entity Selected() const noexcept { return m_state.selected; }

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
	}

private:
	// ControlBar::processCommandUI: what a command button orders for the selected object.
	void Press(std::size_t slot)
	{
		const CommandSlot &command = m_state.slots[slot];
		if (command.button == nullptr || m_state.selected == ecs::Entity{})
			return;
		const content::CommandButtonContent &button = *command.button;
		const ecs::Entity selected = m_state.selected;
		using content::ButtonCommand;
		switch (button.command)
		{
		case ButtonCommand::UnitBuild: m_submit(commands::QueueUnit{selected, button.object}); break;
		case ButtonCommand::PlayerUpgrade:
		case ButtonCommand::ObjectUpgrade: m_submit(commands::ResearchUpgrade{selected, button.upgrade}); break;
		case ButtonCommand::CancelUpgrade: m_submit(commands::CancelResearch{selected, button.upgrade}); break;
		case ButtonCommand::Sell: m_submit(commands::Sell{selected}); break;
		case ButtonCommand::Stop: m_submit(commands::Stop{{selected}}); break;
		case ButtonCommand::ToggleOvercharge: m_submit(commands::ToggleOvercharge{{selected}}); break;
		case ButtonCommand::SwitchWeapon: m_submit(commands::SwitchWeapon{{selected}, button.weaponSlot}); break;
		case ButtonCommand::DozerConstruct: placing.Set(button.object); break;
		// GUI_COMMAND_SPECIAL_POWER: needing a target, it waits for one (setGUICommand); else it goes at once
		// (MSG_DO_SPECIAL_POWER, no target).
		// GUARD (GUI_COMMAND_GUARD, and GUARD_WITHOUT_PURSUIT, GUARD_FLYING_UNITS_ONLY): always waits for its target (setGUICommand).
		case ButtonCommand::Other:
			if (button.commandName.starts_with("GUARD") || button.commandName == "SET_RALLY_POINT" ||
				(button.commandName == "ATTACK_MOVE" && (button.options & content::button_option::NeedTargetPos) != 0))
				targeting.Set(button.name);
			// GUI_COMMAND_FIRE_WEAPON: needing a target, it waits for one (setGUICommand); else MSG_DO_WEAPON at once (the
			// weapon in its slot fired where each stands: a bomb truck's detonation).
			else if (button.commandName == "FIRE_WEAPON")
			{
				if ((button.options & (content::button_option::NeedTargetPos | content::button_option::NeedObjectTarget)) != 0)
					targeting.Set(button.name);
				else
					m_submit(commands::FireWeapon{{selected}, button.weaponSlot, 0, button.maxShotsToFire, {}, {}});
			}
			// GUI_COMMAND_EVACUATE without NEED_TARGET_POS (every shipped one): MSG_EVACUATE at once.
			else if (button.commandName == "EVACUATE" && (button.options & content::button_option::NeedTargetPos) == 0)
				m_submit(commands::Evacuate{{selected}});
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
	ControlBarState m_state;
};
}
