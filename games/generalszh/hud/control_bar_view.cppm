export module games.generalszh.hud.control_bar_view;
import std;

export import games.generalszh.hud.control_bar_view_model;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND.Document;

// Binds Window/ControlBar.wnd to the ControlBarViewModel (the original's ControlBar windows): the command buttons
// (ButtonCommand01..14: shown, enabled, their art (drawn by overlay states: grey while unavailable), clicked), the money readout (MoneyDisplay), and the production queue
// (ProductionQueueWindow with ButtonQueue01..09) in place of the unit's portrait (WinUnitSelected) while something is
// queued. The bar's other panels (observers, beacons, OCL timers, construction) stay hidden until they are ported.
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
			// ControlBar::init: command buttons draw by overlay states (their art, grey while unavailable).
			document.Set_Window_Flag(name, WindowFlag::UseOverlayStates, true);
		}
		bindings.BindVisible("ControlBar.wnd:CommandWindow", viewModel.commandsShown);
		bindings.BindText("ControlBar.wnd:MoneyDisplay", viewModel.money);
		bindings.BindVisible("ControlBar.wnd:MoneyDisplay", viewModel.shown);
		bindings.BindVisible("ControlBar.wnd:ProductionQueueWindow", viewModel.queueShown);
		bindings.BindVisible("ControlBar.wnd:WinUnitSelected", viewModel.queueShown, [](bool queued) { return !queued; });
		for (std::size_t slot = 0; slot < QueueButtons; ++slot)
		{
			const std::string name = QueueWindowName(slot);
			bindings.BindVisible(name, viewModel.queueSlotShown[slot]);
			bindings.BindImage(name, viewModel.queueImage[slot], resolve);
			bindings.BindCommand(name, viewModel.queueClicked[slot]);
		}
		// ControlBar::updateContextCommand: the front of the queue shows its build as an inverse clock.
		bindings.BindClock(QueueWindowName(0), viewModel.buildProgress, buildClockColor, true);
		for (const char *panel : {"ControlBar.wnd:ObserverPlayerListWindow", "ControlBar.wnd:ObserverPlayerInfoWindow", "ControlBar.wnd:BeaconWindow",
				 "ControlBar.wnd:OCLTimerWindow", "ControlBar.wnd:UnderConstructionWindow"})
			document.Set_Window_Flag(panel, WindowFlag::Hidden, true);
	}
};
}
