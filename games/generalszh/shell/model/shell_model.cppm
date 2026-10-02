export module games.generalszh.shell.model.shell_model;
import std;

export import engine.gui.mvvm.observable;

// Model of the front end: what the player has chosen and where the shell
// should go next. Owned by the application, injected into view models;
// the shell host reacts to requests (open a screen, start a game, quit).
export namespace generalszh::shell
{
enum class Side : std::uint8_t
{
	None,
	America,
	China,
	Gla
};

enum class Difficulty : std::uint8_t
{
	Easy,
	Medium,
	Hard
};

enum class Screen : std::uint8_t
{
	MainMenu,
	Options,
	Credits,
	Skirmish,
	OnlineLobby,
	NetworkLobby,
	LoadGame,
	ReplayMenu,
	ChallengeMenu,
	LanGameOptions, // LanGameOptionsMenu (over the LAN lobby)
	DirectConnect,  // NetworkDirectConnect
	Campaign,
	ScoreScreen     // after a game
};

class ShellModel
{
public:
	// The screen shown (the top of the original Shell's screen stack): screens are pushed over
	// the main menu and popped back to what was under them.
	engine::gui::mvvm::Observable<Screen> requestedScreen{Screen::MainMenu};
	void Push(Screen screen)
	{
		m_stack.push_back(requestedScreen.Get());
		requestedScreen.Set(screen);
	}
	void Pop()
	{
		if (m_stack.empty())
			return;
		const Screen under = m_stack.back();
		m_stack.pop_back();
		requestedScreen.Set(under);
	}
	std::size_t Depth() const noexcept { return m_stack.size(); }
	engine::gui::mvvm::Observable<bool> quitRequested{false};
	// The campaign the player set up in the menus.
	engine::gui::mvvm::Observable<Side> campaignSide{Side::None};
	engine::gui::mvvm::Observable<Difficulty> campaignDifficulty{Difficulty::Medium};
	// The options menu, open over the current screen (the original's options layout, brought forward),
	// and how many times the player accepted it (the host saves and applies the options each time).
	engine::gui::mvvm::Observable<bool> optionsOpen{false};
	engine::gui::mvvm::Observable<std::uint32_t> optionsSaved{0};

private:
	std::vector<Screen> m_stack;
};
}
