export module games.generalszh.network.lan.lan_lobby;
import std;

export import games.generalszh.network.lan.lan_protocol;
export import games.generalszh.session.setup.game_setup;
import engine.core.text.utf;

// The LAN lobby (the original's LANAPI with LANAPIhandlers and the game logic
// of LANAPICallbacks, without windows): who is in the lobby, the games being
// set up, hosting one or joining one, its options (the host decides; joiners
// ask), accepting, map availability, chat and the start countdown. It speaks
// through an injected link on a clock the caller gives each update, so it
// runs the same over a socket, in tests and between two lobbies in one
// process. The screens hear what happens through the events.
export namespace generalszh::network::lan
{
namespace setup = generalszh::session::setup;

inline constexpr std::uint64_t UpdateDelay = 200;      // LANAPIUpdateDelay
inline constexpr std::uint64_t ResendDelta = 10000;    // s_resendDelta
inline constexpr std::uint64_t LobbyTimeout = 2 * ResendDelta;
inline constexpr std::uint64_t HostTimeout = 16 * ResendDelta;
inline constexpr std::uint64_t PlayerTimeout = 8 * ResendDelta;
inline constexpr std::uint64_t ActionTimeout = 5000;   // m_actionTimeout
inline constexpr std::uint32_t Broadcast = 0xFFFFFFFF; // INADDR_BROADCAST

// Datagrams out and in (UDP on port 8086 in the game; a pipe in tests). Addresses in host byte order.
struct Link
{
	std::function<void(std::uint32_t to, std::span<const std::byte> datagram)> send;
	std::function<std::optional<std::pair<std::uint32_t, std::vector<std::byte>>>()> receive;
};

// What the lobby needs to know of the game's content (DI).
struct LobbyServices
{
	std::function<std::u16string(std::string_view label)> text;         // a label's text (Generals.csf)
	std::function<bool(const std::string &map, std::uint32_t crc)> hasMap; // the map is here with that CRC
	std::function<bool(const std::string &map)> wouldTransfer;          // WouldMapTransfer: not an official map
	std::function<std::u16string(const std::string &map)> mapName;      // the map's shown name
	std::function<std::string(const std::string &map)> portableMap;     // realMapPathToPortableMapPath
	std::function<std::uint32_t()> seed;                                // GetTickCount
	setup::OptionsLimits limits{};
	int startCountdown{5}; // MultiplayerSettings StartCountdownTimer
};

// What the screens hear (the original's On* callbacks).
struct LobbyEvents
{
	std::function<void()> playersChanged;                  // OnPlayerList / OnNameChange
	std::function<void()> gamesChanged;                    // OnGameList
	std::function<void(Result)> gameCreated;               // OnGameCreate
	std::function<void(Result)> gameJoined;                // OnGameJoin
	std::function<void()> slotsChanged;                    // lanUpdateSlotList / updateGameOptions
	std::function<void()> leftGame;                        // OnPlayerLeave(self) and OnHostLeave: back to the lobby
	std::function<void(std::u16string name, std::uint32_t ip, std::u16string text, ChatType type)> chat;
	std::function<void()> gameStarted;                     // OnGameStart
};

struct LobbyPlayer
{
	std::u16string name;
	std::string login, host;
	std::uint32_t ip{0};
	std::uint64_t lastHeard{0};
};

struct LobbyGame
{
	std::u16string name; // "%8.8X%8.8X": the host's address and seed
	setup::GameSetup setup;
	bool inProgress{false};
	bool directConnect{false};
	std::uint64_t lastHeard{0};
	std::array<std::uint64_t, setup::MaxSlots> playerLastHeard{};
	std::array<std::string, setup::MaxSlots> logins{}, hosts{};

	std::uint32_t HostIp() const noexcept { return setup.slots[0].ip; }
	int SlotOf(std::uint32_t ip) const noexcept
	{
		for (int index = 0; index < setup::MaxSlots; ++index)
			if (setup.slots[static_cast<std::size_t>(index)].Human() && setup.slots[static_cast<std::size_t>(index)].ip == ip)
				return index;
		return -1;
	}
	int SlotNamed(std::u16string_view name) const noexcept // getSlotNum: case blind
	{
		const auto lower = [](std::u16string_view text) {
			std::u16string folded(text);
			for (char16_t &c : folded)
				if (c >= u'A' && c <= u'Z')
					c = static_cast<char16_t>(c - u'A' + u'a');
			return folded;
		};
		for (int index = 0; index < setup::MaxSlots; ++index)
			if (setup.slots[static_cast<std::size_t>(index)].Human() && lower(setup.slots[static_cast<std::size_t>(index)].name) == lower(name))
				return index;
		return -1;
	}
};

class LanLobby
{
public:
	LanLobby(Link link, std::uint32_t localIp, LobbyServices services, LobbyEvents events = {})
		: m_link(std::move(link)), m_localIp(localIp), m_services(std::move(services)), m_events(std::move(events))
	{
	}

	void SetEvents(LobbyEvents events) { m_events = std::move(events); }

