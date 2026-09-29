export module games.generalszh.shell.lan.lan_game_options_view_model;
import std;

export import games.generalszh.shell.lan.lan_lobby_view_model;
export import games.generalszh.shell.skirmish.skirmish_view_model;
import engine.core.text.quoted_printable;

// The LAN game setup screen (the original's LanGameOptionsMenu.cpp and
// LanMapSelectMenu.cpp, without windows). The host sets the game up: the
// slots (open, closed, AIs; a human can be sent away), the AIs' choices, the
// map, the money and the superweapon limit, and starts it once everyone has
// accepted. A joiner picks its own colour, faction, team and start spot (the
// host decides) and accepts. Everyone chats. EnableAcceptControls and
// UpdateSlotList say which boxes take input.
export namespace generalszh::shell
{
namespace setup = generalszh::session::setup;

inline constexpr std::uint32_t AcceptedColor = 0x00FF00FF, NotAcceptedColor = 0xFF0000FF;

class LanGameOptionsViewModel
{
public:
	LanGameOptionsViewModel(ShellModel &model, MessageBoxViewModel &messages, const SetupCatalog &catalog, engine::config::Preferences &preferences,
		LanServices services)
		: m_model(model), m_messages(messages), m_catalog(catalog), m_preferences(preferences), m_services(std::move(services))
	{
		back.SetAction([this] {
			if (mapSelectOpen.Get())
				CloseMapSelect();
			if (m_lobby != nullptr)
				m_lobby->RequestGameLeave();
		});
		escape.SetAction([this] {
			if (mapSelectOpen.Get())
				mapBack.Execute();
			else
				back.Execute();
		});
		start.SetAction([this] { StartPressed(); });
		send.SetAction([this] { Send(); });
		selectMap.SetAction([this] { OpenMapSelect(); });
		mapOk.SetAction([this] { ChooseMap(); });
		mapBack.SetAction([this] { CloseMapSelect(); });
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			startPosition[static_cast<std::size_t>(index)].SetAction([this, index] { PickStartPosition(index); });
			startPositionClear[static_cast<std::size_t>(index)].SetAction([this, index] { ClearStartPosition(index); });
		}
		Watch();
	}
	~LanGameOptionsViewModel()
	{
		for (auto &release : m_releases)
			release();
	}
	LanGameOptionsViewModel(const LanGameOptionsViewModel &) = delete;
	LanGameOptionsViewModel &operator=(const LanGameOptionsViewModel &) = delete;

