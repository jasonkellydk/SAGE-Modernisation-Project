export module games.generalszh.shell.quit_menu.quit_menu_view_model;
import std;

export import engine.gui.mvvm.observable;

export import games.generalszh.shell.dialog.message_box_view_model;

// The in-game menu (the original's QuitMenu.cpp: ToggleQuitMenu, QuitMenuSystem). Escape or the control bar's options
// button opens it over the game: QuitMenu.wnd in a single-player game or a skirmish, QuitNoSave.wnd (no Save/Load) in a
// multiplayer game. A campaign's or challenge's mission and a skirmish pause while it is open. Return (or Escape again)
// closes it; Exit asks (GUI:QuitPopupTitle / QuitPopupMessage) and leaves the game; Restart asks (GUI:RestartConfirmation)
// and starts the match again, or in a multiplayer game Surrender asks (GUI:SurrenderConfirmation) and gives up, not for a
// player already beaten; Options and Save/Load open over it, both off while input is off (a cinematic).
export namespace generalszh::shell
{
enum class QuitMenuMode : std::uint8_t
{
	SinglePlayer, // a campaign's or challenge's mission: "GUI:RestartMission", "GUI:ExitMission"
	Skirmish,     // Restart as it is
	Multiplayer,  // Surrender ("GUI:Surrender"), no Save/Load
};

struct QuitMenuServices
{
	std::function<void()> exit;         // GameLogic::quit(FALSE): back to the shell
	std::function<void()> restart;      // restartMissionMenu
	std::function<void()> surrender;    // surrenderQuitMenu: MSG_SELF_DESTRUCT
	std::function<void()> openOptions;  // the options menu over it
	std::function<void()> openSaveLoad; // PopupSaveLoad over it
	std::function<void(bool)> pause;    // GameLogic::setGamePaused
};

struct QuitMenuTexts
{
	std::u16string quitTitle{u"Quit"}, quitMessage{u"Are you sure you want to quit?"};
	std::u16string restartTitle{u"Restart"}, restartMessage{u"Are you sure you want to restart?"};
	std::u16string surrenderTitle{u"Surrender"}, surrenderMessage{u"Are you sure you want to surrender?"};
	std::u16string restartMission{u"Restart Mission"}, exitMission{u"Exit Mission"}, surrender{u"Surrender"};
	std::u16string restartGame{u"Restart Game"}, exitGame{u"Exit"}; // the layout's own (GUI:RestartGame, GUI:Exit)
};

class QuitMenuViewModel
{
public:
	QuitMenuViewModel(MessageBoxViewModel &messages, QuitMenuServices services, QuitMenuTexts texts = {})
		: m_messages(messages), m_services(std::move(services)), m_texts(std::move(texts))
	{
		returnToGame.SetAction([this] { Close(); });
		exit.SetAction([this] {
			m_messages.Show(MessageBoxYesNo(m_texts.quitTitle, m_texts.quitMessage, [this] {
				Close();
				if (m_services.exit)
					m_services.exit();
			}));
		});
		restart.SetAction([this] {
			if (m_mode == QuitMenuMode::Multiplayer)
				m_messages.Show(MessageBoxYesNo(m_texts.surrenderTitle, m_texts.surrenderMessage, [this] {
					Close();
					if (m_services.surrender)
						m_services.surrender();
				}));
			else
				m_messages.Show(MessageBoxYesNo(m_texts.restartTitle, m_texts.restartMessage, [this] {
					Close();
					if (m_services.restart)
						m_services.restart();
				}));
		});
		options.SetAction([this] {
			if (m_services.openOptions)
				m_services.openOptions();
		});
		saveLoad.SetAction([this] {
			if (m_services.openSaveLoad)
				m_services.openSaveLoad();
		});
	}

	// ToggleQuitMenu opening it: `inputEnabled` (InGameUI::getInputEnabled), `beaten` (the local player no longer
	// active, or its side already won: no surrendering).
	void Open(QuitMenuMode mode, bool inputEnabled, bool beaten)
	{
		m_mode = mode;
		saveLoadShown.Set(mode != QuitMenuMode::Multiplayer);
		saveLoadEnabled.Set(inputEnabled);
		optionsEnabled.Set(inputEnabled);
		restartEnabled.Set(mode != QuitMenuMode::Multiplayer || !beaten);
		restartText.Set(mode == QuitMenuMode::SinglePlayer ? m_texts.restartMission : mode == QuitMenuMode::Multiplayer ? m_texts.surrender : m_texts.restartGame);
		exitText.Set(mode == QuitMenuMode::SinglePlayer ? m_texts.exitMission : m_texts.exitGame);
		if (mode != QuitMenuMode::Multiplayer && m_services.pause)
			m_services.pause(true);
		open.Set(true);
	}

	// ToggleQuitMenu closing it: the game goes on (not paused in a multiplayer game).
	void Close()
	{
		if (!open.Get())
			return;
		open.Set(false);
		if (m_mode != QuitMenuMode::Multiplayer && m_services.pause)
			m_services.pause(false);
	}

	engine::gui::mvvm::Observable<bool> open{false};
	engine::gui::mvvm::Observable<bool> saveLoadShown{true}; // QuitMenu.wnd (else QuitNoSave.wnd)
	engine::gui::mvvm::Observable<bool> saveLoadEnabled{true}, optionsEnabled{true}, restartEnabled{true};
	engine::gui::mvvm::Observable<std::u16string> restartText, exitText;
	engine::gui::mvvm::Command returnToGame, exit, restart, options, saveLoad;

private:
	MessageBoxViewModel &m_messages;
	QuitMenuServices m_services;
	QuitMenuTexts m_texts;
	QuitMenuMode m_mode{QuitMenuMode::SinglePlayer};
};
}
