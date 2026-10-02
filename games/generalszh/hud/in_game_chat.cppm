export module games.generalszh.hud.in_game_chat;
import std;

export import engine.gui.mvvm.observable;

// The in-game chat (the original's InGameChat.cpp over InGameChat.wnd, and ConnectionManager::sendChat / processChat): a
// network game's chat line typed in its box and sent to everyone (but the muted), to the player's allies or to the
// player alone; what arrives shown to the player in its sender's colour with the GUICommunicatorIncoming sound.
// Headless: the host sends the line and shows what arrives.
export namespace generalszh::hud
{
enum class InGameChatType : std::uint8_t
{
	Everyone, // INGAME_CHAT_EVERYONE
	Allies,   // INGAME_CHAT_ALLIES
	Players,  // INGAME_CHAT_PLAYERS
};

// Where the chat is asked for.
struct ChatContext
{
	bool replay{false};          // TheGameLogic->isInReplayGame
	bool multiplayer{false};     // TheGameInfo->isMultiPlayer (a network game)
	bool quitMenuVisible{false}; // TheInGameUI->isQuitMenuVisible
	bool localActive{true};      // the local player isPlayerActive (not an observer, not defeated)
};

// A game slot as the chat sees it when sending (ToggleInGameChat).
struct ChatSlot
{
	bool player{false};       // a player "player<slot>" exists
	bool local{false};        // it is the local player
	bool muted{false};        // GameSlot::isMuted (the local player muted it)
	bool mutualAllies{false}; // it and the local player count each other ALLIES
};

// ToggleInGameChat's playerMask: each slot with a player (and a local player at all): EVERYONE, unless muted; ALLIES,
// when allied both ways or the local player itself; PLAYERS, the local player alone.
inline std::uint32_t ChatRecipients(InGameChatType type, std::span<const ChatSlot> slots, bool haveLocal)
{
	std::uint32_t mask = 0;
	if (!haveLocal)
		return mask;
	for (std::size_t slot = 0; slot < slots.size() && slot < 8; ++slot)
	{
		const ChatSlot &each = slots[slot];
		if (!each.player)
			continue;
		bool include = false;
		switch (type)
		{
		case InGameChatType::Everyone: include = !each.muted; break;
		case InGameChatType::Allies: include = each.mutualAllies || each.local; break;
		case InGameChatType::Players: include = each.local; break;
		}
		if (include)
			mask |= 1u << slot;
	}
	return mask;
}

// A chat line that arrived, as processChat shows it.
struct ChatArrival
{
	int fromSlot{-1};            // the sender's slot (0..7)
	std::u16string senderName;   // its slot's name (the local user's own for itself)
	std::u16string text;
	std::uint32_t slots{0};      // whom it is for
	bool senderHasPlayer{true};  // getPlayerFromSlotIndex found its player
	bool senderActive{true};     // its player isPlayerActive
	bool senderMuted{false};     // its slot is muted here
	std::uint32_t senderColor{0}; // its player's colour, 0xAARRGGBB
};

struct ChatShown
{
	std::u16string text;
	std::optional<std::uint32_t> color; // none: the messages' own colours (TheInGameUI->message)
	bool sound{false};                  // GUICommunicatorIncoming
};

// ConnectionManager::processChat on this machine (local slot `localSlot`, its player active or not): "[name] text";
// with no player for the sender it shows plainly whoever it was for; else only when it is for this slot, and seen only
// by an observer or from a player still playing, and not from a muted slot: in the sender's colour, with the sound.
inline std::optional<ChatShown> ReceiveChat(const ChatArrival &chat, int localSlot, bool localActive)
{
	if (chat.fromSlot < 0 || chat.fromSlot >= 8)
		return std::nullopt;
	ChatShown shown;
	shown.text = u"[" + chat.senderName + u"] " + chat.text;
	if (!chat.senderHasPlayer)
		return shown;
	const bool fromObserver = !chat.senderActive;
	const bool amIObserver = !localActive;
	const bool canSee = (amIObserver || !fromObserver) && !chat.senderMuted;
	if (localSlot < 0 || localSlot >= 8 || (chat.slots & (1u << localSlot)) == 0 || !canSee)
		return std::nullopt;
	shown.color = chat.senderColor;
	shown.sound = true;
	return shown;
}

class InGameChatViewModel
{
public:
	// `text`: a label's text (TheGameText->fetch); `send`: the line said, to whom (TheNetwork->sendChat).
	InGameChatViewModel(std::function<std::u16string(std::string_view)> text, std::function<void(std::u16string, InGameChatType)> send)
		: m_text(std::move(text)), m_send(std::move(send))
	{
		done.SetAction([this] { Toggle(m_context); });
		clear.SetAction([this] {
			entry.Set(std::u16string{});
			m_saved.clear();
		});
	}

