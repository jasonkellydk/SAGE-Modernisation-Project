export module games.generalszh.hud.control_bar_view;
import std;

export import games.generalszh.hud.control_bar_view_model;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND.Document;

// Binds Window/ControlBar.wnd to the ControlBarViewModel (the original's ControlBar windows): the command buttons
// (ButtonCommand01..14: shown, enabled, their art (drawn by overlay states: grey while unavailable, dimmed for the
// structure inventory's), an inventory rider's rank over it, clicked), the money readout (MoneyDisplay), and the production queue
// (ProductionQueueWindow with ButtonQueue01..09) in place of the unit's portrait (WinUnitSelected) while something is
// queued; the under-construction and OCL timer panels. The beacon panel stays hidden until it is ported; the observer
// panels are bound by the ObserverPanelView.
// This table is the whole view: no control bar logic here.
export namespace generalszh::hud
{
inline std::string CommandWindowName(std::size_t slot)
{
	return std::format("ControlBar.wnd:ButtonCommand{:02}", slot + 1);
}

inline std::string QueueWindowName(std::size_t slot)
{
	return std::format("ControlBar.wnd:ButtonQueue{:02}", slot + 1);
}

class ControlBarView
{
public:
	ControlBarView(Engine::UI::WND::WNDBindings &bindings, Engine::UI::WND::WNDDocument &document, ControlBarViewModel &viewModel,
		std::function<Engine::UI::WND::ImageRef(std::string_view)> resolve, Graphics::Color2D buildClockColor = {0.0f, 0.0f, 0.0f, 100.0f / 255.0f})
	{
		using Engine::UI::WND::WindowFlag;
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			const std::string name = CommandWindowName(slot);
			bindings.BindVisible(name, viewModel.commandShown[slot]);
			bindings.BindEnabled(name, viewModel.commandEnabled[slot]);
			bindings.BindChecked(name, viewModel.commandChecked[slot]);
			bindings.BindImage(name, viewModel.commandImage[slot], resolve);
			bindings.BindCommand(name, viewModel.commandClicked[slot]);
			// getCommandAvailability: a charging power draws its inverse clock (m_buildUpClockColor).
			bindings.BindClock(name, viewModel.commandClock[slot], buildClockColor, true);
			// CAMEO_FLASH: WIN_STATUS_FLASHING, drawn as the Cameo_push overlay.
			bindings.BindFlag(name, WindowFlag::Flashing, viewModel.commandFlashing[slot]);
			// An inventory rider's rank over its art (GadgetButtonDrawOverlayImage).
			bindings.BindOverlay(name, viewModel.commandOverlay[slot], resolve);
			// The structure inventory's buttons dim rather than grey while disabled (WIN_STATUS_ALWAYS_COLOR).
			bindings.BindFlag(name, WindowFlag::AlwaysColor, viewModel.commandAlwaysColor[slot]);
			// ControlBar::setCommandBarBorder: its kind's border colour (GadgetButtonSetBorder).
			bindings.BindBorder(name, viewModel.commandBorder[slot]);
			// ControlBar::init: command buttons draw by overlay states (their art, grey while unavailable).
			document.Set_Window_Flag(name, WindowFlag::UseOverlayStates, true);
		}
		bindings.BindVisible("ControlBar.wnd:CommandWindow", viewModel.commandsShown);
		bindings.BindText("ControlBar.wnd:MoneyDisplay", viewModel.money);
		bindings.BindVisible("ControlBar.wnd:MoneyDisplay", viewModel.shown);
		bindings.BindVisible("ControlBar.wnd:ProductionQueueWindow", viewModel.queueShown);
		bindings.BindVisible("ControlBar.wnd:WinUnitSelected", viewModel.portraitShown);
		bindings.BindImage("ControlBar.wnd:CameoWindow", viewModel.portraitImage, resolve);
		bindings.BindOverlay("ControlBar.wnd:CameoWindow", viewModel.portraitOverlay, resolve);
		for (std::size_t slot = 0; slot < UpgradeCameos; ++slot)
		{
			const std::string name = "ControlBar.wnd:UnitUpgrade" + std::to_string(slot + 1);
			bindings.BindVisible(name, viewModel.upgradeShown[slot]);
			bindings.BindImage(name, viewModel.upgradeImage[slot], resolve);
			bindings.BindEnabled(name, viewModel.upgradeEnabled[slot]);
		}
		for (std::size_t slot = 0; slot < QueueButtons; ++slot)
		{
			const std::string name = QueueWindowName(slot);
			bindings.BindVisible(name, viewModel.queueSlotShown[slot]);
			bindings.BindImage(name, viewModel.queueImage[slot], resolve);
			bindings.BindCommand(name, viewModel.queueClicked[slot]);
		}
		// ControlBar::updateContextCommand: the front of the queue shows its build as an inverse clock.
		bindings.BindClock(QueueWindowName(0), viewModel.buildProgress, buildClockColor, true);
		// CB_CONTEXT_UNDER_CONSTRUCTION: its panel, Cancel (overlay states, as populateUnderConstruction sets) and the percent.
		bindings.BindVisible("ControlBar.wnd:UnderConstructionWindow", viewModel.underConstructionShown);
		bindings.BindText("ControlBar.wnd:UnderConstructionDesc", viewModel.constructionText);
		bindings.BindImage("ControlBar.wnd:ButtonCancelConstruction", viewModel.cancelConstructionImage, resolve);
		bindings.BindCommand("ControlBar.wnd:ButtonCancelConstruction", viewModel.cancelConstructionClicked);
		document.Set_Window_Flag("ControlBar.wnd:ButtonCancelConstruction", WindowFlag::UseOverlayStates, true);
		// CB_CONTEXT_OCL_TIMER: its panel, the Sell (or rally point) button, the countdown text and bar.
		bindings.BindVisible("ControlBar.wnd:OCLTimerWindow", viewModel.oclTimerShown);
		bindings.BindText("ControlBar.wnd:OCLTimerStaticText", viewModel.oclTimerText);
		bindings.BindProgress("ControlBar.wnd:OCLTimerProgressBar", viewModel.oclTimerProgress);
		bindings.BindVisible("ControlBar.wnd:OCLTimerSellButton", viewModel.oclButtonShown);
		bindings.BindImage("ControlBar.wnd:OCLTimerSellButton", viewModel.oclButtonImage, resolve);
		bindings.BindCommand("ControlBar.wnd:OCLTimerSellButton", viewModel.oclButtonClicked);
		document.Set_Window_Flag("ControlBar.wnd:OCLTimerSellButton", WindowFlag::UseOverlayStates, true);
		bindings.BindEnabled("ControlBar.wnd:ButtonIdleWorker", viewModel.idleWorkerEnabled);
		bindings.BindCommand("ControlBar.wnd:ButtonIdleWorker", viewModel.idleWorkerClicked);
		// The observer panels are the ObserverPanelView's (observer_panel_view.cppm).
		document.Set_Window_Flag("ControlBar.wnd:BeaconWindow", WindowFlag::Hidden, true);
	}
};
}
