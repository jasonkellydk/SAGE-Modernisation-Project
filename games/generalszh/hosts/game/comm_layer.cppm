export module games.generalszh.hosts.game.comm_layer;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Input;
import Engine.UI.WND.Bindings;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import engine.core.text.utf;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.match.resources.match_outcome;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.hosts.game.game_client;
import games.generalszh.hud.comm_views;
import games.generalszh.session.setup.game_setup;
import games.generalszh.commands.game_commands;
import games.generalszh.presentation.hud.resources.military_caption;

// The in-game chat and diplomacy windows in a match (InGameChat.wnd and Diplomacy.wnd over the game): their view models
// fed from the match's slots and the game's state, the chat line sent through the command stream and what arrives shown
// in the messages, the briefing history kept from the military subtitles and popup messages; the keyboard while the
// chat box is open, the mouse over either window.
export namespace generalszh::host
{
class CommLayer
{
public:
	bool Load(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, GameClient &game, std::uint32_t width,
		std::uint32_t height, float fontScale, std::string &error)
	{
		m_game = &game;
		m_strings = &strings;
		m_width = static_cast<int>(width);
		if (!m_chatMenu.Load(files, "Window/InGameChat.wnd", strings, width, height, error, fontScale) ||
			!m_diplomacyMenu.Load(files, "Window/Diplomacy.wnd", strings, width, height, error, fontScale))
			return false;
		const auto text = [this](std::string_view label) { return Localized(*m_strings, label); };
		m_chat = std::make_unique<hud::InGameChatViewModel>(text, [this](std::u16string line, hud::InGameChatType type) { Send(line, type); });
		m_diplomacy = std::make_unique<hud::DiplomacyViewModel>(text, [this] { return Slots(); });
		m_chatBindings.emplace(m_chatMenu.Document());
		m_diplomacyBindings.emplace(m_diplomacyMenu.Document());
		hud::BindInGameChatView(*m_chatBindings, *m_chat);
		hud::BindDiplomacyView(*m_diplomacyBindings, *m_diplomacy);
		if (const Engine::UI::WND::WNDWindow *parent = m_diplomacyMenu.Document().Find_Window(DiplomacyParent))
		{
			m_diplomacyAuthoredTop = parent->screen_region.top;
			m_diplomacy->SetLayout(static_cast<int>(static_cast<float>(parent->screen_region.top) * m_diplomacyMenu.ScaleY()), m_width);
		}
		m_chatMenu.Refresh();
		m_diplomacyMenu.Refresh();
		m_loaded = true;
		return true;
	}

	// A new match (InGameUI::reset: ResetInGameChat, ResetDiplomacy and the briefing history cleared): its slots, the local
	// one, whether it is a multiplayer game (a skirmish or network game: TheRecorder->isMultiplayer), a network one (LAN),
	// and a replay.
	void SetMatch(const session::setup::GameSetup &setup, int localSlot, bool multiplayer, bool network, bool replay)
	{
		m_setup = setup;
		m_localSlot = localSlot;
		m_multiplayer = multiplayer;
		m_network = network;
		m_replay = replay;
		m_seenCaption.clear();
		m_seenPopup = 0;
		if (!m_loaded)
			return;
		m_chat->Reset();
		m_diplomacy->Hide(true);
		m_diplomacy->SetNetwork(network);
		m_diplomacy->Briefing(u"", u"", true);
	}

	// MSG_META_CHAT_ALLIES / CHAT_EVERYONE (CommandXlat): in a network game not played back, for a player still playing
	// (and anyone for EVERYONE when it is a sandbox: m_netMinPlayers); ToggleInGameChat then SetInGameChatType.
	void Chat(hud::InGameChatType type)
	{
		if (!m_loaded || !m_network || m_replay)
			return;
		const bool active = LocalActive();
		if (!active)
			return;
		m_chat->Toggle(ChatContext());
		m_chat->SetType(type, active);
		Refresh();
	}

