export module games.generalszh.shell.main_menu.main_menu_view_model;
import std;

export import games.generalszh.shell.model.shell_model;

// Main menu logic (the original MainMenu.cpp behaviour, without windows):
// which drop-down panel is open, which side's campaign buttons show, and
// what each button does. Views bind to it; it knows nothing about WND.
export namespace generalszh::shell
{
enum class MainMenuPanel : std::uint8_t
{
	None,
	Main,
	SinglePlayer,
	Multiplayer,
	LoadReplay,
	Difficulty
};

class MainMenuViewModel
{
public:
	explicit MainMenuViewModel(ShellModel &model) : m_model(model)
	{
		using engine::gui::mvvm::Command;
		singlePlayer.SetAction([this] { panel.Set(MainMenuPanel::SinglePlayer); });
		multiplayer.SetAction([this] { panel.Set(MainMenuPanel::Multiplayer); });
		replays.SetAction([this] { panel.Set(MainMenuPanel::LoadReplay); });
		back.SetAction([this] { Back(); });
		chooseAmerica.SetAction([this] { ChooseSide(Side::America); });
		chooseChina.SetAction([this] { ChooseSide(Side::China); });
		chooseGla.SetAction([this] { ChooseSide(Side::Gla); });
		easy.SetAction([this] { StartCampaign(Difficulty::Easy); });
		medium.SetAction([this] { StartCampaign(Difficulty::Medium); });
		hard.SetAction([this] { StartCampaign(Difficulty::Hard); });
		skirmish.SetAction([this] { m_model.Push(Screen::Skirmish); });
		online.SetAction([this] { m_model.Push(Screen::OnlineLobby); });
		network.SetAction([this] { m_model.Push(Screen::NetworkLobby); });
		// Generals' Challenge: the difficulty first (the training side's), then the challenge menu (launchChallengeMenu).
		challenge.SetAction([this] {
			highlightedSide.Set(Side::None);
			m_challenge = true;
			panel.Set(MainMenuPanel::Difficulty);
		});
		loadGame.SetAction([this] { m_model.Push(Screen::LoadGame); });
		loadReplay.SetAction([this] { m_model.Push(Screen::ReplayMenu); });
		// The options menu opens over the main menu (MainMenu.cpp: getOptionsLayout, bringForward).
		options.SetAction([this] { m_model.optionsOpen.Set(true); });
		credits.SetAction([this] { m_model.Push(Screen::Credits); });
		exit.SetAction([this] { m_model.quitRequested.Set(true); });
	}

	MainMenuViewModel(const MainMenuViewModel &) = delete;
	MainMenuViewModel &operator=(const MainMenuViewModel &) = delete;

	// The open drop-down; the menu rests on the main panel.
	engine::gui::mvvm::Observable<MainMenuPanel> panel{MainMenuPanel::Main};
	// The side whose recent-save / load buttons (and faction art) show.
	engine::gui::mvvm::Observable<Side> highlightedSide{Side::None};

	engine::gui::mvvm::Command singlePlayer, multiplayer, replays, back;
	engine::gui::mvvm::Command chooseAmerica, chooseChina, chooseGla;
	engine::gui::mvvm::Command easy, medium, hard;
	engine::gui::mvvm::Command skirmish, online, network, challenge, loadGame, loadReplay, options, credits, exit;

private:
	void Back()
	{
		switch (panel.Get())
		{
		case MainMenuPanel::Difficulty:
			highlightedSide.Set(Side::None);
			m_challenge = false;
			panel.Set(MainMenuPanel::SinglePlayer);
			break;
		case MainMenuPanel::SinglePlayer:
		case MainMenuPanel::Multiplayer:
		case MainMenuPanel::LoadReplay:
			panel.Set(MainMenuPanel::Main);
			break;
		default:
			break;
		}
	}

	void ChooseSide(Side side)
	{
		highlightedSide.Set(side);
		m_challenge = false;
		m_model.campaignSide.Set(side);
		panel.Set(MainMenuPanel::Difficulty);
	}

	void StartCampaign(Difficulty difficulty)
	{
		m_model.campaignDifficulty.Set(difficulty);
		m_model.Push(m_challenge ? Screen::ChallengeMenu : Screen::Campaign);
	}

	ShellModel &m_model;
	bool m_challenge{false}; // the difficulty panel leads to the challenge menu
};
}
