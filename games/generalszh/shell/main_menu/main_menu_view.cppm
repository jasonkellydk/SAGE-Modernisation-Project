export module games.generalszh.shell.main_menu.main_menu_view;
import std;

export import games.generalszh.shell.main_menu.main_menu_view_model;
export import Engine.UI.WND.Bindings;
export import Engine.UI.WND.Transitions;
import Engine.UI.WND.Document;

// Binds Window/Menus/MainMenu.wnd to the MainMenuViewModel: the five
// drop-down panels, the side-specific campaign buttons and faction art, and
// every button's command. This table is the whole view: no menu logic here.
export namespace generalszh::shell
{
// The main menu's transitions (MainMenu.cpp's button handlers): a panel change plays the panel
// going out ("Back" groups) while the new one waits hidden behind it, so both panels show until it
// is done; leaving for another screen plays that screen's way out first.
class MainMenuMotion
{
public:
	MainMenuMotion(MainMenuViewModel &viewModel, Engine::UI::WND::WNDTransitions &transitions) : m_viewModel(viewModel), m_transitions(transitions)
	{
		m_shown = viewModel.panel.Get();
		visiblePanels.Set(Bit(m_shown));
		const auto id = viewModel.panel.Subscribe([this](MainMenuPanel next) { Change(next); });
		m_release = [this, id] { m_viewModel.panel.Unsubscribe(id); };
	}
	~MainMenuMotion() { m_release(); }
	MainMenuMotion(const MainMenuMotion &) = delete;
	MainMenuMotion &operator=(const MainMenuMotion &) = delete;

	// Each MainMenuPanel's bit, set while that panel shows.
	engine::gui::mvvm::Observable<std::uint32_t> visiblePanels{0};

	// The transitions done: only the new panel shows.
	void Step()
	{
		if (m_transitions.IsFinished() && visiblePanels.Get() != Bit(m_viewModel.panel.Get()))
			visiblePanels.Set(Bit(m_viewModel.panel.Get()));
	}

	// Nothing of the menu shows yet (the first launch, until the player stirs).
	void Hide() { visiblePanels.Set(0); }

	// The main menu comes (back) into view: its buttons flash in (MainMenuInit's first launch; LogoFade on return).
	void Enter(bool first)
	{
		visiblePanels.Set(Bit(m_viewModel.panel.Get()));
		if (first)
		{
			m_transitions.SetGroup("MainMenuFade", true);
			m_transitions.SetGroup("MainMenuDefaultMenu");
		}
		else
			m_transitions.SetGroup("MainMenuDefaultMenuLogoFade");
	}

	// What plays as the main menu gives way to `screen` (reversed); empty for none.
	std::string Leave(Screen screen) const
	{
		switch (screen)
		{
		case Screen::Skirmish: return "MainMenuSinglePlayerMenuBackSkirmish";
		case Screen::ChallengeMenu: return "MainMenuDifficultyMenuTraining"; // setupGameStart: reversed
		case Screen::NetworkLobby:
		case Screen::OnlineLobby: return "MainMenuMultiPlayerMenuTransitionToNext";
		case Screen::LoadGame:
		case Screen::ReplayMenu: return "MainMenuLoadReplayMenuBackTransition";
		case Screen::Credits: return "MainMenuDefaultMenu";
		default: return {};
		}
	}

private:
	static std::uint32_t Bit(MainMenuPanel panel) { return 1u << static_cast<unsigned>(panel); }

	static const char *SideName(Side side, bool usa)
	{
		switch (side)
		{
		case Side::America: return usa ? "USA" : "US";
		case Side::China: return "China";
		case Side::Gla: return "GLA";
		default: return "Training";
		}
	}

