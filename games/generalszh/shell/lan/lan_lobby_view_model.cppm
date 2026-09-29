export module games.generalszh.shell.lan.lan_lobby_view_model;
import std;

export import games.generalszh.shell.model.shell_model;
export import games.generalszh.shell.dialog.message_box_view_model;
export import games.generalszh.shell.game_setup.setup_catalog;
export import games.generalszh.network.lan.lan_lobby;
export import engine.config.adapters.preferences.preferences_file;
import engine.core.text.quoted_printable;

// The LAN lobby screen and direct connect (the original's LanLobbyMenu.cpp and
// NetworkDirectConnect.cpp, without windows): the player's name, who else is
// in the lobby, the games being set up (the host's name; one under way in grey
// brackets), the selected game's details, the chat, and hosting or joining.
// Network.ini keeps the name and the remote addresses.
export namespace generalszh::shell
{
namespace lan = generalszh::network::lan;

struct LanServices
{
	std::function<std::u16string(std::string_view)> text;    // a label's text (Generals.csf)
	std::function<void()> savePreferences;                   // Network.ini written
	std::function<std::u16string()> machineName;             // the name when Network.ini has none
	std::function<std::u16string(const std::string &)> mapName; // a map's shown name (else its file's)
	std::function<std::string(std::uint32_t)> addressText;   // "a.b.c.d"
	// The game starts (LANAPI::OnGameStart: MSG_NEW_GAME, GAME_LAN): its setup, this machine's slot, the host's address
	// and whether this machine hosts it. None: the game cannot be played here.
	std::function<void(const generalszh::session::setup::GameSetup &, int, std::uint32_t, bool)> start;
};

// A line of the chat window, in its colour (0xRRGGBBAA).
struct ChatLine
{
	std::u16string text;
	std::uint32_t rgba{0xFFFFFFFF};
	bool operator==(const ChatLine &) const = default;
};

// The selected game's details (GameInfoWindow): its host, its map and who is in it.
struct GameDetails
{
	bool shown{false};
	std::u16string host, map;
	std::vector<ChatLine> players;
	bool operator==(const GameDetails &) const = default;
};

inline constexpr std::uint32_t ChatSystemColor = 0xFFFFFFFF, EmoteLocalColor = 0x80FFFFFF, EmoteRemoteColor = 0xFF00FFFF, InProgressColor = 0x808080FF;
inline constexpr std::size_t ChatHistory = 100;

// LANPreferences::getUserName: Network.ini's UserName, else the machine's name; at most 12 characters.
inline std::u16string LanUserName(const engine::config::Preferences &preferences, const std::function<std::u16string()> &machineName)
{
	std::u16string name;
	if (const auto stored = preferences.Find("UserName"))
		name = engine::core::text::DecodeQuotedPrintableWide(*stored);
	while (!name.empty() && name.front() == u' ')
		name.erase(name.begin());
	while (!name.empty() && name.back() == u' ')
		name.pop_back();
	if (name.empty() && machineName)
		name = machineName();
	return name.substr(0, lan::PlayerNameLength);
}

class LanLobbyViewModel
{
public:
	LanLobbyViewModel(ShellModel &model, MessageBoxViewModel &messages, const SetupCatalog &catalog, engine::config::Preferences &preferences,
		LanServices services)
		: m_model(model), m_messages(messages), m_catalog(catalog), m_preferences(preferences), m_services(std::move(services))
	{
		back.SetAction([this] { Leave(); });
		escape.SetAction([this] { Leave(); });
		host.SetAction([this] {
			if (m_lobby != nullptr)
				m_lobby->RequestGameCreate(false, PreferredMap());
		});
		join.SetAction([this] { Join(); });
		clear.SetAction([this] { playerName.Set({}); });
		send.SetAction([this] { Send(); });
		directConnect.SetAction([this] {
			if (m_lobby != nullptr)
				m_lobby->RequestLobbyLeave();
			m_model.Push(Screen::DirectConnect);
		});
		const auto id = playerName.Subscribe([this](const std::u16string &text) {
			if (!m_updating)
				NameEdited(text);
		});
		m_releases.push_back([this, id] { playerName.Unsubscribe(id); });
		const auto selectedId = selectedGame.Subscribe([this](int) { ShowDetails(); });
		m_releases.push_back([this, selectedId] { selectedGame.Unsubscribe(selectedId); });
	}
	~LanLobbyViewModel()
	{
		for (auto &release : m_releases)
			release();
	}
	LanLobbyViewModel(const LanLobbyViewModel &) = delete;
	LanLobbyViewModel &operator=(const LanLobbyViewModel &) = delete;