	// MSG_META_DIPLOMACY and the control bar's communicator button: ToggleDiplomacy(FALSE) in a game.
	void ToggleDiplomacy(bool quitMenuVisible)
	{
		if (!m_loaded)
			return;
		m_diplomacy->Toggle(false, hud::DiplomacyContext{InputEnabled(), quitMenuVisible, m_multiplayer, true});
		Refresh();
	}

	// The keyboard while the chat box is open (InGameChatInput): Esc closes it (HideInGameChat); typed text and Backspace
	// edit it, Enter sends it (GEM_EDIT_DONE). (The diplomacy window refuses the keyboard: GWM_INPUT_FOCUS.) True: taken.
	bool Escape()
	{
		if (!m_loaded || !m_chat->Active())
			return false;
		m_chat->Hide();
		Refresh();
		return true;
	}
	bool KeyboardTaken() const noexcept { return m_loaded && m_chat->Active(); }
	void Type(const std::u16string &typed)
	{
		if (!KeyboardTaken())
			return;
		Focus();
		if (auto input = m_chatPointer.Type(m_chatMenu.Document(), typed))
			m_chatBindings->Apply(*input);
		Refresh();
	}
	void Key(Engine::UI::WND::WNDPointer::EditKey key)
	{
		if (!KeyboardTaken())
			return;
		Focus();
		if (auto input = m_chatPointer.Key(m_chatMenu.Document(), key))
			m_chatBindings->Apply(*input);
		// The Enter that sent the line is CHAT_EVERYONE's key too (CommandMap.ini): its toggle comes right after and is the
		// one ToggleInGameChat's justHid swallows; SetInGameChatType(EVERYONE) still follows.
		if (key == Engine::UI::WND::WNDPointer::EditKey::Enter && !m_chat->Active() && m_network && !m_replay && LocalActive())
		{
			m_chat->Toggle(ChatContext());
			m_chat->SetType(hud::InGameChatType::Everyone, true);
		}
		Refresh();
	}

	// The mouse this frame (screen pixels): true while one of the windows takes it.
	bool Point(float x, float y, std::uint8_t pressed, std::uint8_t released, std::uint32_t milliseconds)
	{
		if (!m_loaded)
			return false;
		bool over = false;
		if (m_diplomacy->Shown())
			over = PointAt(m_diplomacyMenu, *m_diplomacyBindings, m_diplomacyPointer, x, y, pressed, released, milliseconds) || over;
		if (m_chat->Active())
			over = PointAt(m_chatMenu, *m_chatBindings, m_chatPointer, x, y, pressed, released, milliseconds) || over;
		Refresh();
		return over;
	}

	// Each frame: the windows' slides, the chat lines that arrived, the briefing history.
	void Update(double seconds)
	{
		if (!m_loaded || m_game == nullptr)
			return;
		m_diplomacy->Update(seconds);
		const int top = m_diplomacy->top.Get();
		if (top != m_diplomacyTop && m_diplomacyMenu.ScaleY() > 0.0f)
		{
			m_diplomacyTop = top;
			const Engine::UI::WND::WNDWindow *parent = m_diplomacyMenu.Document().Find_Window(DiplomacyParent);
			if (parent != nullptr)
			{
				m_diplomacyMenu.Document().Move_Window(DiplomacyParent, parent->screen_region.left,
					static_cast<int>(std::lround(static_cast<float>(top) / m_diplomacyMenu.ScaleY())));
				m_diplomacyMenu.Refresh();
			}
		}
		// VictoryConditions::update: a player defeated fills the diplomacy rows again (PopulateInGameDiplomacyPopup).
		if (session::SessionView *view = m_game->View())
			if (const auto *outcome = view->World().FindResource<engine::gameplay::MatchOutcome>())
			{
				std::size_t defeated = 0;
				for (const auto &standing : outcome->players)
					defeated += standing.defeated ? 1u : 0u;
				if (defeated != m_defeated)
				{
					m_defeated = defeated;
					m_diplomacy->Refresh();
				}
			}
		ReceiveChat();
		NoteBriefing();
		Refresh();
	}