	// ---- state ----
	std::uint32_t LocalIp() const noexcept { return m_localIp; }
	const std::u16string &Name() const noexcept { return m_name; }
	bool InLobby() const noexcept { return m_inLobby; }
	const std::vector<LobbyPlayer> &Players() const noexcept { return m_players; }
	// The games known, in the list's order (by name, case blind).
	const std::vector<LobbyGame> &Games() const noexcept { return m_games; }
	LobbyGame *CurrentGame() noexcept { return m_current ? Find(*m_current) : nullptr; }
	const LobbyGame *CurrentGame() const noexcept { return m_current ? Find(*m_current) : nullptr; }
	bool AmIHost() const noexcept
	{
		const LobbyGame *game = CurrentGame();
		return game != nullptr && game->HostIp() == m_localIp;
	}
	int LocalSlot() const noexcept
	{
		const LobbyGame *game = CurrentGame();
		return game != nullptr ? game->SlotOf(m_localIp) : -1;
	}
	bool StartTimerRunning() const noexcept { return m_startTime != 0; }

	// ---- the loop (LANAPI::update) ----
	void Update(std::uint64_t now)
	{
		m_now = now;
		if (m_lastUpdate != 0 && now < m_lastUpdate + UpdateDelay)
			return;
		m_lastUpdate = now;
		Pump();
		Resend();
		Timeouts();
		Expire();
		StartCountdown();
	}

	// Everything waiting, handled now (for tests and flushes).
	void Pump()
	{
		if (!m_link.receive)
			return;
		while (auto datagram = m_link.receive())
		{
			if (datagram->first == m_localIp)
				continue; // our own broadcasts
			if (const auto message = Unframe(datagram->second))
				Handle(*message, datagram->first);
		}
	}

	// ---- requests ----
	// RequestSetName: the name, trimmed; not while an action is pending.
	void RequestSetName(std::u16string name)
	{
		name = Trim(name);
		if (m_pending != Pending::None)
		{
			Notify(m_events.playersChanged);
			return;
		}
		m_lastResend = m_now;
		if (!m_inLobby)
			return;
		m_name = name;
		Message message = Header(MessageType::LobbyAnnounce);
		Send(message);
		Upsert(m_localIp, m_name, {}, {});
		Notify(m_events.playersChanged);
	}

	void RequestLocations() { Send(Header(MessageType::RequestLocations)); }

	// RequestLobbyLeave: we are going (to a game or away).
	void RequestLobbyLeave() { Send(Header(MessageType::RequestLobbyLeave)); }

	// RequestGameCreate: we host a game named by our address and its seed.
	void RequestGameCreate(bool directConnect, const std::string &preferredMap)
	{
		if ((!directConnect && (!m_inLobby || m_current)) || m_pending != Pending::None)
		{
			Notify(m_events.gameCreated, Result::Busy);
			return;
		}
		LobbyGame game;
		game.setup.seed = static_cast<std::int32_t>(m_services.seed ? m_services.seed() : 0);
		char name[17];
		std::snprintf(name, sizeof(name), "%8.8X%8.8X", m_localIp, static_cast<std::uint32_t>(game.setup.seed));
		game.name = std::u16string(name, name + 16);
		setup::GameSlot me;
		me.SetState(setup::SlotState::Player, m_name, m_localIp);
		me.port = GamePort;
		game.setup.SetSlot(0, me);
		SetMap(game, preferredMap);
		game.directConnect = directConnect;
		game.lastHeard = m_now;
		const std::u16string key = game.name;
		AddGame(std::move(game));
		m_current = key;
		m_inLobby = false;
		Notify(m_events.gameCreated, Result::Ok);
		RequestLobbyLeave();
	}

	// RequestGameJoin: ask the game's host (or `ip`) to let us in.
	void RequestGameJoin(const std::u16string &gameName, std::uint32_t ip = 0)
	{
		if (m_pending != Pending::None && m_pending != Pending::JoinDirectConnect)
		{
			Notify(m_events.gameJoined, Result::Busy);
			return;
		}
		const LobbyGame *game = Find(gameName);
		if (game == nullptr)
		{
			Notify(m_events.gameJoined, Result::GameGone);
			return;
		}
		Message message = Header(MessageType::RequestJoin);
		message.gameIp = game->HostIp();
		Send(message, ip);
		m_pending = Pending::Join;
		m_pendingUntil = m_now + ActionTimeout;
	}

	// RequestGameJoinDirectConnect: ask the machine at `ip` for its game.
	void RequestGameJoinDirectConnect(std::uint32_t ip)
	{
		if (m_pending != Pending::None)
		{
			Notify(m_events.gameJoined, Result::Busy);
			return;
		}
		if (ip == 0)
		{
			Notify(m_events.gameJoined, Result::GameGone);
			return;
		}
		m_directConnectIp = ip;
		Message message = Header(MessageType::RequestGameInfo);
		message.ip = m_localIp;
		message.playerName = m_name;
		Send(message, ip);
		m_pending = Pending::JoinDirectConnect;
		m_pendingUntil = m_now + ActionTimeout;
	}