	// LanGameOptionsMenuInit.
	void Open(lan::LanLobby &lobby)
	{
		m_lobby = &lobby;
		chat.Set({});
		lan::LobbyGame *game = lobby.CurrentGame();
		if (game == nullptr)
			return;
		// Back from the game (its score screen gone): back to the lobby (popImmediate).
		if (game->inProgress)
		{
			m_model.Pop();
			return;
		}
		const bool isHost = lobby.AmIHost();
		if (isHost)
		{
			// The host's own choices and the game's options from Network.ini.
			setup::GameSlot &me = game->setup.slots[0];
			me.color = PreferredColor();
			me.playerTemplate = PreferredFaction();
			me.natBehavior = 1;
			game->setup.startingCash = static_cast<std::uint32_t>(m_preferences.Number("StartingCash", m_catalog.defaultStartingMoney));
			game->setup.superweaponRestriction = m_preferences.Flag("SuperweaponRestrict", false) ? 1 : 0;
			if (const SetupMap *map = m_catalog.Find(game->setup.map))
			{
				lobby.SetCurrentMap(map->file, map->crc, map->size);
				game->setup.AdjustSlotsForMap(map->players);
			}
		}
		else if (const SetupMap *map = m_catalog.Find(game->setup.map))
		{
			lobby.SetCurrentMap(game->setup.map, game->setup.mapCrc, game->setup.mapSize);
			(void)map;
		}
		// The fixed lists.
		std::vector<std::u16string> types{Text("GUI:Open"), Text("GUI:Closed"), Text("GUI:EasyAI"), Text("GUI:MediumAI"), Text("GUI:HardAI")};
		std::vector<std::u16string> factions{Text("GUI:Random")};
		for (const SetupFaction &faction : m_catalog.factions)
			factions.push_back(faction.name);
		factions.push_back(Text("GUI:Observer"));
		std::vector<std::u16string> teams{Text("Team:0")};
		for (int team = 1; team <= setup::MaxSlots / 2; ++team)
			teams.push_back(Text("Team:" + std::to_string(team)));
		m_updating = true;
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			playerItems[at].Set(types);
			factionItems[at].Set(factions);
			teamItems[at].Set(teams);
		}
		std::vector<std::u16string> money;
		for (const std::uint32_t amount : m_catalog.startingMoney)
			money.push_back(MoneyText(amount));
		moneyItems.Set(std::move(money));
		startText.Set(isHost ? Text("GUI:Start") : Text("GUI:Accept"));
		hostOptionsEnabled.Set(isHost);
		mapSelectOpen.Set(false);
		m_updating = false;
		if (isHost)
		{
			lobby.BroadcastOptions();
			lobby.RequestGameAnnounce();
		}
		else
			lobby.RequestHasMap();
		Refresh();
	}

	// The lobby's events while this screen shows.
	void SlotsChanged() { Refresh(); }
	void Chat(const std::u16string &name, std::uint32_t ip, const std::u16string &text, lan::ChatType type)
	{
		if (m_lineFor)
			AddChat(m_lineFor(name, ip, text, type));
	}
	// OnPlayerLeave(self) / OnHostLeave: Network.ini keeps our choices; back to the lobby.
	void LeftGame()
	{
		SaveChoices();
		m_model.Pop();
	}
	// OnGameStart: our choices kept (LANPreferences), and the game on (MSG_NEW_GAME, GAME_LAN, InitRandom(seed)).
	void GameStarted()
	{
		SaveChoices();
		const lan::LobbyGame *game = m_lobby != nullptr ? m_lobby->CurrentGame() : nullptr;
		if (game != nullptr && m_services.start)
		{
			m_services.start(game->setup, m_lobby->LocalSlot(), game->HostIp(), m_lobby->AmIHost());
			return;
		}
		m_messages.Show(MessageBoxOk(Text("GUI:ErrorStartingGame"), Text("GUI:ErrorStartingGame")));
		if (m_lobby != nullptr)
			m_lobby->RequestGameLeave();
	}

	// How the lobby screen colours a chat line (shared, so both look alike).
	std::function<ChatLine(const std::u16string &, std::uint32_t, const std::u16string &, lan::ChatType)> m_lineFor;

	std::array<engine::gui::mvvm::Observable<std::vector<std::u16string>>, setup::MaxSlots> playerItems, colorItems, factionItems, teamItems;
	std::array<engine::gui::mvvm::Observable<std::vector<std::uint32_t>>, setup::MaxSlots> colorRowColors;
	std::array<engine::gui::mvvm::Observable<int>, setup::MaxSlots> player, color, faction, team;
	std::array<engine::gui::mvvm::Observable<std::u16string>, setup::MaxSlots> playerText; // a human's name in its box
	std::array<engine::gui::mvvm::Observable<bool>, setup::MaxSlots> playerEnabled, slotEnabled, acceptShown, accepted;
	std::array<engine::gui::mvvm::Observable<std::u16string>, setup::MaxSlots> startNumber;
	std::array<engine::gui::mvvm::Observable<bool>, setup::MaxSlots> startShown;
	std::array<engine::gui::mvvm::Command, setup::MaxSlots> startPosition, startPositionClear;
	engine::gui::mvvm::Observable<std::vector<MapPoint>> startSpots;
	engine::gui::mvvm::Observable<std::u16string> mapName, startText, chatEntry;
	engine::gui::mvvm::Observable<MapPreview> preview;
	engine::gui::mvvm::Observable<std::vector<std::u16string>> moneyItems;
	engine::gui::mvvm::Observable<int> money{-1};
	engine::gui::mvvm::Observable<bool> limitSuperweapons{false}, hostOptionsEnabled{false}, startEnabled{true};
	engine::gui::mvvm::Observable<std::vector<ChatLine>> chat;
	// The map list over the screen (LanMapSelectMenu.wnd).
	engine::gui::mvvm::Observable<bool> mapSelectOpen{false}, mainButtonsEnabled{true}, systemMaps{true};
	engine::gui::mvvm::Observable<std::vector<std::u16string>> mapItems;
	engine::gui::mvvm::Observable<int> mapSelected{-1};
	engine::gui::mvvm::Observable<MapPreview> mapListPreview;
	engine::gui::mvvm::Observable<std::vector<MapPoint>> mapListStartSpots;
	engine::gui::mvvm::Command back, escape, start, send, selectMap, mapOk, mapBack;

