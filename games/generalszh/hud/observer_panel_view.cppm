export module games.generalszh.hud.observer_panel_view;
import std;

export import games.generalszh.hud.observer_panel;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND.Document;

// Binds ControlBar.wnd's observer windows to the ObserverPanelViewModel (ControlBar::initObserverControls): the list
// (ObserverPlayerListWindow: ButtonPlayer<n> with its art drawn by overlay states and its tooltip, StaticTextPlayer<n> in
// the player's colour), the info window (ObserverPlayerInfoWindow: StaticTextNumberOfUnits, ...OfBuildings,
// ...OfUnitsKilled, ...OfUnitsLost, StaticTextPlayerName in its colour, WinFlag, WinGeneralPortrait, ButtonCancel) and
// ButtonIdleWorker, hidden for an observer. This table is the whole view.
export namespace generalszh::hud
{
inline void BindObserverPanelView(Engine::UI::WND::WNDBindings &bindings, Engine::UI::WND::WNDDocument &document, ObserverPanelViewModel &viewModel,
	std::function<Engine::UI::WND::ImageRef(std::string_view)> resolve)
{
	using Engine::UI::WND::WindowFlag;
	const std::string bar = "ControlBar.wnd:";
	bindings.BindVisible(bar + "ObserverPlayerListWindow", viewModel.listShown);
	bindings.BindVisible(bar + "ObserverPlayerInfoWindow", viewModel.infoShown);
	for (std::size_t button = 0; button < ObserverButtons; ++button)
	{
		const std::string number = std::to_string(button);
		const std::string name = bar + "ButtonPlayer" + number;
		bindings.BindVisible(name, viewModel.buttonShown[button]);
		bindings.BindImage(name, viewModel.buttonImage[button], resolve);
		bindings.BindCommand(name, viewModel.playerClicked[button]);
		document.Set_Window_Flag(name, WindowFlag::UseOverlayStates, true);
		const std::string text = bar + "StaticTextPlayer" + number;
		bindings.BindVisible(text, viewModel.buttonShown[button]);
		bindings.BindText(text, viewModel.label[button]);
		bindings.BindTextColor(text, viewModel.labelColor[button]);
	}
	bindings.BindText(bar + "StaticTextNumberOfUnits", viewModel.units);
	bindings.BindText(bar + "StaticTextNumberOfBuildings", viewModel.buildings);
	bindings.BindText(bar + "StaticTextNumberOfUnitsKilled", viewModel.unitsKilled);
	bindings.BindText(bar + "StaticTextNumberOfUnitsLost", viewModel.unitsLost);
	bindings.BindText(bar + "StaticTextPlayerName", viewModel.name);
	bindings.BindTextColor(bar + "StaticTextPlayerName", viewModel.nameColor);
	bindings.BindImage(bar + "WinFlag", viewModel.flag, std::move(resolve));
	bindings.BindVisible(bar + "WinGeneralPortrait", viewModel.portraitShown);
	bindings.BindCommand(bar + "ButtonCancel", viewModel.cancel);
	bindings.BindVisible(bar + "ButtonIdleWorker", viewModel.idleWorkerShown);
}
}
