export module games.generalszh.shell.load_screen.shell_game_load_screen_view_model;
import std;

export import engine.gui.mvvm.observable;

// The shell's load screen (LoadScreen.cpp ShellGameLoadScreen), shown as the shell map, a replay or a saved game loads:
// Menus/ShellGameLoadScreen.wnd with its progress bar at what the load reports. The game's first load (the shell map
// after the intro) shows the title screen ("TitleScreen") with the legal text in place of the layout's own picture.
export namespace generalszh::shell
{
class ShellGameLoadScreenViewModel
{
public:
	// ShellGameLoadScreen::init: the bar at 0; on the first load (`firstLoad`, the static firstLoad with
	// GameLODManager::didMemPass) the title screen and StaticTextLegal. The bar is hidden while init runs and shown
	// at its end, so it shows once the screen is up.
	void Init(bool firstLoad)
	{
		progress.Set(0);
		titleImage.Set(firstLoad ? std::string("TitleScreen") : std::string());
		legalShown.Set(firstLoad);
		progressShown.Set(true);
	}

	// ShellGameLoadScreen::update: the bar at the load's percent.
	void Update(int percent) { progress.Set(percent); }

	engine::gui::mvvm::Observable<int> progress{0};
	engine::gui::mvvm::Observable<bool> progressShown{false};
	engine::gui::mvvm::Observable<std::string> titleImage; // empty: the layout's own picture (LoadPageHuge)
	engine::gui::mvvm::Observable<bool> legalShown{false};
};
}