private:
	std::u16string Text(std::string_view label) const
	{
		if (m_services.text)
			if (std::u16string found = m_services.text(label); !found.empty())
				return found;
		return std::u16string(label.begin(), label.end());
	}
	static std::u16string Number(long long value)
	{
		const std::string text = std::to_string(value);
		return std::u16string(text.begin(), text.end());
	}
	std::u16string MoneyText(std::uint32_t amount) const
	{
		std::u16string format = Text("GUI:StartingMoneyFormat");
		const auto at = format.find(u"%d");
		return at == std::u16string::npos ? Number(amount) : format.substr(0, at) + Number(amount) + format.substr(at + 2);
	}
	std::u16string Format(std::u16string format, long long value) const
	{
		const auto at = format.find(u"%d");
		return at == std::u16string::npos ? format : format.substr(0, at) + Number(value) + format.substr(at + 2);
	}
	int PreferredColor() const
	{
		const int value = static_cast<int>(m_preferences.Number("Color", -1));
		return value < -1 || value >= static_cast<int>(m_catalog.colors.size()) ? -1 : value;
	}
	int PreferredFaction() const
	{
		const int value = static_cast<int>(m_preferences.Number("PlayerTemplate", -1));
		for (const SetupFaction &faction : m_catalog.factions)
			if (faction.playerTemplate == value)
				return value;
		return -1;
	}

	lan::LobbyGame *Game() const { return m_lobby != nullptr ? m_lobby->CurrentGame() : nullptr; }
	bool Host() const { return m_lobby != nullptr && m_lobby->AmIHost(); }
	int Local() const { return m_lobby != nullptr ? m_lobby->LocalSlot() : -1; }

	// Network.ini: our slot's choices; the host's game options too (OnGameStart, OnPlayerLeave).
	void SaveChoices()
	{
		const lan::LobbyGame *game = Game();
		if (game == nullptr || Local() < 0)
			return;
		const setup::GameSlot &me = game->setup.slots[static_cast<std::size_t>(Local())];
		m_preferences.Set("PlayerTemplate", static_cast<std::int64_t>(me.Observer() ? -1 : me.playerTemplate));
		m_preferences.Set("Color", static_cast<std::int64_t>(me.color));
		if (Host())
		{
			m_preferences.Set("Map", engine::core::text::EncodeQuotedPrintable(std::string_view(game->setup.map)));
			m_preferences.Set("SuperweaponRestrict", game->setup.superweaponRestriction != 0 ? "Yes" : "No");
			m_preferences.Set("StartingCash", static_cast<std::int64_t>(game->setup.startingCash));
		}
		if (m_services.savePreferences)
			m_services.savePreferences();
	}

	void AddChat(ChatLine line)
	{
		std::vector<ChatLine> lines = chat.Get();
		lines.push_back(std::move(line));
		if (lines.size() > ChatHistory)
			lines.erase(lines.begin());
		chat.Set(std::move(lines));
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

	// The host applies and tells everyone; a joiner asks.
	void Changed(bool unaccept)
	{
		if (Host())
		{
			if (unaccept)
				m_lobby->ResetAccepted();
			m_lobby->BroadcastOptions();
		}
		Refresh();
	}

	void Watch()
	{
		const auto on = [this](auto &observable, auto handler) {
			const auto id = observable.Subscribe([this, handler](const auto &value) {
				if (!m_updating)
					handler(value);
			});
			m_releases.push_back([&observable, id] { observable.Unsubscribe(id); });
		};
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			on(player[at], [this, index](int state) { PlayerPicked(index, state); });
			on(color[at], [this, index](int row) {
				lan::LobbyGame *game = Game();
				const auto &values = m_colorValues[static_cast<std::size_t>(index)];
				if (game == nullptr || row < 0 || static_cast<std::size_t>(row) >= values.size())
					return;
				const int chosen = values[static_cast<std::size_t>(row)];
				if (chosen != setup::Random && game->setup.ColorTaken(chosen, index))
					return Refresh();
				if (Host())
				{
					game->setup.slots[static_cast<std::size_t>(index)].color = chosen;
					Changed(false);
				}
				else if (index == Local())
					m_lobby->RequestGameOptions("Color=" + std::to_string(chosen));
			});
			on(faction[at], [this, index](int row) {
				lan::LobbyGame *game = Game();
				if (game == nullptr || row < 0 || static_cast<std::size_t>(row) > m_catalog.factions.size() + 1)
					return;
				const int chosen = row == 0 ? setup::Random
					: static_cast<std::size_t>(row) == m_catalog.factions.size() + 1 ? setup::ObserverTemplate
					: m_catalog.factions[static_cast<std::size_t>(row - 1)].playerTemplate;
				setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
				if (Host())
				{
					slot.playerTemplate = chosen;
					if (slot.Observer())
						slot.color = slot.startPos = slot.team = setup::Random;
					Changed(true);
				}
				else
					m_lobby->RequestGameOptions("PlayerTemplate=" + std::to_string(chosen));
			});
			on(team[at], [this, index](int row) {
				lan::LobbyGame *game = Game();
				if (game == nullptr || row < 0 || row > setup::MaxSlots / 2)
					return;
				if (Host())
				{
					game->setup.slots[static_cast<std::size_t>(index)].team = row - 1;
					Changed(true);
				}
				else
					m_lobby->RequestGameOptions("Team=" + std::to_string(row - 1));
			});
		}
		on(money, [this](int row) {
			lan::LobbyGame *game = Game();
			if (game == nullptr || !Host() || row < 0 || static_cast<std::size_t>(row) >= m_catalog.startingMoney.size())
				return;
			game->setup.startingCash = m_catalog.startingMoney[static_cast<std::size_t>(row)];
			Changed(true);
		});
		on(limitSuperweapons, [this](bool limited) {
			lan::LobbyGame *game = Game();
			if (game == nullptr || !Host())
				return;
			game->setup.superweaponRestriction = limited ? 1 : 0;
			Changed(true);
		});
		on(systemMaps, [this](bool) { FillMapList(); });
		on(mapSelected, [this](int row) {
			const SetupMap *map = row >= 0 && static_cast<std::size_t>(row) < m_mapRows.size() ? m_mapRows[static_cast<std::size_t>(row)] : nullptr;
			MapPreview shown;
			if (map != nullptr)
				shown = {true, map->preview, map->extentWidth, map->extentHeight, map->supplies, map->techs};
			mapListPreview.Set(shown);
			mapListStartSpots.Set(map != nullptr && map->multiplayer ? map->starts : std::vector<MapPoint>{});
		});
	}

	// ComboBoxPlayer<n> (host only): another state for a human sends it away; an AI's level
	// or an open or closed slot changes (every accept is dropped when an AI comes or goes).
	void PlayerPicked(int index, int state)
	{
		lan::LobbyGame *game = Game();
		if (game == nullptr || !Host() || index == Local() || state < 0 || state > static_cast<int>(setup::SlotState::HardAI))
			return Refresh();
		setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
		const auto next = static_cast<setup::SlotState>(state);
		if (slot.state == next)
			return;
		const bool wasHuman = slot.Human(), wasAi = slot.AI();
		slot.SetState(next);
		Changed(wasHuman || wasAi || slot.AI());
	}

	// UpdateSlotList / EnableAcceptControls / updateMapStartSpots / updateGameOptions.
	void Refresh()
	{
		const lan::LobbyGame *game = Game();
		if (game == nullptr)
			return;
		m_updating = true;
		const bool isHost = Host();
		const int local = Local();
		const SetupMap *map = m_catalog.Find(game->setup.map);
		const bool willTransfer = map == nullptr || !map->official;
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			const setup::GameSlot &slot = game->setup.slots[at];
			bool enabled = false;
			if (isHost && slot.AI())
				enabled = true;
			else if (index == local)
				enabled = (slot.accepted && !isHost) ? false : (slot.hasMap || willTransfer);
			slotEnabled[at].Set(enabled);
			playerEnabled[at].Set(isHost && index != 0 && index != local);
			if (slot.Human())
			{
				playerText[at].Set(slot.name);
				player[at].Set(-1);
			}
			else
			{
				playerText[at].Set({});
				player[at].Set(static_cast<int>(slot.state));
			}
			acceptShown[at].Set(slot.Human() && index != 0);
			accepted[at].Set(slot.accepted);
			// The colours no other slot has.
			std::vector<std::u16string> colors{slot.Observer() ? Text("GUI:None") : Text("GUI:???")};
			std::vector<std::uint32_t> rows{RandomRowColor};
			std::vector<int> values{setup::Random};
			if (!slot.Observer())
				for (int c = 0; c < static_cast<int>(m_catalog.colors.size()); ++c)
					if (!game->setup.ColorTaken(c, index))
					{
						colors.push_back(m_catalog.colors[static_cast<std::size_t>(c)].name);
						rows.push_back(m_catalog.colors[static_cast<std::size_t>(c)].rgba);
						values.push_back(c);
					}
			const auto chosen = std::find(values.begin(), values.end(), slot.color);
			colorItems[at].Set(std::move(colors));
			colorRowColors[at].Set(std::move(rows));
			color[at].Set(chosen == values.end() ? 0 : static_cast<int>(chosen - values.begin()));
			m_colorValues[at] = std::move(values);
			int factionRow = 0;
			for (std::size_t f = 0; f < m_catalog.factions.size(); ++f)
				if (m_catalog.factions[f].playerTemplate == slot.playerTemplate)
					factionRow = static_cast<int>(f) + 1;
			if (slot.Observer())
				factionRow = static_cast<int>(m_catalog.factions.size()) + 1;
			faction[at].Set(factionRow);
			team[at].Set(slot.team + 1);
		}
		mapName.Set(map != nullptr ? map->name : std::u16string(game->setup.map.begin(), game->setup.map.end()));
		MapPreview shown;
		if (map != nullptr)
			shown = {true, map->preview, map->extentWidth, map->extentHeight, map->supplies, map->techs};
		preview.Set(shown);
		startSpots.Set(map != nullptr && map->multiplayer ? map->starts : std::vector<MapPoint>{});
		for (int spot = 0; spot < setup::MaxSlots; ++spot)
		{
			const auto at = static_cast<std::size_t>(spot);
			startShown[at].Set(map != nullptr && map->multiplayer && spot < map->players);
			std::u16string number;
			for (int index = 0; index < setup::MaxSlots; ++index)
			{
				const setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
				if (slot.startPos == spot && map != nullptr && spot < map->players && slot.playerTemplate > setup::ObserverTemplate)
					number = Text("NUMBER:" + std::to_string(index + 1));
			}
			startNumber[at].Set(std::move(number));
		}
		int moneyRow = -1;
		for (std::size_t m = 0; m < m_catalog.startingMoney.size(); ++m)
			if (m_catalog.startingMoney[m] == game->setup.startingCash)
				moneyRow = static_cast<int>(m);
		money.Set(moneyRow);
		limitSuperweapons.Set(game->setup.superweaponRestriction != 0);
		startEnabled.Set(isHost ? m_lobby->StartEnabled() : !(local >= 0 && game->setup.slots[static_cast<std::size_t>(local)].accepted));
		m_updating = false;
	}

	// ButtonMapStartPosition<n>: a spot held by us or a host's AI passes on; a free spot is taken.
	void PickStartPosition(int spot)
	{
		lan::LobbyGame *game = Game();
		if (game == nullptr)
			return;
		const bool isHost = Host();
		const int local = Local();
		const auto selectable = [&](int index) {
			const setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
			return slot.startPos == setup::Random && ((index == local && !slot.Observer()) || (isHost && slot.AI()));
		};
		const auto set = [&](int index, int position) {
			if (isHost || index != local)
			{
				game->setup.slots[static_cast<std::size_t>(index)].startPos = position;
				Changed(true);
			}
			else
				m_lobby->RequestGameOptions("StartPos=" + std::to_string(position));
		};
		int holder = -1;
		for (int index = 0; index < setup::MaxSlots && holder < 0; ++index)
			if (game->setup.slots[static_cast<std::size_t>(index)].startPos == spot)
				holder = index;
		if (holder >= 0)
		{
			if (holder != local && !(isHost && game->setup.slots[static_cast<std::size_t>(holder)].AI()))
				return;
			int next = -1;
			if (isHost)
				for (int index = holder + 1; index < setup::MaxSlots && next < 0; ++index)
					if (selectable(index))
						next = index;
			set(holder, setup::Random);
			if (next >= 0)
				set(next, spot);
			return;
		}
		int next = -1;
		for (int index = 0; index < setup::MaxSlots && next < 0; ++index)
			if (selectable(index))
				next = index;
		if (next < 0)
			next = local;
		if (next >= 0)
			set(next, spot);
	}

	void ClearStartPosition(int spot)
	{
		lan::LobbyGame *game = Game();
		if (game == nullptr)
			return;
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const setup::GameSlot &slot = game->setup.slots[static_cast<std::size_t>(index)];
			if (slot.startPos != spot)
				continue;
			if (index == Local() && !Host())
				m_lobby->RequestGameOptions("StartPos=-1");
			else if (index == Local() || (Host() && slot.AI()))
			{
				game->setup.slots[static_cast<std::size_t>(index)].startPos = setup::Random;
				Changed(true);
			}
			return;
		}
	}

	// StartPressed (host) or RequestAccept (joiner).
	void StartPressed()
	{
		lan::LobbyGame *game = Game();
		if (game == nullptr)
			return;
		if (!Host())
		{
			m_lobby->RequestAccept();
			return;
		}
		game->setup.slots[0].accepted = true;
		int users = 0, humans = 0;
		for (const setup::GameSlot &slot : game->setup.slots)
			if (slot.Occupied() && !slot.Observer())
			{
				++users;
				humans += slot.Human() ? 1 : 0;
			}
		const SetupMap *map = m_catalog.Find(game->setup.map);
		if (map == nullptr || map->players < users)
		{
			m_lobby->SystemChat(Format(Text("LAN:TooManyPlayers"), map != nullptr ? map->players : 0));
			return;
		}
		if (humans == 0)
		{
			m_lobby->SystemChat(Text("GUI:NeedHumanPlayers"));
			return;
		}
		std::vector<int> teams;
		int randomTeams = 0;
		for (const setup::GameSlot &slot : game->setup.slots)
			if (slot.Occupied() && !slot.Observer())
			{
				if (slot.team < 0)
					++randomTeams;
				else if (std::find(teams.begin(), teams.end(), slot.team) == teams.end())
					teams.push_back(slot.team);
			}
		if (static_cast<int>(teams.size()) + randomTeams < 2)
			m_lobby->SystemChat(Text("GUI:SandboxMode"));
		bool everyone = true;
		for (const setup::GameSlot &slot : game->setup.slots)
			everyone = everyone && (!slot.Human() || slot.accepted);
		if (everyone)
		{
			for (int index = 0; index < setup::MaxSlots; ++index)
				if (game->setup.slots[static_cast<std::size_t>(index)].state == setup::SlotState::Open)
				{
					setup::GameSlot closed;
					closed.SetState(setup::SlotState::Closed);
					game->setup.SetSlot(index, closed);
				}
			m_lobby->SetStartEnabled(false);
			m_lobby->BroadcastOptions();
			if (m_countdown > 0)
				m_lobby->RequestGameStartTimer(m_countdown);
			else
				m_lobby->RequestGameStart();
			Refresh();
			return;
		}
		m_lobby->SystemChat(Text("GUI:NotifiedStartIntent"));
		m_lobby->RequestAccept();
	}

