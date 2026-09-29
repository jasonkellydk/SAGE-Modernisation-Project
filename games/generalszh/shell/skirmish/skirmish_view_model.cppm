export module games.generalszh.shell.skirmish.skirmish_view_model;
import std;

export import games.generalszh.shell.model.shell_model;
export import games.generalszh.shell.dialog.message_box_view_model;
export import games.generalszh.shell.game_setup.setup_catalog;
export import games.generalszh.session.setup.game_setup;
export import engine.config.adapters.preferences.preferences_file;
import engine.core.text.quoted_printable;

// The skirmish setup screen (the original's SkirmishGameOptionsMenu.cpp and
// SkirmishMapSelectMenu.cpp, without windows): the player in slot 0 and up
// to seven AIs, each slot's colour (only those no one else has), faction and
// team, the start spots on the map preview, the map (picked from the map
// list over the screen), the starting money, the superweapon limit and the
// game speed. It opens with Skirmish.ini's last setup and writes it back on
// Back and Start. Start checks the map and the player count.
export namespace generalszh::shell
{
namespace setup = generalszh::session::setup;

// What the host does for the screen (DI).
struct SkirmishServices
{
	std::function<std::u16string(std::string_view)> text;          // a label's text (Generals.csf)
	std::function<void()> savePreferences;                          // Skirmish.ini written
	std::function<std::int32_t()> seed;                             // GetTickCount
	std::function<void(const setup::GameSetup &, int fps)> start;   // the game starts (empty: not yet possible)
	std::function<std::u16string()> machineName;                    // the name when Skirmish.ini has none
};

// A battle honour as the record shows it (InsertBattleHonor): its image, dark until it is gained.
struct BattleHonor
{
	std::string image;
	bool gained{false};
	bool operator==(const BattleHonor &) const = default;
};

// BattleHonors.h's bits of SkirmishStats.ini's Honors.
namespace honor
{
inline constexpr std::uint32_t Streak = 0x2, BattleTank = 0x80, AirWing = 0x100, CampaignUsa = 0x800, CampaignChina = 0x1000, CampaignGla = 0x2000,
							   Blitz5 = 0x4000, Blitz10 = 0x8000, Apocalypse = 0x20000;
inline constexpr int GeneralTypes = 9; // MAX_GLOBAL_GENERAL_TYPES
}

// populateSkirmishBattleHonors: the honours earned in skirmish, campaigns and challenges
// (SkirmishStats.ini), in the original's order: campaigns, challenge, air wing, battle tank;
// then endurance, apocalypse, blitz, streak, domination, ultimate.
inline std::vector<std::vector<BattleHonor>> BattleHonors(const engine::config::Preferences &stats, const SetupCatalog &catalog)
{
	const auto number = [&](const std::string &key) { return stats.Number(key, 0); };
	const std::uint32_t honors = static_cast<std::uint32_t>(number("Honors"));
	const auto campaign = [&](const char *side, const char *prefix, std::uint32_t bit) {
		// GameDifficulty: 0 easy, 1 normal, 2 hard.
		const std::string key = std::string(side) + "Campaign_";
		if (number(key + "2") != 0)
			return BattleHonor{std::string(prefix) + "Campaign_G", true};
		if (number(key + "1") != 0)
			return BattleHonor{std::string(prefix) + "Campaign_S", true};
		if (number(key + "0") != 0)
			return BattleHonor{std::string(prefix) + "Campaign_B", true};
		return BattleHonor{std::string(prefix) + "Campaign_B", (honors & bit) != 0};
	};
	std::vector<BattleHonor> first{campaign("CHINA", "China", honor::CampaignChina), campaign("GLA", "GLA", honor::CampaignGla),
		campaign("USA", "USA", honor::CampaignUsa)};
	bool hard = false, normal = false, easy = false;
	for (int general = 0; general < honor::GeneralTypes; ++general)
	{
		const std::string key = "ChallengeCampaign" + std::to_string(general) + "_";
		hard = hard || number(key + "2") != 0;
		normal = normal || number(key + "1") != 0;
		easy = easy || number(key + "0") != 0;
	}
	first.push_back(hard ? BattleHonor{"Challenge_Gold", true} : normal ? BattleHonor{"Challenge_Silver", true}
		: easy ? BattleHonor{"Challenge_Bronz", true} : BattleHonor{"Challenge_Bronz", false});
	first.push_back({"HonorAirWing", (honors & honor::AirWing) != 0});
	first.push_back({"HonorBattleTank", (honors & honor::BattleTank) != 0});

	// Endurance: every official multiplayer map beaten at a level (SLOT_EASY_AI 2, SLOT_MED_AI 3, SLOT_BRUTAL_AI 4).
	bool missingEasy = false, missingMedium = false, missingBrutal = false, perfect = true;
	for (const SetupMap &map : catalog.maps)
	{
		if (!map.official || !map.multiplayer)
			continue;
		const bool beatEasy = number(map.file + "_2") != 0, beatMedium = number(map.file + "_3") != 0, beatBrutal = number(map.file + "_4") != 0;
		missingEasy = missingEasy || (!beatEasy && !beatMedium && !beatBrutal);
		missingMedium = missingMedium || (!beatMedium && !beatBrutal);
		missingBrutal = missingBrutal || !beatBrutal;
		if (perfect && number(map.file + "_4") < map.players - 1)
			perfect = false;
	}
	std::vector<BattleHonor> second;
	second.push_back(!missingBrutal ? BattleHonor{"Endurance_G", true} : !missingMedium ? BattleHonor{"Endurance_S", true}
		: !missingEasy ? BattleHonor{"Endurance_B", true} : BattleHonor{"Endurance_B", false});
	second.push_back({"Apocalypse", (honors & honor::Apocalypse) != 0});
	second.push_back((honors & honor::Blitz5) != 0 ? BattleHonor{"HonorBlitz5", true}
		: (honors & honor::Blitz10) != 0 ? BattleHonor{"HonorBlitz10", true} : BattleHonor{"HonorBlitz10", false});
	const auto streak = number("BestWinStreak");
	second.push_back(streak >= 1000 ? BattleHonor{"HonorStreak_1000", true} : streak >= 500 ? BattleHonor{"HonorStreak_500", true}
		: streak >= 100 ? BattleHonor{"HonorStreak_100", true} : streak >= 25 ? BattleHonor{"HonorStreak_G", true}
		: streak >= 10 ? BattleHonor{"HonorStreak_S", true} : streak >= 3 ? BattleHonor{"HonorStreak_B", true} : BattleHonor{"HonorStreak_B", false});
	const auto won = number("Wins");
	second.push_back(won >= 10000 ? BattleHonor{"Domination_10000", true} : won >= 1000 ? BattleHonor{"Domination_1000", true}
		: won >= 500 ? BattleHonor{"Domination_500", true} : won >= 100 ? BattleHonor{"Domination_100", true} : BattleHonor{"Domination_100", false});
	second.push_back({"Ultimate", perfect});
	// A spacer row above each row of honours.
	return {{}, first, {}, second};
}

// The map preview (W3DDrawMapPreview): letterboxed to the map's extent, supplies and tech buildings over it.
struct MapPreview
{
	bool known{false};
	std::string image;
	int extentWidth{1}, extentHeight{1};
	std::vector<MapPoint> supplies, techs;
	bool operator==(const MapPreview &) const = default;
};

inline constexpr int MinGameSpeed = 15, MaxGameSpeed = 61, NoFpsLimit = 60; // the slider; past 60 is no limit
inline constexpr std::uint32_t RandomRowColor = 0xFFFFFFFF;                 // MultiplayerSettings' random colour (white)

class SkirmishViewModel
{
public:
	SkirmishViewModel(ShellModel &model, MessageBoxViewModel &messages, const SetupCatalog &catalog, engine::config::Preferences &preferences,
		SkirmishServices services, int defaultFps = 30)
		: m_model(model), m_messages(messages), m_catalog(catalog), m_preferences(preferences), m_services(std::move(services)), m_defaultFps(defaultFps)
	{
		back.SetAction([this] {
			WritePreferences();
			m_model.Pop();
		});
		escape.SetAction([this] {
			if (mapSelectOpen.Get())
				mapBack.Execute();
			else
				back.Execute();
		});
		start.SetAction([this] { Start(); });
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
	~SkirmishViewModel()
	{
		for (auto &release : m_releases)
			release();
	}
	SkirmishViewModel(const SkirmishViewModel &) = delete;
	SkirmishViewModel &operator=(const SkirmishViewModel &) = delete;

	// SkirmishGameOptionsMenuInit: the last setup from Skirmish.ini (else the player and an AI
	// matching their record), a fresh seed, the lists filled.
	void Open(const engine::config::Preferences &stats = {})
	{
		m_stats = stats;
		const auto record = [&](const char *key) { return static_cast<int>(stats.Number(key, 0)); };
		m_updating = true;
		m_setup = setup::GameSetup{};
		m_setup.seed = m_services.seed ? m_services.seed() : 0;
		setup::GameSlot me;
		me.SetState(setup::SlotState::Player, UserName());
		me.color = PreferredColor();
		me.playerTemplate = PreferredFaction();
		m_setup.SetSlot(0, me);
		setup::GameSlot ai = me;
		ai.SetState(record("Wins") > 10 ? setup::SlotState::HardAI : record("Wins") > 5 ? setup::SlotState::MediumAI : setup::SlotState::EasyAI);
		m_setup.SetSlot(1, ai);
		// ParseAsciiStringToGameInfo(prefs.getSlotList()): the whole last setup, when it reads.
		if (const auto slots = m_preferences.Find("SlotList"))
			if (auto last = setup::ParseOptionsString(*slots, {static_cast<int>(m_catalog.colors.size()), m_catalog.playerTemplateCount}))
				for (int index = 0; index < setup::MaxSlots; ++index)
					m_setup.SetSlot(index, last->slots[static_cast<std::size_t>(index)]);
		m_setup.startingCash = static_cast<std::uint32_t>(m_preferences.Number("StartingCash", m_catalog.defaultStartingMoney));
		m_setup.superweaponRestriction = m_preferences.Flag("SuperweaponRestrict", false) ? 1 : 0;
		SetMap(PreferredMap());
		gameSpeed.Set(std::clamp(static_cast<int>(m_preferences.Number("FPS", m_defaultFps)), MinGameSpeed, MaxGameSpeed));
		wins.Set(Number(record("Wins")));
		losses.Set(Number(record("Losses")));
		streak.Set(Number(record("WinStreak")));
		bestStreak.Set(Number(record("BestWinStreak")));
		honors.Set(BattleHonors(stats, m_catalog));
		// The fixed lists: player types, factions, teams, money.
		std::vector<std::u16string> types{Text("GUI:Open"), Text("GUI:Closed"), Text("GUI:EasyAI"), Text("GUI:MediumAI"), Text("GUI:HardAI")};
		std::vector<std::u16string> factions{Text("GUI:Random")};
		for (const SetupFaction &faction : m_catalog.factions)
			factions.push_back(faction.name);
		std::vector<std::u16string> teams{Text("Team:0")};
		for (int team = 1; team <= setup::MaxSlots / 2; ++team)
			teams.push_back(Text("Team:" + std::to_string(team)));
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			playerItems[at].Set(index == 0 ? std::vector<std::u16string>{} : types);
			factionItems[at].Set(factions);
			teamItems[at].Set(teams);
		}
		std::vector<std::u16string> money;
		for (const std::uint32_t amount : m_catalog.startingMoney)
			money.push_back(Money(amount));
		moneyItems.Set(std::move(money));
		mapSelectOpen.Set(false);
		m_updating = false;
		Refresh();
	}

