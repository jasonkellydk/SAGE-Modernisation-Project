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
		// MainMenuSystem's GBM_SELECTED: nothing once a button has left the menu (buttonPushed); the buttons that start
		// the menu's own transitions take no press while one they started plays (dontAllowTransitions, until
		// MainMenuUpdate sees the transitions finished); the side, skirmish and challenge buttons none once one of them
		// was taken (campaignSelected, until the difficulty panel's back).
		singlePlayer.SetAction([this] { Navigate(false, [this] { panel.Set(MainMenuPanel::SinglePlayer); }); });
		multiplayer.SetAction([this] { Navigate(false, [this] { panel.Set(MainMenuPanel::Multiplayer); }); });
		replays.SetAction([this] { Navigate(false, [this] { panel.Set(MainMenuPanel::LoadReplay); }); });
		back.SetAction([this] { Back(); });
		chooseAmerica.SetAction([this] { ChooseSide(Side::America); });
		chooseChina.SetAction([this] { ChooseSide(Side::China); });
		chooseGla.SetAction([this] { ChooseSide(Side::Gla); });
		easy.SetAction([this] { StartCampaign(Difficulty::Easy); });
		medium.SetAction([this] { StartCampaign(Difficulty::Medium); });
		hard.SetAction([this] { StartCampaign(Difficulty::Hard); });
		skirmish.SetAction([this] {
			if (m_pushed || m_campaignSelected || m_holdTransitions)
				return;
			m_pushed = true;
			m_campaignSelected = true;
			m_model.Push(Screen::Skirmish);
		});
		online.SetAction([this] { Navigate(true, [this] { m_model.Push(Screen::OnlineLobby); }); });
		network.SetAction([this] { Navigate(true, [this] { m_model.Push(Screen::NetworkLobby); }); });
		// Generals' Challenge: the difficulty first (the training side's), then the challenge menu (launchChallengeMenu).
		challenge.SetAction([this] {
			if (m_pushed || m_campaignSelected || m_holdTransitions)
				return;
			m_campaignSelected = true;
			highlightedSide.Set(Side::None);
			m_challenge = true;
			panel.Set(MainMenuPanel::Difficulty);
		});
		loadGame.SetAction([this] { Navigate(true, [this] { m_model.Push(Screen::LoadGame); }); });
		loadReplay.SetAction([this] { Navigate(true, [this] { m_model.Push(Screen::ReplayMenu); }); });
		// The options menu opens over the main menu (MainMenu.cpp: getOptionsLayout, bringForward); it leaves
		// buttonPushed as it was.
		options.SetAction([this] {
			if (m_pushed || m_holdTransitions)
				return;
			m_holdTransitions = true;
			m_model.optionsOpen.Set(true);
		});
		credits.SetAction([this] { Navigate(true, [this] { m_model.Push(Screen::Credits); }); });
		exit.SetAction([this] {
			if (!m_pushed)
				m_model.quitRequested.Set(true);
		});
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

	// MainMenuUpdate: once the transitions have finished, the buttons may start new ones (dontAllowTransitions off).
	void TransitionsFinished() noexcept { m_holdTransitions = false; }
	// MainMenuInit: the menu (shown again) takes presses and its side buttons afresh.
	void Entered() noexcept
	{
		m_pushed = false;
		m_campaignSelected = false;
	}
	// GBM_MOUSE_ENTERING: a side, skirmish or challenge button pointed at starts its faction art's transition only
	// while none of them was taken and no button's transition plays.
	bool FactionHoverAllowed() const noexcept { return !m_campaignSelected && !m_holdTransitions; }

private:
	// A button that starts the menu's transitions: ignored while one plays or after the menu was left; `leaves` the
	// menu for good (buttonPushed) or stays on it.
	template<typename Action>
	void Navigate(bool leaves, Action &&action)
	{
		if (m_pushed || m_holdTransitions)
			return;
		m_holdTransitions = true;
		m_pushed = leaves;
		action();
	}

	void Back()
	{
		if (m_pushed)
			return;
		switch (panel.Get())
		{
		case MainMenuPanel::Difficulty:
			// buttonDiffBack.
			if (m_holdTransitions)
				return;
			m_holdTransitions = true;
			m_campaignSelected = false;
			highlightedSide.Set(Side::None);
			m_challenge = false;
			panel.Set(MainMenuPanel::SinglePlayer);
			break;
		case MainMenuPanel::SinglePlayer:
			// buttonSingleBack: not once a side was taken.
			if (m_campaignSelected || m_holdTransitions)
				return;
			m_holdTransitions = true;
			panel.Set(MainMenuPanel::Main);
			break;
		case MainMenuPanel::Multiplayer:
		case MainMenuPanel::LoadReplay:
			// buttonMultiBack, buttonLoadReplayBack.
			Navigate(false, [this] { panel.Set(MainMenuPanel::Main); });
			break;
		default:
			break;
		}
	}

	void ChooseSide(Side side)
	{
		if (m_pushed || m_campaignSelected || m_holdTransitions)
			return;
		m_campaignSelected = true;
		highlightedSide.Set(side);
		m_challenge = false;
		m_model.campaignSide.Set(side);
		panel.Set(MainMenuPanel::Difficulty);
	}

	void StartCampaign(Difficulty difficulty)
	{
		if (m_pushed || m_holdTransitions)
			return;
		m_model.campaignDifficulty.Set(difficulty);
		m_model.Push(m_challenge ? Screen::ChallengeMenu : Screen::Campaign);
	}

	ShellModel &m_model;
	bool m_challenge{false}; // the difficulty panel leads to the challenge menu
	bool m_holdTransitions{false};  // dontAllowTransitions
	bool m_campaignSelected{false}; // campaignSelected
	bool m_pushed{false};           // buttonPushed
};
}