	// The original's remove / reverse / setGroup for each change of panel.
	void Change(MainMenuPanel next)
	{
		const MainMenuPanel from = m_shown;
		m_shown = next;
		if (from == next)
			return;
		std::string removed, reversed, shown;
		const Side side = m_viewModel.highlightedSide.Get() != Side::None ? m_viewModel.highlightedSide.Get() : m_side;
		if (from == MainMenuPanel::Main)
		{
			removed = "MainMenuDefaultMenu";
			reversed = "MainMenuDefaultMenuBack";
			shown = next == MainMenuPanel::SinglePlayer ? "MainMenuSinglePlayerMenu"
				: next == MainMenuPanel::Multiplayer ? "MainMenuMultiPlayerMenu" : "MainMenuLoadReplayMenu";
		}
		else if (next == MainMenuPanel::Main)
		{
			removed = from == MainMenuPanel::SinglePlayer ? "MainMenuSinglePlayerMenu"
				: from == MainMenuPanel::Multiplayer ? "MainMenuMultiPlayerMenu" : "MainMenuLoadReplayMenu";
			reversed = from == MainMenuPanel::SinglePlayer ? "MainMenuSinglePlayerMenuBack"
				: from == MainMenuPanel::Multiplayer ? "MainMenuMultiPlayerMenuReverse" : "MainMenuLoadReplayMenuBack";
			shown = "MainMenuDefaultMenu";
		}
		else if (from == MainMenuPanel::SinglePlayer && next == MainMenuPanel::Difficulty)
		{
			// The side just picked (none: the challenge's training panel).
			m_side = m_viewModel.highlightedSide.Get();
			reversed = std::string("MainMenuSinglePlayerMenuBack") + SideName(m_side, false);
			shown = std::string("MainMenuDifficultyMenu") + SideName(m_side, false);
		}
		else if (from == MainMenuPanel::Difficulty && next == MainMenuPanel::SinglePlayer)
		{
			reversed = std::string("MainMenuDifficultyMenu") + SideName(m_side, false) + "Back";
			shown = std::string("MainMenuSinglePlayer") + SideName(m_side, true) + "MenuFromDiff";
		}
		if (!removed.empty())
			m_transitions.Remove(removed);
		if (!reversed.empty())
			m_transitions.Reverse(reversed);
		if (!shown.empty())
			m_transitions.SetGroup(shown);
		visiblePanels.Set(Bit(from) | Bit(next));
	}