	// LanLobbyMenuInit with the lobby the host made: the name, the lists empty, who is here asked.
	void Open(lan::LanLobby &lobby)
	{
		m_lobby = &lobby;
		m_defaultName = LanUserName(m_preferences, m_services.machineName);
		m_updating = true;
		playerName.Set(m_defaultName);
		chatEntry.Set({});
		m_updating = false;
		chat.Set({});
		lobby.RequestSetName(m_defaultName);
		lobby.RequestLocations();
		Refresh();
	}

	// LanLobbyMenuShutdown: the name kept.
	void Close()
	{
		m_preferences.Set("UserName", engine::core::text::EncodeQuotedPrintable(std::u16string_view(playerName.Get())));
		if (m_services.savePreferences)
			m_services.savePreferences();
		if (m_lobby != nullptr)
			m_lobby->RequestLobbyLeave();
	}

	// The lobby's events.
	void PlayersChanged() { Refresh(); }
	void GamesChanged() { Refresh(); }
	void GameCreated(lan::Result result)
	{
		if (result == lan::Result::Ok)
		{
			m_model.Push(Screen::LanGameOptions);
			return;
		}
		AddChat({Text(result == lan::Result::GameExists ? "LAN:ErrorGameExists" : result == lan::Result::Busy ? "LAN:ErrorBusy" : "LAN:ErrorUnknown"),
			ChatSystemColor});
	}
	// OnGameJoin: in, the joiner says what it would like; else why not.
	void GameJoined(lan::Result result)
	{
		if (result == lan::Result::Ok)
		{
			m_model.Push(Screen::LanGameOptions);
			m_lobby->RequestGameOptions("PlayerTemplate=" + std::to_string(PreferredNumber("PlayerTemplate")));
			m_lobby->RequestGameOptions("Color=" + std::to_string(PreferredNumber("Color")));
			m_lobby->RequestGameOptions("NAT=1");
			return;
		}
		if (result != lan::Result::Busy)
			m_messages.Show(MessageBoxOk(Text("LAN:JoinFailed"), ErrorText(result)));
	}
	void Chat(const std::u16string &name, std::uint32_t ip, const std::u16string &text, lan::ChatType type)
	{
		AddChat(Line(name, ip, text, type));
	}

	// The lobby's lists as the windows show them.
	engine::gui::mvvm::Observable<std::u16string> playerName, chatEntry;
	engine::gui::mvvm::Observable<std::vector<std::u16string>> players, games;
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> playerColors, gameColors;
	engine::gui::mvvm::Observable<int> selectedPlayer{-1}, selectedGame{-1};
	engine::gui::mvvm::Observable<std::vector<ChatLine>> chat;
	engine::gui::mvvm::Observable<GameDetails> details;
	engine::gui::mvvm::Command back, escape, host, join, clear, send, directConnect;

	// LANAPI::getErrorStringFromReturnType.
	std::u16string ErrorText(lan::Result result) const
	{
		switch (result)
		{
		case lan::Result::Ok: return Text("LAN:OK");
		case lan::Result::Timeout: return Text("LAN:ErrorTimeout");
		case lan::Result::GameFull: return Text("LAN:ErrorGameFull");
		case lan::Result::DuplicateName: return Text("LAN:ErrorDuplicateName");
		case lan::Result::CrcMismatch: return Text("LAN:ErrorCRCMismatch");
		case lan::Result::GameStarted: return Text("LAN:ErrorGameStarted");
		case lan::Result::GameExists: return Text("LAN:ErrorGameExists");
		case lan::Result::GameGone: return Text("LAN:ErrorGameGone");
		case lan::Result::Busy: return Text("LAN:ErrorBusy");
		case lan::Result::SerialDupe: return Text("WOL:ChatErrorSerialDup");
		default: return Text("LAN:ErrorUnknown");
		}
	}

