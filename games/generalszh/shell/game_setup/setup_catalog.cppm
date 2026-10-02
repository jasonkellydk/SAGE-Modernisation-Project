export module games.generalszh.shell.game_setup.setup_catalog;
import std;

// What the game setup screens (skirmish, LAN) pick from, as the host builds it
// from the game's content: the player colours (named, in their order), the
// factions a player may pick (PopulatePlayerTemplateComboBox's: one per side,
// playable, not locked), the starting money choices and the maps
// (the MapCache). Plain data, localized, so the screens need no content.
export namespace generalszh::shell
{
struct SetupColor
{
	std::u16string name;
	std::uint32_t rgba{0xFFFFFFFF};
};

struct SetupFaction
{
	int playerTemplate{-1}; // the index a game setup names
	std::u16string name;    // SIDE:<side>
	std::string armyTooltip; // its PlayerTemplate's ArmyTooltip (a label: the faction box's tooltip)
};

// A point on a map as the preview shows it: per 10000 of the map's extent, from its top left.
struct MapPoint
{
	int x{0}, y{0};
	bool operator==(const MapPoint &) const = default;
};

struct SetupMap
{
	std::string file;    // portable, lower case
	std::u16string name; // shown ("Tournament Continent (4)")
	int players{0};
	bool multiplayer{false};
	bool official{false};
	std::uint32_t crc{0}, size{0};
	int extentWidth{1}, extentHeight{1};
	std::vector<MapPoint> starts, supplies, techs;
	std::string preview; // its preview image (the map's .tga), empty if none
};

struct SetupCatalog
{
	std::vector<SetupColor> colors;
	std::vector<SetupFaction> factions;
	int playerTemplateCount{15}; // every template, pickable or not (a setup names one by index)
	std::vector<std::string> sideIcons; // each template's SideIconImage, by index (GameInfoWindow's player icons)
	std::vector<std::uint32_t> startingMoney;
	std::uint32_t defaultStartingMoney{10000};
	int startCountdown{5}; // MultiplayerSettings StartCountdownTimer (LAN)
	std::vector<SetupMap> maps; // sorted by file (the MapCache)

	const SetupMap *Find(std::string_view file) const
	{
		std::string key(file);
		std::transform(key.begin(), key.end(), key.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
		for (const SetupMap &map : maps)
			if (map.file == key)
				return &map;
		return nullptr;
	}

	// populateMapListbox: the maps of a folder for a kind of game, by player count, then by name;
	// Zero Hour leaves out two maps it does not play.
	std::vector<const SetupMap *> MapList(std::string_view folder, bool multiplayer) const
	{
		std::vector<const SetupMap *> list;
		for (const SetupMap &map : maps)
			if (map.file.starts_with(folder) && map.multiplayer == multiplayer && !map.name.empty() && map.file != "maps\\armored fury\\armored fury.map" &&
				map.file != "maps\\scorched earth\\scorched earth.map" && map.players >= 1 && map.players <= 8)
				list.push_back(&map);
		// The original's names are a set ordered without case (rts::less_than_nocase).
		const auto lower = [](const std::u16string &name) {
			std::u16string folded = name;
			for (char16_t &c : folded)
				if (c >= u'A' && c <= u'Z')
					c = static_cast<char16_t>(c - u'A' + u'a');
			return folded;
		};
		std::stable_sort(list.begin(), list.end(), [&](const SetupMap *a, const SetupMap *b) {
			return a->players != b->players ? a->players < b->players : lower(a->name) < lower(b->name);
		});
		// Names equal but for case: the set keeps the first; the very same name: its
		// name-to-file map keeps the later file.
		std::vector<const SetupMap *> shown;
		for (const SetupMap *map : list)
			if (!shown.empty() && shown.back()->players == map->players && lower(shown.back()->name) == lower(map->name))
			{
				if (shown.back()->name == map->name)
					shown.back() = map;
			}
			else
				shown.push_back(map);
		return shown;
	}

	// getDefaultMap: the first multiplayer map of the cache.
	std::string DefaultMap() const
	{
		for (const SetupMap &map : maps)
			if (map.multiplayer)
				return map.file;
		return {};
	}
};
}