	// The setup as it stands (read by the host and the tests).
	const setup::GameSetup &Setup() const noexcept { return m_setup; }

	// Per slot: its player type (slots 1-7), colour, faction and team boxes, and whether they take input.
	std::array<engine::gui::mvvm::Observable<std::vector<std::u16string>>, setup::MaxSlots> playerItems, colorItems, factionItems, teamItems;
	std::array<engine::gui::mvvm::Observable<std::vector<std::uint32_t>>, setup::MaxSlots> colorRowColors;
	std::array<engine::gui::mvvm::Observable<int>, setup::MaxSlots> player, color, faction, team;
	std::array<engine::gui::mvvm::Observable<bool>, setup::MaxSlots> slotEnabled;
	engine::gui::mvvm::Observable<std::u16string> playerName;
	// The map: its name, preview and start spots (their numbers, where they sit, whether shown).
	engine::gui::mvvm::Observable<std::u16string> mapName;
	engine::gui::mvvm::Observable<MapPreview> preview;
	std::array<engine::gui::mvvm::Observable<std::u16string>, setup::MaxSlots> startText;
	std::array<engine::gui::mvvm::Observable<bool>, setup::MaxSlots> startShown;
	engine::gui::mvvm::Observable<std::vector<MapPoint>> startSpots;
	std::array<engine::gui::mvvm::Command, setup::MaxSlots> startPosition, startPositionClear;
	// The options.
	engine::gui::mvvm::Observable<std::vector<std::u16string>> moneyItems;
	engine::gui::mvvm::Observable<int> money{-1};
	engine::gui::mvvm::Observable<bool> limitSuperweapons{false};
	engine::gui::mvvm::Observable<int> gameSpeed{30};
	engine::gui::mvvm::Observable<std::u16string> gameSpeedText;
	engine::gui::mvvm::Observable<bool> gameSpeedIsDefault{true}; // the number shows disabled at the game's own speed
	// The record (SkirmishStats.ini).
	engine::gui::mvvm::Observable<std::u16string> wins, losses, streak, bestStreak;
	engine::gui::mvvm::Observable<std::vector<std::vector<BattleHonor>>> honors;
	// The map list over the screen (SkirmishMapSelectMenu.wnd).
	engine::gui::mvvm::Observable<bool> mapSelectOpen{false};
	engine::gui::mvvm::Observable<bool> mainButtonsEnabled{true}; // Back takes no input under the map list
	engine::gui::mvvm::Observable<bool> systemMaps{true};
	engine::gui::mvvm::Observable<std::vector<std::u16string>> mapItems;
	engine::gui::mvvm::Observable<std::vector<std::string>> mapMedals; // each row's medal image (empty: none)
	engine::gui::mvvm::Observable<int> mapSelected{-1};
	engine::gui::mvvm::Observable<MapPreview> mapListPreview;
	engine::gui::mvvm::Observable<std::vector<MapPoint>> mapListStartSpots;
	engine::gui::mvvm::Command back, escape, start, selectMap, mapOk, mapBack;

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
	// GUI:StartingMoneyFormat ("$%d"), as formatMoneyForStartingCashComboBox.
	std::u16string Money(std::uint32_t amount) const
	{
		std::u16string format = Text("GUI:StartingMoneyFormat");
		const auto at = format.find(u"%d");
		if (at == std::u16string::npos)
			return Number(amount);
		return format.substr(0, at) + Number(amount) + format.substr(at + 2);
	}