	// A chat line as OnChat colours it: a system line white, an emote cyan (ours) or magenta,
	// a player's line "[name] text" in their colour in the game (else white).
	ChatLine Line(const std::u16string &name, std::uint32_t ip, const std::u16string &text, lan::ChatType type) const
	{
		if (type == lan::ChatType::System)
			return {text, ChatSystemColor};
		if (type == lan::ChatType::Emote)
			return {name + u' ' + text, m_lobby != nullptr && ip == m_lobby->LocalIp() ? EmoteLocalColor : EmoteRemoteColor};
		std::uint32_t color = 0xFFFFFFFF;
		if (m_lobby != nullptr && !m_lobby->InLobby())
			if (const lan::LobbyGame *game = m_lobby->CurrentGame())
				if (const int slot = game->SlotNamed(name); slot >= 0)
					if (const int c = game->setup.slots[static_cast<std::size_t>(slot)].color; c >= 0 && static_cast<std::size_t>(c) < m_catalog.colors.size())
						color = m_catalog.colors[static_cast<std::size_t>(c)].rgba;
		return {u"[" + name + u"] " + text, color};
	}

	void AddChat(ChatLine line)
	{
		std::vector<ChatLine> lines = chat.Get();
		lines.push_back(std::move(line));
		if (lines.size() > ChatHistory)
			lines.erase(lines.begin());
		chat.Set(std::move(lines));
	}

	std::u16string Text(std::string_view label) const
	{
		if (m_services.text)
			if (std::u16string found = m_services.text(label); !found.empty())
				return found;
		return std::u16string(label.begin(), label.end());
	}

	// LANPreferences::getPreferredMap: Network.ini's Map when it is a multiplayer map, else the first.
	std::string PreferredMap() const
	{
		if (const auto map = m_preferences.Find("Map"))
			if (const SetupMap *found = m_catalog.Find(engine::core::text::DecodeQuotedPrintable(*map)); found != nullptr && found->multiplayer)
				return found->file;
		return m_catalog.DefaultMap();
	}
	int PreferredNumber(const char *key) const { return static_cast<int>(m_preferences.Number(key, -1)); }

private:
	void Leave()
	{
		Close();
		m_model.Pop();
	}

	// ButtonJoin: the selected game, else say none is.
	void Join()
	{
		const int row = selectedGame.Get();
		if (m_lobby == nullptr || row < 0 || static_cast<std::size_t>(row) >= m_gameRows.size())
		{
			AddChat({Text("LAN:ErrorNoGameSelected"), ChatSystemColor});
			return;
		}
		m_lobby->RequestGameJoin(m_gameRows[static_cast<std::size_t>(row)]);
	}

	void Send()
	{
		std::u16string text = chatEntry.Get();
		while (!text.empty() && (text.front() == u' ' || text.front() == u'\t'))
			text.erase(text.begin());
		while (!text.empty() && (text.back() == u' ' || text.back() == u'\t'))
			text.pop_back();
		m_updating = true;
		chatEntry.Set({});
		m_updating = false;
		if (!text.empty() && m_lobby != nullptr)
			m_lobby->RequestPlayerChat(text);
	}

	// GEM_UPDATE_TEXT on the name: no leading spaces, 12 characters, no trailing , : ;.
	void NameEdited(std::u16string text)
	{
		while (!text.empty() && (text.front() == u' ' || text.front() == u'\t'))
			text.erase(text.begin());
		text = text.substr(0, lan::PlayerNameLength);
		for (const char16_t trailing : {u',', u':', u';'})
			if (!text.empty() && text.back() == trailing)
				text.pop_back();
		if (m_lobby != nullptr)
			m_lobby->RequestSetName(text.empty() ? m_defaultName : text);
		if (text != playerName.Get())
		{
			m_updating = true;
			playerName.Set(text);
			m_updating = false;
		}
	}

	void Refresh()
	{
		if (m_lobby == nullptr)
			return;
		std::vector<std::u16string> names;
		for (const lan::LobbyPlayer &player : m_lobby->Players())
			names.push_back(player.name);
		playerColors.Set(std::vector<std::uint32_t>(names.size(), 0xFFFFFFFF));
		players.Set(std::move(names));
		// The games by the list's order, each as its host's name.
		const std::u16string chosen = selectedGame.Get() >= 0 && static_cast<std::size_t>(selectedGame.Get()) < m_gameRows.size()
			? m_gameRows[static_cast<std::size_t>(selectedGame.Get())] : std::u16string{};
		m_gameRows.clear();
		std::vector<std::u16string> shown;
		std::vector<std::uint32_t> colors;
		for (const lan::LobbyGame &game : m_lobby->Games())
		{
			const std::u16string &hostName = game.setup.slots[0].name;
			shown.push_back(game.inProgress ? u"[" + hostName + u"]" : hostName);
			colors.push_back(game.inProgress ? InProgressColor : 0xFFFFFFFF);
			m_gameRows.push_back(game.name);
		}
		gameColors.Set(std::move(colors));
		games.Set(std::move(shown));
		int row = -1;
		for (std::size_t index = 0; index < m_gameRows.size(); ++index)
			if (m_gameRows[index] == chosen)
				row = static_cast<int>(index);
		selectedGame.Set(row);
		ShowDetails();
	}

