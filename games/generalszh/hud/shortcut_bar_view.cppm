export module games.generalszh.hud.shortcut_bar_view;
import std;

export import games.generalszh.hud.shortcut_bar;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND.Document;

// Binds a side's general's powers shortcut bar (Window/GenPowersShortcutBar<side>.wnd, as ControlBar::
// initSpecialPowershortcutBar finds its windows) to the ShortcutBarViewModel: the bar (GenPowersShortcutBarParent),
// each button (ButtonCommand1..11 with its frame ButtonParent1..11: shown, enabled, in colour when it cannot be afforded,
// its art drawn by overlay states, a charging power's inverse clock over it, how many are ready as its text, clicked).
// A layout lacking a window simply leaves it out. This table is the whole view.
export namespace generalszh::hud
{
class ShortcutBarView
{
public:
	ShortcutBarView(Engine::UI::WND::WNDBindings &bindings, Engine::UI::WND::WNDDocument &document, std::string_view layout,
		ShortcutBarViewModel &viewModel, std::function<Engine::UI::WND::ImageRef(std::string_view)> resolve, Graphics::Color2D clockColor)
	{
		using Engine::UI::WND::WindowFlag;
		bindings.BindVisible(std::format("{}:GenPowersShortcutBarParent", layout), viewModel.shown);
		for (std::size_t slot = 0; slot < ShortcutButtons; ++slot)
		{
			const std::string name = std::format("{}:ButtonCommand{}", layout, slot + 1);
			if (!bindings.BindVisible(name, viewModel.slotShown[slot]))
				continue;
			bindings.BindVisible(std::format("{}:ButtonParent{}", layout, slot + 1), viewModel.slotShown[slot]);
			bindings.BindEnabled(name, viewModel.slotEnabled[slot]);
			bindings.BindFlag(name, WindowFlag::AlwaysColor, viewModel.slotAlwaysColor[slot]);
			bindings.BindImage(name, viewModel.slotImage[slot], resolve);
			bindings.BindText(name, viewModel.slotCount[slot]);
			bindings.BindClock(name, viewModel.slotClock[slot], clockColor, true);
			bindings.BindCommand(name, viewModel.clicked[slot]);
			// initSpecialPowershortcutBar: WIN_STATUS_USE_OVERLAY_STATES (and the shortcut look).
			document.Set_Window_Flag(name, WindowFlag::UseOverlayStates, true);
		}
	}
};
}