	// SkirmishPreferences.
	std::u16string UserName() const
	{
		if (const auto name = m_preferences.Find("UserName"))
		{
			std::u16string decoded = engine::core::text::DecodeQuotedPrintableWide(*name);
			while (!decoded.empty() && decoded.back() == u' ')
				decoded.pop_back();
			while (!decoded.empty() && decoded.front() == u' ')
				decoded.erase(decoded.begin());
			if (!decoded.empty())
				return decoded;
		}
		return m_services.machineName ? m_services.machineName() : Text("GUI:Player");
	}
	int PreferredColor() const
	{
		const int color = static_cast<int>(m_preferences.Number("Color", setup::Random));
		return color < -1 || color >= static_cast<int>(m_catalog.colors.size()) ? setup::Random : color;
	}
	int PreferredFaction() const
	{
		const int faction = static_cast<int>(m_preferences.Number("PlayerTemplate", setup::Random));
		for (const SetupFaction &each : m_catalog.factions)
			if (each.playerTemplate == faction)
				return faction;
		return setup::Random;
	}
	std::string PreferredMap() const
	{
		if (const auto map = m_preferences.Find("Map"))
		{
			std::string decoded = engine::core::text::DecodeQuotedPrintable(*map);
			if (const SetupMap *found = m_catalog.Find(decoded); found != nullptr && found->multiplayer) // isValidMap
				return found->file;
		}
		return m_catalog.DefaultMap();
	}
	void WritePreferences()
	{
		m_preferences.Set("Color", static_cast<std::int64_t>(m_setup.slots[0].color));
		m_preferences.Set("PlayerTemplate", static_cast<std::int64_t>(m_setup.slots[0].playerTemplate));
		m_preferences.Set("Map", m_setup.map);
		m_preferences.Set("UserName", engine::core::text::EncodeQuotedPrintable(std::u16string_view(m_setup.slots[0].name)));
		m_preferences.Set("StartingCash", static_cast<std::int64_t>(m_setup.startingCash));
		m_preferences.Set("SuperweaponRestrict", m_setup.superweaponRestriction != 0 ? "Yes" : "No");
		m_preferences.Set("SlotList", setup::ToOptionsString(m_setup));
		m_preferences.Set("FPS", static_cast<std::int64_t>(gameSpeed.Get()));
		if (m_services.savePreferences)
			m_services.savePreferences();
	}