public:
	int m_countdown{5}; // MultiplayerSettings StartCountdownTimer

private:
	void OpenMapSelect()
	{
		if (!Host())
			return;
		const lan::LobbyGame *game = Game();
		const SetupMap *current = game != nullptr ? m_catalog.Find(game->setup.map) : nullptr;
		m_updating = true;
		systemMaps.Set(current == nullptr || current->official);
		m_updating = false;
		FillMapList();
		mainButtonsEnabled.Set(false);
		mapSelectOpen.Set(true);
	}

	void FillMapList()
	{
		m_mapRows = m_catalog.MapList(systemMaps.Get() ? "maps\\" : "userdata\\maps\\", true);
		const lan::LobbyGame *game = Game();
		std::vector<std::u16string> names;
		int chosen = 0;
		for (std::size_t row = 0; row < m_mapRows.size(); ++row)
		{
			names.push_back(m_mapRows[row]->name);
			if (game != nullptr && m_mapRows[row]->file == game->setup.map)
				chosen = static_cast<int>(row);
		}
		mapItems.Set(std::move(names));
		mapSelected.Set(-2);
		mapSelected.Set(m_mapRows.empty() ? -1 : chosen);
	}

	void CloseMapSelect()
	{
		mapSelectOpen.Set(false);
		mainButtonsEnabled.Set(true);
		Refresh();
	}

	// ButtonOK: the map with its size and CRC, the slots opened to its player count, every spot free,
	// every accept dropped; everyone hears.
	void ChooseMap()
	{
		lan::LobbyGame *game = Game();
		const int row = mapSelected.Get();
		if (game != nullptr && row >= 0 && static_cast<std::size_t>(row) < m_mapRows.size())
		{
			const SetupMap &map = *m_mapRows[static_cast<std::size_t>(row)];
			m_lobby->SetCurrentMap(map.file, map.crc, map.size);
			game->setup.ResetStartSpots();
			game->setup.AdjustSlotsForMap(map.players);
			m_lobby->ResetAccepted();
			m_lobby->BroadcastOptions();
		}
		CloseMapSelect();
	}

	ShellModel &m_model;
	MessageBoxViewModel &m_messages;
	const SetupCatalog &m_catalog;
	engine::config::Preferences &m_preferences;
	LanServices m_services;
	lan::LanLobby *m_lobby{nullptr};
	std::array<std::vector<int>, setup::MaxSlots> m_colorValues;
	std::vector<const SetupMap *> m_mapRows;
	bool m_updating{false};
	std::vector<std::function<void()>> m_releases;
};
}