	void Draw(Graphics::Renderer2D &renderer)
	{
		if (!m_loaded)
			return;
		if (m_diplomacy->Shown())
			m_diplomacyMenu.Draw(renderer);
		if (m_chat->Active())
			m_chatMenu.Draw(renderer);
	}

	hud::InGameChatViewModel *ChatViewModel() noexcept { return m_chat.get(); }
	hud::DiplomacyViewModel *DiplomacyViewModel() noexcept { return m_diplomacy.get(); }

private:
	static constexpr std::string_view DiplomacyParent = "Diplomacy.wnd:Parent";
	static constexpr std::string_view ChatEntry = "InGameChat.wnd:TextEntryChat";

	static bool PointAt(ShellMenu &menu, Engine::UI::WND::WNDBindings &bindings, Engine::UI::WND::WNDPointer &pointer, float x, float y,
		std::uint8_t pressed, std::uint8_t released, std::uint32_t milliseconds)
	{
		auto &document = menu.Document();
		const auto [lx, ly] = menu.ToLayout(x, y);
		const bool over = Engine::UI::WND::Hit_Test(document, lx, ly).has_value();
		if (auto input = pointer.Move(document, lx, ly))
			bindings.Apply(*input);
		if ((pressed & 1u) != 0 && over)
			if (auto press = pointer.Press(document, lx, ly))
				bindings.Apply(*press);
		if ((released & 1u) != 0)
			if (auto release = pointer.Release(document, lx, ly, milliseconds))
				bindings.Apply(*release);
		return over;
	}

	// winSetFocus(chatTextEntry): the box has the keyboard.
	void Focus()
	{
		if (!m_focused)
		{
			const Engine::UI::WND::WNDWindow *entry = m_chatMenu.Document().Find_Window(ChatEntry);
			if (entry != nullptr)
			{
				const float x = static_cast<float>(entry->screen_region.left + 1), y = static_cast<float>(entry->screen_region.top + 1);
				m_chatPointer.Move(m_chatMenu.Document(), x, y);
				m_chatPointer.Press(m_chatMenu.Document(), x, y); // a text entry pressed takes the keyboard
			}
			m_focused = true;
		}
	}

	void Refresh()
	{
		if (m_chatBindings && (m_chatBindings->TakeDirty() || m_chatPointer.TakeLook()))
			m_chatMenu.Refresh();
		if (m_diplomacyBindings && (m_diplomacyBindings->TakeDirty() || m_diplomacyPointer.TakeLook()))
			m_diplomacyMenu.Refresh();
		if (!m_chat || !m_chat->Active())
			m_focused = false;
	}

	hud::ChatContext ChatContext() const
	{
		return hud::ChatContext{m_replay, m_network, false, LocalActive()};
	}

	bool InputEnabled() const { return m_game != nullptr && !m_game->Settings().inputDisabled; }

	// Player::isPlayerActive for the local player: not an observer, not defeated.
	bool LocalActive() const
	{
		if (m_game == nullptr || m_game->LocalPlayerObserver())
			return false;
		const auto player = m_game->LocalPlayer();
		session::SessionView *view = m_game->View();
		const auto *outcome = view != nullptr ? view->World().FindResource<engine::gameplay::MatchOutcome>() : nullptr;
		return !(player && outcome != nullptr && outcome->Eliminated(*player));
	}

	// The match's player for slot `slot` ("player<slot>"), from the score board's rows (one per player, in order).
	std::optional<std::uint32_t> PlayerOfSlot(int slot, const std::vector<shell::ScorePlayer> &board) const
	{
		const std::string name = "player" + std::to_string(slot);
		for (std::size_t index = 0; index < board.size(); ++index)
			if (board[index].playerName == name)
				return static_cast<std::uint32_t>(index);
		return std::nullopt;
	}