	MainMenuViewModel &m_viewModel;
	Engine::UI::WND::WNDTransitions &m_transitions;
	MainMenuPanel m_shown{MainMenuPanel::Main};
	Side m_side{Side::None};
	std::function<void()> m_release;
};

// `motion`: the panels show as the transitions have them (else as the view model says).
inline void BindMainMenuView(Engine::UI::WND::WNDBindings &bindings, MainMenuViewModel &viewModel, MainMenuMotion *motion = nullptr)
{
	using Engine::UI::WND::WNDBindings;
	const std::array<std::pair<const char *, MainMenuPanel>, 5> panels{{{"MainMenu.wnd:MapBorder2", MainMenuPanel::Main},
		{"MainMenu.wnd:MapBorder", MainMenuPanel::SinglePlayer}, {"MainMenu.wnd:MapBorder1", MainMenuPanel::Multiplayer},
		{"MainMenu.wnd:MapBorder3", MainMenuPanel::LoadReplay}, {"MainMenu.wnd:MapBorder4", MainMenuPanel::Difficulty}}};
	for (const auto &[window, panel] : panels)
		if (motion != nullptr)
			bindings.BindVisible(window, motion->visiblePanels, [bit = 1u << static_cast<unsigned>(panel)](std::uint32_t shown) { return (shown & bit) != 0; });
		else
			bindings.BindVisible(window, viewModel.panel, [panel](MainMenuPanel current) { return current == panel; });

	// Per-side recent save / load buttons (showSelectiveButtons in the original).
	const auto sideIs = [](Side wanted) { return [wanted](Side current) { return current == wanted; }; };
	for (const auto &[window, side] : std::array<std::pair<std::string_view, Side>, 6>{{
			 {"MainMenu.wnd:ButtonUSARecentSave", Side::America}, {"MainMenu.wnd:ButtonUSALoadGame", Side::America},
			 {"MainMenu.wnd:ButtonGLARecentSave", Side::Gla}, {"MainMenu.wnd:ButtonGLALoadGame", Side::Gla},
			 {"MainMenu.wnd:ButtonChinaRecentSave", Side::China}, {"MainMenu.wnd:ButtonChinaLoadGame", Side::China}}})
		bindings.BindVisible(window, viewModel.highlightedSide, sideIs(side));

	// Faction art: hidden at rest (initialHide in the original); shown on
	// hover by the transition port.
	constexpr std::array<std::string_view, 16> factionArt{"MainMenu.wnd:WinFactionUS", "MainMenu.wnd:WinFactionUSMedium",
		"MainMenu.wnd:WinFactionUSSmall", "MainMenu.wnd:WinFactionGLA", "MainMenu.wnd:WinFactionGLAMedium", "MainMenu.wnd:WinFactionGLASmall",
		"MainMenu.wnd:WinFactionChina", "MainMenu.wnd:WinFactionChinaMedium", "MainMenu.wnd:WinFactionChinaSmall",
		"MainMenu.wnd:WinFactionTraining", "MainMenu.wnd:WinFactionTrainingMedium", "MainMenu.wnd:WinFactionTrainingSmall",
		"MainMenu.wnd:WinFactionSkirmish", "MainMenu.wnd:WinFactionSkirmishMedium", "MainMenu.wnd:WinFactionSkirmishSmall",
		"MainMenu.wnd:WinGrowMarker"};
	for (const std::string_view window : factionArt)
		bindings.BindVisible(window, viewModel.highlightedSide, [](Side) { return false; });

	bindings.BindCommand("MainMenu.wnd:ButtonSinglePlayer", viewModel.singlePlayer);
	bindings.BindCommand("MainMenu.wnd:ButtonMultiplayer", viewModel.multiplayer);
	bindings.BindCommand("MainMenu.wnd:ButtonReplay", viewModel.replays);
	for (const std::string_view back : {"MainMenu.wnd:ButtonSingleBack", "MainMenu.wnd:ButtonMultiBack",
			 "MainMenu.wnd:ButtonLoadReplayBack", "MainMenu.wnd:ButtonDiffBack"})
		bindings.BindCommand(back, viewModel.back);
	bindings.BindCommand("MainMenu.wnd:ButtonUSA", viewModel.chooseAmerica);
	bindings.BindCommand("MainMenu.wnd:ButtonChina", viewModel.chooseChina);
	bindings.BindCommand("MainMenu.wnd:ButtonGLA", viewModel.chooseGla);
	bindings.BindCommand("MainMenu.wnd:ButtonEasy", viewModel.easy);
	bindings.BindCommand("MainMenu.wnd:ButtonMedium", viewModel.medium);
	bindings.BindCommand("MainMenu.wnd:ButtonHard", viewModel.hard);
	bindings.BindCommand("MainMenu.wnd:ButtonSkirmish", viewModel.skirmish);
	bindings.BindCommand("MainMenu.wnd:ButtonOnline", viewModel.online);
	bindings.BindCommand("MainMenu.wnd:ButtonNetwork", viewModel.network);
	bindings.BindCommand("MainMenu.wnd:ButtonChallenge", viewModel.challenge);
	bindings.BindCommand("MainMenu.wnd:ButtonLoadGame", viewModel.loadGame);
	bindings.BindCommand("MainMenu.wnd:ButtonLoadReplay", viewModel.loadReplay);
	bindings.BindCommand("MainMenu.wnd:ButtonOptions", viewModel.options);
	bindings.BindCommand("MainMenu.wnd:ButtonCredits", viewModel.credits);
	bindings.BindCommand("MainMenu.wnd:ButtonExit", viewModel.exit);
}
}