	// RefreshGameInfoWindow: the host, the map and each occupied slot in its colour.
	void ShowDetails()
	{
		GameDetails shown;
		const int row = selectedGame.Get();
		if (m_lobby != nullptr && row >= 0 && static_cast<std::size_t>(row) < m_gameRows.size())
			for (const lan::LobbyGame &game : m_lobby->Games())
			{
				if (game.name != m_gameRows[static_cast<std::size_t>(row)])
					continue;
				shown.shown = true;
				shown.host = game.setup.slots[0].name;
				const SetupMap *map = m_catalog.Find(game.setup.map);
				shown.map = map != nullptr ? map->name : std::u16string(game.setup.map.begin(), game.setup.map.end());
				for (const auto &slot : game.setup.slots)
				{
					if (!slot.Occupied())
						continue;
					const std::u16string name = slot.Human() ? slot.name
						: Text(slot.state == session::setup::SlotState::EasyAI ? "GUI:EasyAI" : slot.state == session::setup::SlotState::MediumAI ? "GUI:MediumAI" : "GUI:HardAI");
					const std::uint32_t color = slot.color >= 0 && static_cast<std::size_t>(slot.color) < m_catalog.colors.size()
						? m_catalog.colors[static_cast<std::size_t>(slot.color)].rgba : 0xFFFFFFFF;
					shown.players.push_back({name, color});
				}
			}
		details.Set(std::move(shown));
	}

	ShellModel &m_model;
	MessageBoxViewModel &m_messages;
	const SetupCatalog &m_catalog;
	engine::config::Preferences &m_preferences;
	LanServices m_services;
	lan::LanLobby *m_lobby{nullptr};
	std::u16string m_defaultName;
	std::vector<std::u16string> m_gameRows; // each row's game
	bool m_updating{false};
	std::vector<std::function<void()>> m_releases;
};

// NetworkDirectConnect: host or join a game at an address, remembered in Network.ini.
class DirectConnectViewModel
{
public:
	DirectConnectViewModel(ShellModel &model, engine::config::Preferences &preferences, LanServices services)
		: m_model(model), m_preferences(preferences), m_services(std::move(services))
	{
		back.SetAction([this] {
			SaveName();
			m_model.Pop();
		});
		escape.SetAction([this] { back.Execute(); });
		host.SetAction([this] {
			SaveName();
			if (m_lobby == nullptr)
				return;
			m_lobby->RequestSetName(playerName.Get().substr(0, lan::PlayerNameLength));
			m_lobby->RequestGameCreate(true, PreferredMap ? PreferredMap() : std::string{});
		});
		join.SetAction([this] { Join(); });
		// Typing an address drops the pick from the list.
		const auto id = remoteText.Subscribe([this](const std::u16string &) {
			if (!m_populating)
				remote.Set(-1);
		});
		m_release = [this, id] { remoteText.Unsubscribe(id); };
	}
	~DirectConnectViewModel()
	{
		if (m_release)
			m_release();
	}
	DirectConnectViewModel(const DirectConnectViewModel &) = delete;
	DirectConnectViewModel &operator=(const DirectConnectViewModel &) = delete;

	std::function<std::string()> PreferredMap; // the lobby's preferred map

	// NetworkDirectConnectInit: the name, the remembered addresses, ours shown.
	void Open(lan::LanLobby &lobby)
	{
		m_lobby = &lobby;
		std::u16string name = LanUserName(m_preferences, m_services.machineName);
		if (name.empty())
			name = m_services.text ? m_services.text("GUI:Player") : u"Player";
		playerName.Set(name);
		Populate();
		const std::string local = m_services.addressText ? m_services.addressText(lobby.LocalIp()) : std::string{};
		localAddress.Set(std::u16string(local.begin(), local.end()));
		lobby.RequestLobbyLeave();
	}