	// RequestGameLeave: the host leaves at once; a joiner waits (nothing answers a leave).
	void RequestGameLeave()
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr)
			return;
		Message message = Header(MessageType::RequestGameLeave);
		message.gameName = game->name;
		Send(message);
		if (AmIHost())
		{
			Notify(m_events.leftGame);
			RemoveGame(game->name);
			m_current.reset();
			m_inLobby = true;
			Upsert(m_localIp, m_name, {}, {});
			return;
		}
		m_pending = Pending::Leave;
		m_pendingUntil = m_now + ActionTimeout;
	}

	void RequestAccept()
	{
		const LobbyGame *game = CurrentGame();
		if (game == nullptr || m_inLobby)
			return;
		Message message = Header(MessageType::SetAccept);
		message.gameName = game->name;
		message.accepted = true;
		Send(message);
	}

	// RequestHasMap: our map status, to everyone in the game.
	void RequestHasMap()
	{
		const LobbyGame *game = CurrentGame();
		if (game == nullptr || m_inLobby || LocalSlot() < 0)
			return;
		Message message = Header(MessageType::MapAvailability);
		message.gameName = game->name;
		message.hasMap = game->setup.slots[static_cast<std::size_t>(LocalSlot())].hasMap;
		message.mapCrc = Crc(Portable(game->setup.map));
		Send(message);
		if (!message.hasMap)
			SystemChat(Format(Text(Transfers(game->setup.map) ? "GUI:LocalPlayerNoMapWillTransfer" : "GUI:LocalPlayerNoMap"), {MapName(game->setup.map)}));
	}

	// RequestChat: sent cut to 100 characters, shown here whole.
	void RequestChat(const std::u16string &text, ChatType type)
	{
		Message message = Header(MessageType::Chat);
		if (const LobbyGame *game = CurrentGame(); game != nullptr && !m_inLobby)
			message.gameName = game->name;
		message.chatType = type;
		message.chat = text.substr(0, ChatLength);
		Send(message);
		if (m_events.chat)
			m_events.chat(m_name, m_localIp, text, type);
	}

	// RequestPlayerChat: "/me " (any case) makes it an emote.
	void RequestPlayerChat(std::u16string text)
	{
		if (text.size() >= 4 && (text[0] == u'/') && (text[1] | 0x20) == u'm' && (text[2] | 0x20) == u'e' && text[3] == u' ')
			RequestChat(text.substr(4), ChatType::Emote);
		else
			RequestChat(text, ChatType::Normal);
	}

	// RequestGameOptions: the host's options to everyone (or `ip`), and to itself.
	void RequestGameOptions(const std::string &options, std::uint32_t ip = 0)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr)
			return;
		Message message = Header(MessageType::GameOptions);
		message.options = options.substr(0, OptionsLength);
		Send(message, ip);
		OnGameOptions(m_localIp, LocalSlot(), message.options);
	}

	// The host's options string (GenerateGameOptionsString: empty unless we host).
	std::string GameOptions() const
	{
		const LobbyGame *game = CurrentGame();
		return game != nullptr && AmIHost() ? setup::ToOptionsString(game->setup) : std::string{};
	}

	// The host has changed the setup: tell everyone.
	void BroadcastOptions() { RequestGameOptions(GameOptions()); }

	// RequestGameAnnounce: the host of a game others can find says where it is.
	void RequestGameAnnounce()
	{
		const LobbyGame *game = CurrentGame();
		if (game == nullptr || game->directConnect || !AmIHost())
			return;
		Send(Announce(*game));
	}

	// RequestGameStartTimer: host only; counts down a second at a time, then starts.
	void RequestGameStartTimer(int seconds)
	{
		if (!AmIHost())
			return;
		m_startTime = m_now + 1000;
		m_startSeconds = seconds - 1;
		Message message = Header(MessageType::GameStartTimer);
		message.seconds = seconds;
		Send(message);
		OnGameStartTimer(seconds);
	}

	void ResetGameStartTimer() noexcept
	{
		m_startTime = 0;
		m_startSeconds = 0;
	}

	// RequestGameStart: host only.
	void RequestGameStart()
	{
		if (!AmIHost())
			return;
		Send(Header(MessageType::GameStart));
		OnGameStart();
	}

	// LANGameInfo::resetAccepted: the countdown stops and every human must accept again.
	void ResetAccepted()
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr)
			return;
		ResetGameStartTimer();
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
			if (slot.Human())
				slot.accepted = false; // StartPressed accepts the host again
		}
		m_startEnabled = true;
	}

	// Whether the host's Start and Select Map buttons take input (off once the countdown runs).
	bool StartEnabled() const noexcept { return m_startEnabled; }
	void SetStartEnabled(bool enabled) noexcept { m_startEnabled = enabled; }

	// The map of the game we host changed (setMap, setMapCRC, setMapSize).
	void SetCurrentMap(const std::string &map, std::uint32_t crc, std::uint32_t size)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr)
			return;
		game->setup.map = map;
		game->setup.mapCrc = crc;
		game->setup.mapSize = size;
		RecheckMap(*game);
	}

	// A system line in the chat (as the original's OnChat with LANCHAT_SYSTEM).
	void SystemChat(const std::u16string &text)
	{
		if (m_events.chat)
			m_events.chat({}, 0, text, ChatType::System);
	}

