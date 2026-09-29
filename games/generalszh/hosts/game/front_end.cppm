export module games.generalszh.hosts.game.front_end;
import std;

import engine.platform.events;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import Graphics.Renderer2D;
import games.generalszh.hosts.game.game_client;
import games.generalszh.content.loading.content_loader;
export import games.generalszh.session.setup.game_setup;
export import games.generalszh.shell.quit_menu.quit_menu_view_model;
export import games.generalszh.shell.score.score_screen_view_model;

// The front end over the shell map (the original's Shell with its screens):
// the main menu, the options over it and the credits, each a WND layout
// bound to its headless view model (MVVM); the player's Options.ini; the
// mouse and keyboard routed to the screen showing. Heavy imports stay in
// the implementation so the host's translation units stay small.
export namespace generalszh::host
{
class FrontEnd
{
public:
	struct Setup
	{
		std::filesystem::path userData; // Options.ini's folder
		std::uint32_t width{1024};
		std::uint32_t height{768};
		bool openOptions{false};
		std::string startScreen; // start on this screen (captures, checks): "credits", "replays"
		std::vector<std::string> presses; // buttons clicked in turn, one a step, by window name (captures, checks)
		bool lanLoopback{false};
		bool menuAtOnce{false}; // the main menu shows without waiting for the mouse (captures, checks) // the LAN lobby on 127.0.0.1 only (captures, checks: nothing leaves the machine)
		std::vector<std::string> gamePresses; // buttons of the menus over a game clicked in turn while one shows (checks)
	};

	FrontEnd();
	~FrontEnd();
	FrontEnd(const FrontEnd &) = delete;
	FrontEnd &operator=(const FrontEnd &) = delete;

	// Loads the screens' layouts and the options (applying their volumes to `game`).
	// `loader`: the install's INI (the game setup screens' colours, factions and maps).
	void Load(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, content::ContentLoader &loader,
		GameClient &game, const Setup &setup);
	// Routes one platform event to the screen showing.
	void Handle(const engine::platform::PlatformEvent &event);
	// A frame of real time: the credits scroll, the layouts rebuild when their look changed.
	void Step(float frameSeconds);
	// The shell map shows (it stops, unseen, while the credits run).
	bool ShellMapShown() const noexcept;
	void Draw(Graphics::Renderer2D &renderer);
	bool QuitRequested() const noexcept;
	// A game the player started (the skirmish screen's Start), once; the host loads it and calls EnterGame.
	std::optional<session::setup::GameSetup> TakeGameStart();
	// A campaign the player started (CampaignManager::setCampaign: a side's from the main menu, a general's from the
	// challenge menu, with its PlayerTemplate), at a difficulty (0 easy, 1 normal, 2 hard), once.
	struct CampaignStart
	{
		std::string campaign;
		std::uint8_t difficulty{1};
		std::string playerTemplate;
	};
	std::optional<CampaignStart> TakeCampaignStart();
	// A LAN game that started (LANAPI::OnGameStart), once: its setup, this machine's slot, the host's address and whether
	// this machine hosts it.
	struct LanStart
	{
		session::setup::GameSetup setup;
		int localSlot{0};
		std::uint32_t hostAddress{0};
		bool hosting{false};
	};
	std::optional<LanStart> TakeLanStart();
	// A saved game the player chose to load (the load screen's Load): its file, once.
	std::optional<std::filesystem::path> TakeLoadRequest();
	// The folder saved games go in (GameState::getSaveDirectory).
	std::filesystem::path SaveFolder() const;

	// The menus over a game (the original's QuitMenu and PopupSaveLoad as the shell shows them in a game), as the host
	// answers for them: the quit menu's exit, restart, surrender and pause; which game it is, whether input is on and
	// whether the local player is beaten; the game saved (to a save file, none: a new one) with a description, and a new
	// save's default description.
	struct GameMenus
	{
		shell::QuitMenuServices quit;
		std::function<shell::QuitMenuMode()> mode;
		std::function<bool()> inputEnabled, beaten;
		std::function<void(const std::optional<std::string> &, const std::u16string &)> save;
		std::function<std::u16string()> defaultDescription;
	};
	void SetGameMenus(GameMenus menus);
	// The OPTIONS meta event (Escape, the control bar's options button: ToggleQuitMenu): an open options menu or save
	// popup closes, else the quit menu opens or closes.
	void ToggleQuitMenu();
	// A menu shows over the game (its input is the menu's).
	bool GameMenuShown() const noexcept;
	// The score screen after a game (the shell pushes ScoreScreen.wnd), and what the player chose on it, once: OK (the
	// main menu shows again) or Continue.
	void ShowScoreScreen(const shell::ScoreScreenSetup &setup);
	shell::ScoreChoice TakeScoreChoice();
	// The shell gives way to the game: no menus drawn or fed input from now on.
	void EnterGame();
	// The game is over (GameLogic::exitGame): the shell shows again, on the screen the game was started from.
	void LeaveGame();
	bool InGame() const noexcept;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