	InGameChatViewModel(const InGameChatViewModel &) = delete;
	InGameChatViewModel &operator=(const InGameChatViewModel &) = delete;

	// ToggleInGameChat: just after the window hid (a line was just sent) the next toggle does nothing; nothing in a
	// replay or outside a network game; shown, the line typed goes to whom the chat type says (trimmed; nothing when
	// empty, nor a slash command), the box is emptied and the window hides; hidden (or never made), it shows.
	void Toggle(const ChatContext &context)
	{
		m_context = context;
		if (m_justHid)
		{
			m_justHid = false;
			return;
		}
		if (context.replay || !context.multiplayer)
			return;
		if (!m_created || !shown.Get())
		{
			Show(context);
			return;
		}
		std::u16string message = Trim(entry.Get());
		if (!message.empty() && !SlashCommand(message) && m_send)
			m_send(message, m_type);
		entry.Set(std::u16string{});
		Hide();
		m_justHid = true;
	}

	// SetInGameChatType: the chat type and, once the window was made, its label (Chat:Everyone, or Chat:Observers for a
	// player no longer playing; Chat:Allies; Chat:Players).
	void SetType(InGameChatType type, bool localActive)
	{
		m_type = type;
		if (!m_created)
			return;
		switch (type)
		{
		case InGameChatType::Everyone: typeLabel.Set(Text(localActive ? "Chat:Everyone" : "Chat:Observers")); break;
		case InGameChatType::Allies: typeLabel.Set(Text("Chat:Allies")); break;
		case InGameChatType::Players: typeLabel.Set(Text("Chat:Players")); break;
		}
	}

	// HideInGameChat (Esc in the box, InGameChatInput): the line typed kept for the next time, the window hidden.
	void Hide()
	{
		if (!m_created)
			return;
		m_saved = entry.Get();
		shown.Set(false);
	}

	// ResetInGameChat (a new game): the window gone, nothing kept.
	void Reset()
	{
		m_created = false;
		m_justHid = false;
		m_saved.clear();
		entry.Set(std::u16string{});
		typeLabel.Set(std::u16string{});
		shown.Set(false);
	}

	// IsInGameChatActive: the window shows (it has the keyboard).
	bool Active() const noexcept { return m_created && shown.Get(); }
	InGameChatType Type() const noexcept { return m_type; }

	engine::gui::mvvm::Observable<bool> shown{false};
	engine::gui::mvvm::Observable<std::u16string> entry;     // InGameChat.wnd:TextEntryChat
	engine::gui::mvvm::Observable<std::u16string> typeLabel; // InGameChat.wnd:StaticTextChatType
	engine::gui::mvvm::Command done;                         // GEM_EDIT_DONE (Enter in the box): ToggleInGameChat
	engine::gui::mvvm::Command clear;                        // InGameChat.wnd:ButtonClear

private:
	// ShowInGameChat: not in a replay nor under the quit menu; the window shows with the line kept (once made, empty the
	// first time), the chat type set to EVERYONE.
	void Show(const ChatContext &context)
	{
		if (context.replay || context.quitMenuVisible)
			return;
		if (m_created)
		{
			entry.Set(m_saved);
			m_saved.clear();
		}
		else
		{
			m_created = true;
			entry.Set(std::u16string{});
		}
		shown.Set(true);
		SetType(InGameChatType::Everyone, context.localActive);
	}

	// handleInGameSlashCommands: "/host" (the online game's hosting status, a GameSpy diagnostic) is a command, not said.
	static bool SlashCommand(const std::u16string &message)
	{
		if (message.empty() || message.front() != u'/')
			return false;
		std::u16string token;
		for (std::size_t at = 1; at < message.size() && message[at] != u' ' && message[at] != u'\t'; ++at)
			token.push_back(message[at] >= u'A' && message[at] <= u'Z' ? static_cast<char16_t>(message[at] - u'A' + u'a') : message[at]);
		return token == u"host";
	}

	static std::u16string Trim(std::u16string text)
	{
		const auto space = [](char16_t c) { return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n'; };
		while (!text.empty() && space(text.back()))
			text.pop_back();
		std::size_t first = 0;
		while (first < text.size() && space(text[first]))
			++first;
		return text.substr(first);
	}

	std::u16string Text(std::string_view label) const { return m_text ? m_text(label) : std::u16string(label.begin(), label.end()); }

	std::function<std::u16string(std::string_view)> m_text;
	std::function<void(std::u16string, InGameChatType)> m_send;
	ChatContext m_context;
	InGameChatType m_type{InGameChatType::Everyone};
	bool m_created{false};
	bool m_justHid{false};
	std::u16string m_saved;
};
}