private:
	enum class Pending : std::uint8_t
	{
		None,
		Join,
		JoinDirectConnect,
		Leave,
	};

	template<class Callback, class... Args>
	static void Notify(const Callback &callback, Args &&...args)
	{
		if (callback)
			callback(std::forward<Args>(args)...);
	}

	std::u16string Text(std::string_view label) const
	{
		if (m_services.text)
			if (std::u16string found = m_services.text(label); !found.empty())
				return found;
		return std::u16string(label.begin(), label.end());
	}
	// "%ls" and "%d" in turn replaced (UnicodeString::format with the original's arguments).
	static std::u16string Format(std::u16string format, std::initializer_list<std::u16string> arguments)
	{
		auto next = arguments.begin();
		for (std::size_t at = 0; next != arguments.end();)
		{
			const std::size_t ls = format.find(u"%ls", at), d = format.find(u"%d", at), s = format.find(u"%s", at);
			const std::size_t found = std::min({ls, d, s});
			if (found == std::u16string::npos)
				break;
			const std::size_t length = found == ls ? 3 : 2;
			format.replace(found, length, *next);
			at = found + next->size();
			++next;
		}
		return format;
	}
	std::u16string MapName(const std::string &map) const
	{
		if (m_services.mapName)
			if (std::u16string name = m_services.mapName(map); !name.empty())
				return name;
		const auto slash = map.find_last_of('\\');
		const std::string leaf = slash == std::string::npos ? map : map.substr(slash + 1);
		return std::u16string(leaf.begin(), leaf.end());
	}
	std::string Portable(const std::string &map) const { return m_services.portableMap ? m_services.portableMap(map) : map; }
	bool Transfers(const std::string &map) const { return m_services.wouldTransfer && m_services.wouldTransfer(map); }

	static std::u16string Trim(std::u16string text)
	{
		while (!text.empty() && (text.front() == u' ' || text.front() == u'\t'))
			text.erase(text.begin());
		while (!text.empty() && (text.back() == u' ' || text.back() == u'\t'))
			text.pop_back();
		return text;
	}

	Message Header(MessageType type) const
	{
		Message message;
		message.type = type;
		message.name = m_name.substr(0, PlayerNameLength);
		return message;
	}

	// sendMessage: to `ip`; else to a direct-connect game's players; else to everyone.
	void Send(const Message &message, std::uint32_t ip = 0)
	{
		if (!m_link.send)
			return;
		const std::vector<std::byte> datagram = Frame(message);
		if (ip != 0)
		{
			m_link.send(ip, datagram);
			return;
		}
		if (const LobbyGame *game = CurrentGame(); game != nullptr && game->directConnect)
		{
			for (const setup::GameSlot &slot : game->setup.slots)
				if (slot.Human() && slot.ip != m_localIp && slot.ip != 0)
					m_link.send(slot.ip, datagram);
			return;
		}
		m_link.send(Broadcast, datagram);
	}

	Message Announce(const LobbyGame &game) const
	{
		Message message = Header(MessageType::GameAnnounce);
		message.gameName = game.name;
		message.inProgress = game.inProgress;
		message.options = setup::ToOptionsString(game.setup);
		message.directConnect = game.directConnect;
		return message;
	}

	// ---- the lists (kept by name, case blind) ----
	static std::u16string Folded(std::u16string_view text)
	{
		std::u16string folded(text);
		for (char16_t &c : folded)
			if (c >= u'A' && c <= u'Z')
				c = static_cast<char16_t>(c - u'A' + u'a');
		return folded;
	}
	void Upsert(std::uint32_t ip, const std::u16string &name, const std::string &login, const std::string &host)
	{
		auto found = std::find_if(m_players.begin(), m_players.end(), [ip](const LobbyPlayer &player) { return player.ip == ip; });
		if (found == m_players.end())
		{
			m_players.push_back({name, login, host, ip, m_now});
			found = m_players.end() - 1;
		}
		found->name = name, found->login = login, found->host = host, found->lastHeard = m_now;
		std::stable_sort(m_players.begin(), m_players.end(), [](const LobbyPlayer &a, const LobbyPlayer &b) { return Folded(a.name) < Folded(b.name); });
	}
	void RemovePlayer(std::uint32_t ip)
	{
		std::erase_if(m_players, [ip](const LobbyPlayer &player) { return player.ip == ip; });
	}
	LobbyGame *Find(const std::u16string &name)
	{
		for (LobbyGame &game : m_games)
			if (game.name == name)
				return &game;
		return nullptr;
	}
	const LobbyGame *Find(const std::u16string &name) const
	{
		for (const LobbyGame &game : m_games)
			if (game.name == name)
				return &game;
		return nullptr;
	}
	void AddGame(LobbyGame game)
	{
		m_games.push_back(std::move(game));
		std::stable_sort(m_games.begin(), m_games.end(), [](const LobbyGame &a, const LobbyGame &b) { return Folded(a.name) < Folded(b.name); });
	}
	void RemoveGame(const std::u16string &name)
	{
		std::erase_if(m_games, [&name](const LobbyGame &game) { return game.name == name; });
	}

	// setMap / setMapCRC: whether we have the map is ours to say.
	void SetMap(LobbyGame &game, const std::string &map)
	{
		game.setup.map = map;
		RecheckMap(game);
	}
	void RecheckMap(LobbyGame &game)
	{
		const int local = game.SlotOf(m_localIp);
		if (local >= 0)
			game.setup.slots[static_cast<std::size_t>(local)].hasMap = !m_services.hasMap || m_services.hasMap(game.setup.map, game.setup.mapCrc);
	}

	// ---- handlers ----
	void Handle(const Message &message, std::uint32_t from)
	{
		switch (message.type)
		{
		case MessageType::RequestLocations: OnRequestLocations(message, from); break;
		case MessageType::GameAnnounce: OnGameAnnounce(message, from); break;
		case MessageType::LobbyAnnounce:
			Upsert(from, message.name, message.userName, message.hostName);
			Notify(m_events.playersChanged);
			break;
		case MessageType::RequestJoin: OnRequestJoin(message, from); break;
		case MessageType::JoinAccept: OnJoinAccept(message, from); break;
		case MessageType::JoinDeny:
			if (message.playerIp == m_localIp && m_pending == Pending::Join)
			{
				m_pending = Pending::None;
				Notify(m_events.gameJoined, message.reason);
			}
			break;
		case MessageType::RequestGameLeave: OnRequestGameLeave(message, from); break;
		case MessageType::RequestLobbyLeave:
			if (m_inLobby)
			{
				RemovePlayer(from);
				Notify(m_events.playersChanged);
			}
			break;
		case MessageType::SetAccept: OnSetAccept(message, from); break;
		case MessageType::MapAvailability: OnMapAvailability(message, from); break;
		case MessageType::Chat: OnChat(message, from); break;
		case MessageType::GameStart:
			if (const LobbyGame *game = CurrentGame(); game != nullptr && !game->inProgress && from == game->HostIp())
				OnGameStart();
			break;
		case MessageType::GameStartTimer:
			if (const LobbyGame *game = CurrentGame(); game != nullptr && !game->inProgress && from == game->HostIp())
				OnGameStartTimer(message.seconds);
			break;
		case MessageType::GameOptions:
			if (LobbyGame *game = CurrentGame(); game != nullptr && !m_inLobby && !game->inProgress)
				if (const int slot = game->SlotOf(from); slot >= 0)
					OnGameOptions(from, slot, message.options);
			break;
		case MessageType::Inactive: OnInactive(message, from); break;
		case MessageType::RequestGameInfo:
			if (const LobbyGame *game = CurrentGame(); game != nullptr && AmIHost())
				Send(Announce(*game), from);
			break;
		}
	}

	void OnRequestLocations(const Message &message, std::uint32_t from)
	{
		if (m_inLobby)
		{
			Send(Header(MessageType::LobbyAnnounce));
			m_lastResend = m_now;
		}
		if (const LobbyGame *game = CurrentGame(); game != nullptr && AmIHost())
			Send(Announce(*game));
		Upsert(from, message.name, message.userName, message.hostName);
		Notify(m_events.playersChanged);
	}

	void OnGameAnnounce(const Message &message, std::uint32_t from)
	{
		if (from == m_localIp)
			return;
		if (const LobbyGame *current = CurrentGame(); current != nullptr && current->inProgress)
			return;
		const auto parsed = setup::ParseOptionsString(message.options, m_services.limits);
		if (from == m_directConnectIp && !m_current)
		{
			// The direct-connect host answered: join it.
			if (!parsed)
				return;
			LobbyGame *game = Find(message.gameName);
			if (game == nullptr)
			{
				LobbyGame fresh;
				fresh.name = message.gameName;
				AddGame(std::move(fresh));
				game = Find(message.gameName);
			}
			game->setup = *parsed;
			game->inProgress = message.inProgress;
			game->directConnect = message.directConnect;
			game->lastHeard = m_now;
			RequestGameJoin(message.gameName, m_directConnectIp);
			return;
		}
		LobbyGame *game = Find(message.gameName);
		if (!parsed)
		{
			if (game != nullptr)
				RemoveGame(message.gameName);
			Notify(m_events.gamesChanged);
			return;
		}
		if (game == nullptr)
		{
			LobbyGame fresh;
			fresh.name = message.gameName;
			AddGame(std::move(fresh));
			game = Find(message.gameName);
		}
		// Our own game is re-read too: a joiner keeps its hosts' names and its own map status.
		if (game->name == m_current.value_or(u""))
			ApplyOptions(*game, *parsed);
		else
			game->setup = *parsed;
		game->inProgress = message.inProgress;
		game->directConnect = message.directConnect;
		game->lastHeard = m_now;
		if (m_inLobby)
			Notify(m_events.gamesChanged);
	}

	// The characters a joiner's name may not have, or a name of only spaces (handleRequestJoin).
	static bool BadName(std::u16string_view name)
	{
		bool onlySpaces = true;
		for (const char16_t c : name)
		{
			if (c < 0x20 || c == u',' || c == u':' || c == u';' || (c >= 0x7F && c <= 0x9F) || c == 0x2028 || c == 0x2029 || (c >= 0xD800 && c <= 0xDFFF))
				return true;
			const bool space = c == u' ' || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000;
			onlySpaces = onlySpaces && space;
		}
		return onlySpaces;
	}

	void OnRequestJoin(const Message &message, std::uint32_t from)
	{
		if (message.gameIp != m_localIp)
			return;
		LobbyGame *game = CurrentGame();
		const auto deny = [&](Result reason, bool named) {
			Message reply = Header(MessageType::JoinDeny);
			reply.gameName = named && game != nullptr ? game->name : std::u16string{};
			reply.gameIp = m_localIp;
			reply.playerIp = from;
			reply.reason = reason;
			Send(reply, from);
		};
		if (game == nullptr || !AmIHost())
			deny(Result::GameGone, false);
		else if (game->inProgress)
			deny(Result::GameStarted, false);
		else if (BadName(message.name) || std::any_of(game->setup.slots.begin(), game->setup.slots.end(), [&](const setup::GameSlot &slot) {
					 return slot.Human() && slot.name == message.name; // case sensitive, as the original's
				 }))
			deny(Result::DuplicateName, false);
		else
		{
			int open = -1;
			for (int index = 0; index < setup::MaxSlots && open < 0; ++index)
				if (game->setup.slots[static_cast<std::size_t>(index)].state == setup::SlotState::Open)
					open = index;
			if (open < 0)
				deny(Result::GameFull, true);
			else
			{
				setup::GameSlot joiner;
				joiner.SetState(setup::SlotState::Player, message.name, from);
				joiner.port = GamePort;
				game->setup.SetSlot(open, joiner);
				game->playerLastHeard[static_cast<std::size_t>(open)] = m_now;
				game->logins[static_cast<std::size_t>(open)] = message.userName;
				game->hosts[static_cast<std::size_t>(open)] = message.hostName;
				Message reply = Header(MessageType::JoinAccept);
				reply.gameName = game->name;
				reply.slot = open;
				reply.gameIp = m_localIp;
				reply.playerIp = from;
				Send(reply);
				// OnPlayerJoin: everyone accepts again (and hears so below).
				ResetAccepted();
				Notify(m_events.slotsChanged);
			}
		}
		if (AmIHost())
			BroadcastOptions();
	}

	void OnJoinAccept(const Message &message, std::uint32_t from)
	{
		if (message.playerIp != m_localIp || m_pending != Pending::Join)
			return;
		m_pending = Pending::None;
		LobbyGame *game = Find(message.gameName);
		if (game == nullptr)
		{
			Notify(m_events.gameJoined, Result::Unknown);
			return;
		}
		m_inLobby = false;
		m_current = game->name;
		// We take the slot we were given; the host's options follow.
		setup::GameSlot me;
		me.SetState(setup::SlotState::Player, m_name, m_localIp);
		me.port = GamePort;
		if (message.slot >= 0 && message.slot < setup::MaxSlots)
			game->setup.SetSlot(message.slot, me);
		game->logins[0] = message.userName;
		game->hosts[0] = message.hostName;
		game->lastHeard = m_now;
		RecheckMap(*game);
		(void)from;
		Notify(m_events.gameJoined, Result::Ok);
	}

	void OnRequestGameLeave(const Message &message, std::uint32_t from)
	{
		LobbyGame *game = CurrentGame();
		if (game != nullptr && !m_inLobby && !game->inProgress)
		{
			const int slot = game->SlotOf(from);
			if (slot == 0)
			{
				// The host went: back to the lobby.
				m_pending = Pending::None;
				const std::u16string name = game->name;
				m_current.reset();
				RemoveGame(name);
				m_inLobby = true;
				Upsert(m_localIp, m_name, {}, {});
				Notify(m_events.leftGame);
				return;
			}
			if (slot >= 1)
			{
				if (AmIHost())
				{
					setup::GameSlot open;
					open.SetState(setup::SlotState::Open);
					game->setup.SetSlot(slot, open);
					m_lastResend = 0; // OnPlayerLeave: say so at once
					ResetAccepted();
					BroadcastOptions();
					RequestGameOptions(GameOptions(), from);
					Notify(m_events.slotsChanged);
				}
			}
			return;
		}
		if (m_inLobby)
		{
			RemoveGame(message.gameName);
			Notify(m_events.gamesChanged);
		}
	}

	void OnSetAccept(const Message &message, std::uint32_t from)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr || m_inLobby || game->inProgress)
			return;
		const int slot = game->SlotOf(from);
		if (slot < 0)
			return;
		if (AmIHost())
		{
			game->setup.slots[static_cast<std::size_t>(slot)].accepted = message.accepted;
			BroadcastOptions();
			Notify(m_events.slotsChanged);
		}
		else if (slot == 0)
			SystemChat(Text("GUI:HostWantsToStart"));
	}

	void OnMapAvailability(const Message &message, std::uint32_t from)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr || m_inLobby || Crc(Portable(game->setup.map)) != message.mapCrc)
			return;
		const int slot = game->SlotOf(from);
		if (slot < 0 || !AmIHost())
			return;
		game->setup.slots[static_cast<std::size_t>(slot)].hasMap = message.hasMap;
		if (!message.hasMap)
			SystemChat(Format(Text(Transfers(game->setup.map) ? "GUI:PlayerNoMapWillTransfer" : "GUI:PlayerNoMap"),
				{game->setup.slots[static_cast<std::size_t>(slot)].name, MapName(game->setup.map)}));
		Notify(m_events.slotsChanged);
	}

	void OnChat(const Message &message, std::uint32_t from)
	{
		if (m_inLobby)
		{
			const auto found = std::find_if(m_players.begin(), m_players.end(), [from](const LobbyPlayer &player) { return player.ip == from; });
			if (found == m_players.end())
				return;
			found->lastHeard = m_now;
			if (m_events.chat)
				m_events.chat(found->name, from, message.chat, message.chatType);
			return;
		}
		const LobbyGame *game = CurrentGame();
		if (game == nullptr || game->name != message.gameName || game->SlotOf(from) < 0)
			return;
		if (m_events.chat)
			m_events.chat(message.name, from, message.chat, message.chatType);
	}

	void OnInactive(const Message &message, std::uint32_t from)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr || m_inLobby || game->inProgress || !AmIHost() || StartTimerRunning() || from == m_localIp)
			return;
		const int slot = game->SlotNamed(message.name);
		if (slot < 0 || game->setup.slots[static_cast<std::size_t>(slot)].ip != from)
			return;
		game->setup.slots[static_cast<std::size_t>(slot)].accepted = false;
		BroadcastOptions();
		Notify(m_events.slotsChanged);
	}

	void OnGameStartTimer(int seconds)
	{
		SystemChat(Format(Text(seconds == 1 ? "LAN:GameStartTimerSingular" : "LAN:GameStartTimerPlural"), {std::u16string(Number(seconds))}));
	}
	static std::u16string Number(int value)
	{
		const std::string text = std::to_string(value);
		return std::u16string(text.begin(), text.end());
	}

	void OnGameStart()
	{
		if (LobbyGame *game = CurrentGame(); game != nullptr)
		{
			game->setup.CloseOpenSlots(); // startGame
			game->inProgress = true;
		}
		Notify(m_events.gameStarted);
	}

	// A joiner applies the host's options, keeping each human's login and host by name and its own map status.
	void ApplyOptions(LobbyGame &game, const setup::GameSetup &parsed)
	{
		std::array<std::pair<std::u16string, std::pair<std::string, std::string>>, setup::MaxSlots> kept{};
		for (std::size_t index = 0; index < kept.size(); ++index)
			kept[index] = {game.setup.slots[index].name, {game.logins[index], game.hosts[index]}};
		const bool wasListed = game.SlotOf(m_localIp) >= 0;
		const std::uint32_t oldCrc = game.setup.mapCrc;
		game.setup = parsed;
		for (std::size_t index = 0; index < kept.size(); ++index)
		{
			game.logins[index].clear(), game.hosts[index].clear();
			for (const auto &[name, identity] : kept)
				if (game.setup.slots[index].Human() && name == game.setup.slots[index].name)
					game.logins[index] = identity.first, game.hosts[index] = identity.second;
			if (game.setup.slots[index].Human())
				game.playerLastHeard[index] = m_now;
		}
		RecheckMap(game);
		if (!AmIHost() && game.SlotOf(m_localIp) >= 0 && (oldCrc != game.setup.mapCrc || !wasListed))
		{
			RequestHasMap();
			Notify(m_events.slotsChanged);
		}
	}

	// OnGameOptions: the host's options reach its joiners; a joiner's requests reach the host.
	void OnGameOptions(std::uint32_t from, int slot, const std::string &options)
	{
		LobbyGame *game = CurrentGame();
		if (game == nullptr || game->inProgress || slot < 0 || game->setup.slots[static_cast<std::size_t>(slot)].ip != from)
			return;
		if (slot == 0 && !AmIHost())
		{
			game->lastHeard = m_now;
			const setup::GameSetup old = game->setup;
			if (const auto parsed = setup::ParseOptionsString(options, m_services.limits))
			{
				ApplyOptions(*game, *parsed);
				if (game->SlotOf(m_localIp) < 1)
				{
					// Our slot is gone: we were sent away (or the host has let us go).
					m_pending = Pending::None;
					game->setup = old;
					const std::u16string name = game->name;
					m_current.reset();
					RemoveGame(name);
					m_inLobby = true;
					Notify(m_events.leftGame);
					return;
				}
				Notify(m_events.slotsChanged);
			}
			return;
		}
		if (options.starts_with("User="))
		{
			game->logins[static_cast<std::size_t>(slot)] = options.substr(5);
			return;
		}
		if (options.starts_with("Host="))
		{
			game->hosts[static_cast<std::size_t>(slot)] = options.substr(5);
			return;
		}
		if (!AmIHost() || from == m_localIp)
			return;
		game->playerLastHeard[static_cast<std::size_t>(slot)] = m_now;
		if (options == "HELLO")
			return;
		const auto equals = options.find('=');
		if (equals == std::string::npos)
			return;
		const std::string key = options.substr(0, equals);
		const int value = std::atoi(options.c_str() + equals + 1);
		setup::GameSlot &requester = game->setup.slots[static_cast<std::size_t>(slot)];
		bool change = false, unaccept = false;
		if (key == "Color" && value >= -1 && value < m_services.limits.colors && value != requester.color && !requester.Observer())
		{
			if (value == -1 || !game->setup.ColorTaken(value, slot))
				requester.color = value;
			change = true;
		}
		else if (key == "PlayerTemplate" && value >= setup::ObserverTemplate && value < m_services.limits.playerTemplates && value != requester.playerTemplate)
		{
			requester.playerTemplate = value;
			if (requester.Observer())
				requester.color = requester.startPos = requester.team = setup::Random;
			change = unaccept = true;
		}
		else if (key == "StartPos" && !requester.Observer() && value >= -1 && value < setup::MaxSlots && value != requester.startPos)
		{
			if (value == -1 || !game->setup.StartTaken(value, slot))
				requester.startPos = value;
			change = unaccept = true;
		}
		else if (key == "Team" && !requester.Observer() && value >= -1 && value < setup::MaxSlots / 2 && value != requester.team)
		{
			requester.team = value;
			change = unaccept = true;
		}
		else if (key == "NAT" && value >= 1 && value <= 64)
		{
			requester.natBehavior = value;
			change = true;
		}
		if (!change)
			return;
		if (unaccept)
			ResetAccepted();
		BroadcastOptions();
		Notify(m_events.slotsChanged);
	}

	// ---- the timed parts of update ----
	void Resend()
	{
		if (m_lastResend != 0 && m_now < m_lastResend + ResendDelta)
			return;
		m_lastResend = m_now;
		if (m_inLobby)
		{
			RequestSetName(m_name);
			return;
		}
		const LobbyGame *game = CurrentGame();
		if (game == nullptr)
			return;
		if (!game->inProgress)
		{
			if (AmIHost())
			{
				BroadcastOptions();
				RequestGameAnnounce();
			}
			else
			{
				Message hello = Header(MessageType::GameOptions);
				hello.options = "HELLO";
				Send(hello);
			}
		}
		else
			RequestGameAnnounce();
	}

	void Timeouts()
	{
		const std::size_t players = m_players.size();
		std::erase_if(m_players, [this](const LobbyPlayer &player) { return player.ip != m_localIp && player.lastHeard + LobbyTimeout < m_now; });
		if (players != m_players.size())
			Notify(m_events.playersChanged);
		const std::size_t games = m_games.size();
		std::erase_if(m_games, [this](const LobbyGame &game) { return game.name != m_current.value_or(u"") && game.lastHeard + LobbyTimeout < m_now; });
		if (games != m_games.size())
			Notify(m_events.gamesChanged);
		LobbyGame *game = CurrentGame();
		if (game == nullptr || game->inProgress)
			return;
		if (!AmIHost())
		{
			if (game->lastHeard + HostTimeout < m_now)
			{
				// The host is silent: as if it had left.
				Message left = Header(MessageType::RequestGameLeave);
				left.gameName = game->name;
				OnRequestGameLeave(left, game->HostIp());
				SystemChat(Text("LAN:HostNotResponding"));
			}
			return;
		}
		for (int index = 1; index < setup::MaxSlots; ++index)
		{
			const setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
			if (!slot.Human() || slot.ip == 0 || game->playerLastHeard[static_cast<std::size_t>(index)] + PlayerTimeout >= m_now)
				continue;
			const std::u16string name = slot.name;
			Message left = Header(MessageType::RequestGameLeave);
			left.gameName = game->name;
			OnRequestGameLeave(left, slot.ip);
			SystemChat(Format(Text("LAN:PlayerDropped"), {name}));
			game = CurrentGame();
			if (game == nullptr)
				return;
		}
	}

	void Expire()
	{
		if (m_pending == Pending::None || m_now < m_pendingUntil)
			return;
		const Pending pending = std::exchange(m_pending, Pending::None);
		if (pending == Pending::Join || pending == Pending::JoinDirectConnect)
		{
			Notify(m_events.gameJoined, Result::Timeout);
			m_current.reset();
			m_inLobby = true;
		}
		else if (pending == Pending::Leave)
		{
			if (const LobbyGame *game = CurrentGame(); game != nullptr)
			{
				const std::u16string name = game->name;
				m_current.reset();
				RemoveGame(name);
			}
			m_inLobby = true;
			Upsert(m_localIp, m_name, {}, {});
			Notify(m_events.leftGame);
		}
	}

	void StartCountdown()
	{
		if (m_startTime == 0 || m_startTime > m_now)
			return;
		if (m_startSeconds > 0)
			RequestGameStartTimer(m_startSeconds);
		else
		{
			ResetGameStartTimer();
			RequestGameStart();
		}
	}

	Link m_link;
	std::uint32_t m_localIp;
	LobbyServices m_services;
	LobbyEvents m_events;
	std::u16string m_name;
	bool m_inLobby{true};
	std::vector<LobbyPlayer> m_players;
	std::vector<LobbyGame> m_games;
	std::optional<std::u16string> m_current;
	Pending m_pending{Pending::None};
	std::uint64_t m_pendingUntil{0};
	std::uint32_t m_directConnectIp{0};
	std::uint64_t m_now{0}, m_lastUpdate{0}, m_lastResend{0};
	std::uint64_t m_startTime{0};
	int m_startSeconds{0};
	bool m_startEnabled{true};
};
}