	void SetMap(const std::string &file)
	{
		m_setup.map = file;
		const SetupMap *map = m_catalog.Find(file);
		m_setup.mapCrc = map != nullptr ? map->crc : 0;
		m_setup.mapSize = map != nullptr ? map->size : 0;
	}

	static MapPreview PreviewOf(const SetupMap *map)
	{
		MapPreview shown;
		if (map == nullptr)
			return shown;
		shown.known = true;
		shown.image = map->preview;
		shown.extentWidth = map->extentWidth;
		shown.extentHeight = map->extentHeight;
		shown.supplies = map->supplies;
		shown.techs = map->techs;
		return shown;
	}

	// Each box's changes from the pointer carried to the setup (GCM_SELECTED, GEM_UPDATE_TEXT, ...).
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
			on(player[at], [this, index](int state) {
				if (index == 0 || state < 0 || state > static_cast<int>(setup::SlotState::HardAI))
					return;
				m_setup.slots[static_cast<std::size_t>(index)].SetState(static_cast<setup::SlotState>(state)); // handlePlayerSelection
				Refresh();
			});
			on(color[at], [this, index](int row) {
				const auto &values = m_colorValues[static_cast<std::size_t>(index)];
				if (row < 0 || static_cast<std::size_t>(row) >= values.size())
					return;
				const int chosen = values[static_cast<std::size_t>(row)];
				if (chosen == setup::Random || !m_setup.ColorTaken(chosen, index)) // handleColorSelection
					m_setup.slots[static_cast<std::size_t>(index)].color = chosen;
				Refresh();
			});
			on(faction[at], [this, index](int row) {
				if (row < 0 || static_cast<std::size_t>(row) > m_catalog.factions.size())
					return;
				m_setup.slots[static_cast<std::size_t>(index)].playerTemplate =
					row == 0 ? setup::Random : m_catalog.factions[static_cast<std::size_t>(row - 1)].playerTemplate;
				Refresh();
			});
			on(team[at], [this, index](int row) {
				if (row < 0 || row > setup::MaxSlots / 2)
					return;
				m_setup.slots[static_cast<std::size_t>(index)].team = row - 1;
				Refresh();
			});
		}
		on(playerName, [this](const std::u16string &name) { m_setup.slots[0].name = name; });
		on(money, [this](int row) {
			if (row >= 0 && static_cast<std::size_t>(row) < m_catalog.startingMoney.size())
				m_setup.startingCash = m_catalog.startingMoney[static_cast<std::size_t>(row)];
			Refresh();
		});
		on(limitSuperweapons, [this](bool limited) { m_setup.superweaponRestriction = limited ? 1 : 0; });
		on(gameSpeed, [this](int) { ShowGameSpeed(); });
		on(systemMaps, [this](bool) { FillMapList(); });
		on(mapSelected, [this](int row) {
			const SetupMap *map = row >= 0 && static_cast<std::size_t>(row) < m_mapRows.size() ? m_mapRows[static_cast<std::size_t>(row)] : nullptr;
			mapListPreview.Set(PreviewOf(map));
			mapListStartSpots.Set(map != nullptr && map->multiplayer ? map->starts : std::vector<MapPoint>{});
		});
	}

	// setFPSTextBox.
	void ShowGameSpeed()
	{
		const int speed = gameSpeed.Get();
		gameSpeedText.Set(speed > NoFpsLimit ? std::u16string(u"--") : (speed < 10 ? u" " : u"") + Number(speed));
		gameSpeedIsDefault.Set(speed == m_defaultFps);
	}

	// skirmishUpdateSlotList / UpdateSlotList / updateMapStartSpots / updateSkirmishGameOptions.
	void Refresh()
	{
		m_updating = true;
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			const setup::GameSlot &slot = m_setup.slots[at];
			slotEnabled[at].Set(index == 0 || slot.AI()); // EnableAcceptControls: the host's own slot and its AIs
			if (index > 0)
				player[at].Set(static_cast<int>(slot.state));
			// PopulateColorComboBox: "???" and the colours no other slot has.
			std::vector<std::u16string> colors{slot.Observer() ? Text("GUI:None") : Text("GUI:???")};
			std::vector<std::uint32_t> rows{RandomRowColor};
			std::vector<int> values{setup::Random};
			if (!slot.Observer())
				for (int c = 0; c < static_cast<int>(m_catalog.colors.size()); ++c)
					if (!m_setup.ColorTaken(c, index))
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
			faction[at].Set(factionRow);
			team[at].Set(slot.team + 1);
		}
		playerName.Set(m_setup.slots[0].name);
		const SetupMap *map = m_catalog.Find(m_setup.map);
		mapName.Set(map != nullptr ? map->name : std::u16string(m_setup.map.begin(), m_setup.map.end()));
		preview.Set(PreviewOf(map));
		startSpots.Set(map != nullptr && map->multiplayer ? map->starts : std::vector<MapPoint>{});
		for (int spot = 0; spot < setup::MaxSlots; ++spot)
		{
			const auto at = static_cast<std::size_t>(spot);
			startShown[at].Set(map != nullptr && map->multiplayer && spot < map->players);
			std::u16string number;
			for (int index = 0; index < setup::MaxSlots; ++index)
			{
				const setup::GameSlot &slot = m_setup.slots[static_cast<std::size_t>(index)];
				if (slot.startPos == spot && map != nullptr && spot < map->players && slot.playerTemplate > setup::ObserverTemplate)
					number = Text("NUMBER:" + std::to_string(index + 1));
			}
			startText[at].Set(std::move(number));
		}
		int moneyRow = -1;
		for (std::size_t m = 0; m < m_catalog.startingMoney.size(); ++m)
			if (m_catalog.startingMoney[m] == m_setup.startingCash)
				moneyRow = static_cast<int>(m);
		money.Set(moneyRow);
		limitSuperweapons.Set(m_setup.superweaponRestriction != 0);
		ShowGameSpeed();
		m_updating = false;
	}

	int NextSelectablePlayer(int from) const // getNextSelectablePlayer
	{
		for (int index = from; index < setup::MaxSlots; ++index)
		{
			const setup::GameSlot &slot = m_setup.slots[static_cast<std::size_t>(index)];
			if (slot.startPos == setup::Random && (index == 0 || slot.AI()))
				return index;
		}
		return -1;
	}

	void SetStart(int index, int position) // handleStartPositionSelection
	{
		setup::GameSlot &slot = m_setup.slots[static_cast<std::size_t>(index)];
		if (position == slot.startPos)
			return;
		if (position < 0 || !m_setup.StartTaken(position, index))
			slot.startPos = position;
	}

	// GBM_SELECTED on ButtonMapStartPosition<spot>: a free spot goes to the next player without one;
	// a taken spot passes from its player (the host's own or an AI) to the next one without.
	void PickStartPosition(int spot)
	{
		int holder = -1;
		for (int index = 0; index < setup::MaxSlots && holder < 0; ++index)
			if (m_setup.slots[static_cast<std::size_t>(index)].startPos == spot)
				holder = index;
		if (holder >= 0)
		{
			if (holder == 0 || m_setup.slots[static_cast<std::size_t>(holder)].AI())
			{
				const int next = NextSelectablePlayer(holder + 1);
				SetStart(holder, setup::Random);
				if (next >= 0)
					SetStart(next, spot);
			}
		}
		else
		{
			int next = NextSelectablePlayer(0);
			if (next < 0)
				next = 0;
			SetStart(next, spot);
		}
		Refresh();
	}

	// GBM_SELECTED_RIGHT on a start spot: its player (the host's own or an AI) gives it up.
	void ClearStartPosition(int spot)
	{
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const setup::GameSlot &slot = m_setup.slots[static_cast<std::size_t>(index)];
			if (slot.startPos != spot)
				continue;
			if (index == 0 || slot.AI())
				SetStart(index, setup::Random);
			break;
		}
		Refresh();
	}

	void OpenMapSelect()
	{
		const SetupMap *current = m_catalog.Find(m_setup.map);
		m_updating = true;
		systemMaps.Set(current == nullptr || current->official); // usesSystemMapDir
		m_updating = false;
		FillMapList();
		mainButtonsEnabled.Set(false);
		mapSelectOpen.Set(true);
	}

	void FillMapList()
	{
		m_mapRows = m_catalog.MapList(systemMaps.Get() ? "maps\\" : "userdata\\maps\\", true);
		std::vector<std::u16string> names;
		std::vector<std::string> medals;
		int chosen = 0; // always select *something*
		for (std::size_t row = 0; row < m_mapRows.size(); ++row)
		{
			// addMapToMapListbox: the best level of AI the player beat on it (SLOT_EASY_AI 2 .. SLOT_BRUTAL_AI 4);
			// every opponent slot beaten at the hardest earns the red and yellow star.
			const SetupMap &map = *m_mapRows[row];
			const auto beaten = [&](int level) { return m_stats.Number(map.file + "_" + std::to_string(level), 0); };
			std::string medal;
			if (const auto brutal = beaten(4); brutal != 0)
				medal = brutal == map.players - 1 ? "RedYell_Star" : "Star-Gold";
			else if (beaten(3) != 0)
				medal = "Star-Silver";
			else if (beaten(2) != 0)
				medal = "Star-Bronze";
			medals.push_back(std::move(medal));
			names.push_back(u'\t' + m_mapRows[row]->name); // the medal column (SkirmishStats.ini) first
			if (m_mapRows[row]->file == m_setup.map)
				chosen = static_cast<int>(row);
		}
		mapMedals.Set(std::move(medals));
		mapItems.Set(std::move(names));
		mapSelected.Set(-2); // shown again even when the row is the same
		mapSelected.Set(m_mapRows.empty() ? -1 : chosen);
	}

	void CloseMapSelect()
	{
		mapSelectOpen.Set(false);
		mainButtonsEnabled.Set(true);
		Refresh();
	}

	// ButtonOK: the picked map, its size and CRC; every start spot free again.
	void ChooseMap()
	{
		const int row = mapSelected.Get();
		if (row >= 0 && static_cast<std::size_t>(row) < m_mapRows.size())
		{
			SetMap(m_mapRows[static_cast<std::size_t>(row)]->file);
			m_setup.ResetStartSpots();
		}
		CloseMapSelect();
	}

	// startPressed: the map must be known and hold every player.
	void Start()
	{
		const SetupMap *map = m_catalog.Find(m_setup.map);
		if (map == nullptr)
		{
			m_messages.Show(MessageBoxOk(Text("GUI:ErrorStartingGame"), Text("GUI:CantFindMap")));
			return;
		}
		if (m_setup.Players() > map->players)
		{
			std::u16string message = Text("GUI:TooManyPlayers");
			if (const auto at = message.find(u"%d"); at != std::u16string::npos)
				message = message.substr(0, at) + Number(map->players) + message.substr(at + 2);
			m_messages.Show(MessageBoxOk(Text("GUI:ErrorStartingGame"), message));
			return;
		}
		WritePreferences();
		// reallyDoStart: the speed slider past 60 is no limit (1000); never under 15.
		const int fps = gameSpeed.Get() > NoFpsLimit ? 1000 : std::max(gameSpeed.Get(), MinGameSpeed);
		if (m_services.start)
			m_services.start(m_setup, fps);
		else
			m_messages.Show(MessageBoxOk(Text("GUI:ErrorStartingGame"), u"Skirmish games cannot be played yet."));
	}

	ShellModel &m_model;
	MessageBoxViewModel &m_messages;
	const SetupCatalog &m_catalog;
	engine::config::Preferences &m_preferences;
	SkirmishServices m_services;
	int m_defaultFps;
	setup::GameSetup m_setup;
	std::array<std::vector<int>, setup::MaxSlots> m_colorValues;
	std::vector<const SetupMap *> m_mapRows;
	engine::config::Preferences m_stats; // SkirmishStats.ini as the screen opened
	bool m_updating{false};
	std::vector<std::function<void()>> m_releases;
};
}
