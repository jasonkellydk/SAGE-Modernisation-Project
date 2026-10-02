export module games.generalszh.hud.generals_powers_view;
import std;

export import games.generalszh.hud.generals_powers;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND.Document;

// Binds Window/GeneralsExpPoints.wnd and ControlBar.wnd's general's button to the GeneralsPowersViewModel (the
// original's ControlBar::init / populatePurchaseScience windows and GeneralsExpPointsSystem): the screen
// (GenExpParent), each rank's science buttons (ButtonRank<1|3|8>Number<n>: shown, enabled, their art drawn by overlay
// states, in colour when had, clicked), the points left (StaticTextRankPointsAvailable), the rank's title
// (StaticTextTitle), the rank's progress (ProgressBarExperience) and ButtonExit; the general's button toggles it and
// blinks. A layout lacking a window simply leaves it out. This table is the whole view.
export namespace generalszh::hud
{
inline std::string ScienceWindowName(std::size_t row, std::size_t slot)
{
	static constexpr std::array<int, 3> rank{1, 3, 8};
	return std::format("GeneralsExpPoints.wnd:ButtonRank{}Number{}", rank[row], slot);
}

class GeneralsPowersView
{
public:
	GeneralsPowersView(Engine::UI::WND::WNDBindings &screen, Engine::UI::WND::WNDDocument &document, Engine::UI::WND::WNDBindings &controlBar,
		GeneralsPowersViewModel &viewModel, std::function<Engine::UI::WND::ImageRef(std::string_view)> resolve,
		std::function<Engine::UI::WND::ImageRef(std::string_view)> resolveBar)
	{
		using Engine::UI::WND::WindowFlag;
		screen.BindVisible("GeneralsExpPoints.wnd:GenExpParent", viewModel.shown);
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t slot = 0; slot < ScienceButtons[row]; ++slot)
			{
				const std::string name = ScienceWindowName(row, slot);
				if (!screen.BindVisible(name, viewModel.slotShown[row][slot]))
					continue;
				screen.BindEnabled(name, viewModel.slotEnabled[row][slot]);
				screen.BindFlag(name, WindowFlag::AlwaysColor, viewModel.slotOwned[row][slot]);
				screen.BindImage(name, viewModel.slotImage[row][slot], resolve);
				screen.BindCommand(name, viewModel.clicked[row][slot]);
				document.Set_Window_Flag(name, WindowFlag::UseOverlayStates, true);
			}
		screen.BindText("GeneralsExpPoints.wnd:StaticTextRankPointsAvailable", viewModel.points);
		screen.BindText("GeneralsExpPoints.wnd:StaticTextTitle", viewModel.title);
		screen.BindProgress("GeneralsExpPoints.wnd:ProgressBarExperience", viewModel.progress);
		screen.BindCommand("GeneralsExpPoints.wnd:ButtonExit", viewModel.exit);
		controlBar.BindEnabled("ControlBar.wnd:ButtonGeneral", viewModel.generalEnabled);
		controlBar.BindCommand("ControlBar.wnd:ButtonGeneral", viewModel.general);
		if (!viewModel.generalImage.Get().empty())
			controlBar.BindImage("ControlBar.wnd:ButtonGeneral", viewModel.generalImage, std::move(resolveBar));
	}
};
}