	// ToggleInGameChat's send: the slots it is for (ChatRecipients), the line through the command stream.
	void Send(const std::u16string &line, hud::InGameChatType type)
	{
		if (m_game == nullptr)
			return;
		session::SessionView *view = m_game->View();
		const std::vector<shell::ScorePlayer> board = m_game->ScoreBoard();
		const auto *relationships = view != nullptr ? view->World().FindResource<engine::gameplay::Relationships>() : nullptr;
		const auto local = m_game->LocalPlayer();
		std::array<hud::ChatSlot, 8> slots{};
		for (int slot = 0; slot < 8; ++slot)
		{
			const auto player = PlayerOfSlot(slot, board);
			if (!player)
				continue;
			hud::ChatSlot &each = slots[static_cast<std::size_t>(slot)];
			each.player = true;
			each.local = local && *local == *player;
			each.muted = m_diplomacy->Muted(slot);
			each.mutualAllies = local && relationships != nullptr &&
				relationships->Between(*player, *local) == engine::gameplay::Relationship::Allies &&
				relationships->Between(*local, *player) == engine::gameplay::Relationship::Allies;
		}
		const std::uint32_t mask = hud::ChatRecipients(type, slots, local.has_value());
		m_game->Submit(commands::Chat{engine::core::text::ToUtf8(line), mask});
	}

	// ConnectionManager::processChat for each line that arrived (none shown in a replay: chat is not recorded there).
	void ReceiveChat()
	{
		const std::vector<GameClient::ChatArrived> arrived = m_game->TakeChat();
		if (arrived.empty() || m_replay || !m_network)
			return;
		session::SessionView *view = m_game->View();
		const std::vector<shell::ScorePlayer> board = m_game->ScoreBoard();
		const auto *outcome = view != nullptr ? view->World().FindResource<engine::gameplay::MatchOutcome>() : nullptr;
		for (const GameClient::ChatArrived &line : arrived)
		{
			if (line.player >= board.size())
				continue;
			const shell::ScorePlayer &sender = board[line.player];
			int slot = -1;
			if (sender.playerName.starts_with("player") && sender.playerName.size() == 7)
				slot = sender.playerName[6] - '0';
			hud::ChatArrival chat;
			chat.fromSlot = slot;
			chat.senderName = slot >= 0 && slot < 8 ? m_setup.slots[static_cast<std::size_t>(slot)].name : sender.name;
			chat.text = engine::core::text::FromUtf8(line.text);
			chat.slots = line.slots;
			chat.senderActive = !sender.observer && !(outcome != nullptr && outcome->Eliminated(line.player));
			chat.senderMuted = m_diplomacy->Muted(slot);
			chat.senderColor = sender.color;
			if (const auto shown = hud::ReceiveChat(chat, m_localSlot, LocalActive()))
			{
				if (shown->color)
					m_game->ShowMessage(shown->text, *shown->color);
				else
					m_game->ShowMessage(shown->text);
				if (shown->sound)
					m_game->PlayInterfaceSound("GUICommunicatorIncoming");
			}
		}
	}

	// UpdateDiplomacyBriefingText: a military subtitle's and a popup message's text join the history as they come.
	void NoteBriefing()
	{
		if (session::SessionView *view = m_game->View())
			if (const auto *caption = view->World().FindResource<presentation::MilitaryCaption>(); caption != nullptr && caption->shown &&
				caption->text != m_seenCaption)
			{
				m_seenCaption = caption->text;
				m_diplomacy->Briefing(caption->text, caption->text, false);
			}
		if (const auto *popup = m_game->Popup(); popup != nullptr && popup->serial != m_seenPopup)
		{
			m_seenPopup = popup->serial;
			m_diplomacy->Briefing(popup->text, popup->text, false);
		}
	}