	engine::gui::mvvm::Observable<std::u16string> playerName, localAddress;
	engine::gui::mvvm::Observable<std::vector<std::u16string>> remoteItems;
	engine::gui::mvvm::Observable<int> remote{-1};
	engine::gui::mvvm::Observable<std::u16string> remoteText; // what the address box holds (typed or picked)
	engine::gui::mvvm::Command back, escape, host, join;

	// "a.b.c.d" (up to a '(' or ':'), as the original's sscanf("%d.%d.%d.%d").
	static std::optional<std::uint32_t> ParseAddress(std::u16string_view text)
	{
		std::string narrow;
		for (const char16_t c : text)
		{
			if (c == u'(' || c == u':')
				break;
			narrow.push_back(static_cast<char>(c));
		}
		int a = 0, b = 0, c = 0, d = 0;
		if (std::sscanf(narrow.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
			return std::nullopt;
		for (const int part : {a, b, c, d})
			if (part < 0 || part > 255)
				return std::nullopt;
		return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
	}

private:
	void SaveName()
	{
		m_preferences.Set("UserName", engine::core::text::EncodeQuotedPrintable(std::u16string_view(playerName.Get())));
		if (m_services.savePreferences)
			m_services.savePreferences();
	}

	// PopulateRemoteIPComboBox: "ip(name)" for RemoteIP0.. (each "ip:QPname").
	void Populate()
	{
		std::vector<std::u16string> items;
		const int count = static_cast<int>(m_preferences.Number("NumRemoteIPs", 0));
		for (int index = 0; index < count; ++index)
		{
			const auto entry = m_preferences.Find("RemoteIP" + std::to_string(index));
			if (!entry)
				continue;
			const std::string text(*entry);
			const auto colon = text.find(':');
			const std::string ip = text.substr(0, colon);
			std::u16string shown(ip.begin(), ip.end());
			if (colon != std::string::npos)
				shown += u"(" + engine::core::text::DecodeQuotedPrintableWide(text.substr(colon + 1)) + u")";
			items.push_back(std::move(shown));
		}
		m_populating = true;
		remoteText.Set(items.empty() ? std::u16string{} : items.front());
		m_populating = false;
		remoteItems.Set(std::move(items));
		remote.Set(remoteItems.Get().empty() ? -1 : 0);
	}

	// UpdateRemoteIPList: the address used first, the rest after it.
	void Remember(const std::u16string &text, std::uint32_t address)
	{
		std::vector<std::string> entries;
		const auto encode = [&](std::u16string_view shown) {
			std::string ip;
			std::u16string name;
			const auto open = shown.find(u'(');
			for (const char16_t c : shown.substr(0, open))
				ip.push_back(static_cast<char>(c));
			if (open != std::u16string_view::npos)
				name = std::u16string(shown.substr(open + 1, shown.find(u')') == std::u16string_view::npos ? std::u16string_view::npos : shown.find(u')') - open - 1));
			return ip + ":" + engine::core::text::EncodeQuotedPrintable(std::u16string_view(name));
		};
		entries.push_back(encode(text));
		for (const std::u16string &item : remoteItems.Get())
			if (ParseAddress(item) != address)
				entries.push_back(encode(item));
		m_preferences.Set("NumRemoteIPs", static_cast<std::int64_t>(entries.size()));
		for (std::size_t index = 0; index < entries.size(); ++index)
			m_preferences.Set("RemoteIP" + std::to_string(index), entries[index]);
		if (m_services.savePreferences)
			m_services.savePreferences();
	}

	void Join()
	{
		const std::u16string text = remote.Get() >= 0 && static_cast<std::size_t>(remote.Get()) < remoteItems.Get().size()
			? remoteItems.Get()[static_cast<std::size_t>(remote.Get())] : remoteText.Get();
		SaveName();
		const auto address = ParseAddress(text);
		if (!address || m_lobby == nullptr)
			return;
		Remember(text, *address);
		Populate();
		m_lobby->RequestSetName(playerName.Get().substr(0, lan::PlayerNameLength));
		m_lobby->RequestGameJoinDirectConnect(*address);
	}

	ShellModel &m_model;
	engine::config::Preferences &m_preferences;
	LanServices m_services;
	lan::LanLobby *m_lobby{nullptr};
	bool m_populating{false};
	std::function<void()> m_release;
};
}