	// PopulateInGameDiplomacyPopup's slots (none outside a skirmish or network game: no TheGameInfo).
	std::optional<std::vector<hud::DiplomacySlot>> Slots() const
	{
		if (!m_multiplayer || m_game == nullptr)
			return std::nullopt;
		session::SessionView *view = m_game->View();
		const std::vector<shell::ScorePlayer> board = m_game->ScoreBoard();
		const auto *outcome = view != nullptr ? view->World().FindResource<engine::gameplay::MatchOutcome>() : nullptr;
		const auto &settings = m_game->MultiplayerSettings();
		const session::setup::GameSlot &localSlot = m_setup.slots[static_cast<std::size_t>(std::clamp(m_localSlot, 0, 7))];
		std::vector<hud::DiplomacySlot> slots(8);
		for (int index = 0; index < 8; ++index)
		{
			const session::setup::GameSlot &slot = m_setup.slots[static_cast<std::size_t>(index)];
			hud::DiplomacySlot &each = slots[static_cast<std::size_t>(index)];
			each.occupied = slot.Occupied();
			if (!each.occupied)
				continue;
			each.human = slot.Human();
			each.ai = slot.AI();
			each.local = index == m_localSlot;
			each.team = slot.team;
			const auto player = PlayerOfSlot(index, board);
			const shell::ScorePlayer *row = player ? &board[*player] : nullptr;
			each.name = row != nullptr ? row->name : slot.name;
			each.observer = slot.Observer();
			// hasSinglePlayerBeenDefeated: an observer has nothing; a player counts while the match has not seen it fall.
			each.alive = player && outcome != nullptr && outcome->Of(*player) != nullptr && !outcome->Eliminated(*player);
			// isSlotLocalAlly: itself, its team, or anyone when the local player only watches.
			const bool localAlly = index == m_localSlot || (slot.team >= 0 && slot.team == localSlot.team) || localSlot.Observer();
			// getApparentPlayerTemplateDisplayName / getApparentColor.
			if (slot.Observer())
				each.side = Localized(*m_strings, "GUI:Observer");
			else if (settings.showRandomPlayerTemplate && slot.playerTemplate == session::setup::Random && !localAlly)
				each.side = Localized(*m_strings, "GUI:Random");
			else if (const content::PlayerTemplateInfo *faction = player && view != nullptr ? m_game->PlayerTemplates().Find(view->PlayerTemplateName(*player)) : nullptr)
				each.side = Localized(*m_strings, faction->displayName);
			else
				each.side = Localized(*m_strings, "GUI:Random");
			if (slot.Observer() || (settings.showRandomColor && slot.color < 0 && !localAlly))
				each.color = 0xFFFFFFFFu; // MultiplayerSettings' observer and random colours
			else
				each.color = row != nullptr ? row->color : 0xFFFFFFFFu;
		}
		return slots;
	}

	GameClient *m_game{nullptr};
	const engine::localization::StringTable *m_strings{nullptr};
	ShellMenu m_chatMenu, m_diplomacyMenu;
	// The view models outlive the bindings (they unsubscribe from their observables when they go).
	std::unique_ptr<hud::InGameChatViewModel> m_chat;
	std::unique_ptr<hud::DiplomacyViewModel> m_diplomacy;
	std::optional<Engine::UI::WND::WNDBindings> m_chatBindings, m_diplomacyBindings;
	Engine::UI::WND::WNDPointer m_chatPointer, m_diplomacyPointer;
	session::setup::GameSetup m_setup;
	int m_localSlot{0};
	int m_width{800};
	int m_diplomacyAuthoredTop{0};
	int m_diplomacyTop{std::numeric_limits<int>::min()};
	std::size_t m_defeated{0};
	std::u16string m_seenCaption;
	std::uint32_t m_seenPopup{0};
	bool m_multiplayer{false}, m_network{false}, m_replay{false};
	bool m_focused{false};
	bool m_loaded{false};
};
}
