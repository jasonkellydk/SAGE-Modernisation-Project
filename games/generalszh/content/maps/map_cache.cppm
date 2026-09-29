export module games.generalszh.content.maps.map_cache;
import std;

export import engine.config.binding.schema;
import engine.core.text.quoted_printable;

// Maps\MapCache.ini (the original's INI::parseMapCacheDefinition): what the
// menus know of each shipped map without loading it: its name tag, whether
// it is official and multiplayer, its player count and start spots, its
// extent, supplies and tech buildings, and its size and CRC (which the game
// setup carries so every machine plays the same file). Keyed by the map's
// portable path in lower case ("maps\alpine assault\alpine assault.map"),
// sorted as the original's MapCache (a std::map).
export namespace generalszh::content
{
struct MapMetaData
{
	std::string file;
	std::string nameLookupTag; // MAP:... (empty: the file's name is shown)
	bool official{false};
	bool multiplayer{false};
	int players{0};
	std::uint32_t fileSize{0};
	std::uint32_t crc{0};
	Engine::Math::FixedVector3 extentMin{}, extentMax{};
	Engine::Math::FixedVector3 initialCamera{};
	std::vector<Engine::Math::FixedVector3> starts; // Player_1_Start... one per player
	std::vector<Engine::Math::FixedVector3> supplies, techs;

	// The name the menus show: the tag's text (else the file's name), and " (N)" for 2 players or more.
	template<class Lookup>
	std::u16string DisplayName(Lookup &&lookup) const
	{
		std::u16string name;
		if (nameLookupTag.empty())
		{
			const auto slash = file.find_last_of('\\');
			const std::string leaf = slash == std::string::npos ? file : file.substr(slash + 1);
			name.assign(leaf.begin(), leaf.end());
		}
		else
			name = lookup(nameLookupTag);
		if (players >= 2)
		{
			const std::string count = " (" + std::to_string(players) + ")";
			name.append(count.begin(), count.end());
		}
		return name;
	}
};

struct MapCache
{
	std::vector<MapMetaData> maps; // sorted by file

	const MapMetaData *Find(std::string_view file) const
	{
		std::string key(file);
		std::transform(key.begin(), key.end(), key.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
		const auto at = std::lower_bound(maps.begin(), maps.end(), key, [](const MapMetaData &map, const std::string &name) { return map.file < name; });
		return at != maps.end() && at->file == key ? &*at : nullptr;
	}
};

inline MapCache BindMapCache(const engine::config::Document &document, engine::config::BindContext &context)
{
	using namespace engine::config;
	MapCache cache;
	for (const Node &root : document.Roots())
	{
		if (root.key != "MapCache" || root.values.empty())
			continue;
		MapMetaData map;
		map.file = engine::core::text::DecodeQuotedPrintable(root.values.front());
		std::transform(map.file.begin(), map.file.end(), map.file.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
		std::vector<std::optional<Engine::Math::FixedVector3>> starts(8);
		for (const Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "isOfficial")
				map.official = ReadBool(field, context).value_or(false);
			else if (key == "isMultiplayer")
				map.multiplayer = ReadBool(field, context).value_or(false);
			else if (key == "numPlayers")
				map.players = static_cast<int>(ReadInt(field, context).value_or(0));
			else if (key == "fileSize")
				map.fileSize = static_cast<std::uint32_t>(ReadInt(field, context).value_or(0));
			else if (key == "fileCRC")
				map.crc = static_cast<std::uint32_t>(ReadInt(field, context).value_or(0));
			else if (key == "nameLookupTag")
				map.nameLookupTag = engine::core::text::DecodeQuotedPrintable(ReadText(field));
			else if (key == "extentMin")
				map.extentMin = ReadVec3(field, context).value_or(Engine::Math::FixedVector3{});
			else if (key == "extentMax")
				map.extentMax = ReadVec3(field, context).value_or(Engine::Math::FixedVector3{});
			else if (key == "InitialCameraPosition")
				map.initialCamera = ReadVec3(field, context).value_or(Engine::Math::FixedVector3{});
			else if (key == "supplyPosition")
			{
				if (const auto at = ReadVec3(field, context))
					map.supplies.push_back(*at);
			}
			else if (key == "techPosition")
			{
				if (const auto at = ReadVec3(field, context))
					map.techs.push_back(*at);
			}
			else if (key.size() == 14 && key.starts_with("Player_") && key.ends_with("_Start") && key[7] >= '1' && key[7] <= '8')
				starts[static_cast<std::size_t>(key[7] - '1')] = ReadVec3(field, context);
		}
		for (int player = 0; player < std::min(map.players, 8); ++player)
			map.starts.push_back(starts[static_cast<std::size_t>(player)].value_or(Engine::Math::FixedVector3{}));
		const auto at = std::lower_bound(cache.maps.begin(), cache.maps.end(), map.file, [](const MapMetaData &each, const std::string &name) { return each.file < name; });
		if (at != cache.maps.end() && at->file == map.file)
			*at = std::move(map); // a later definition wins
		else
			cache.maps.insert(at, std::move(map));
	}
	return cache;
}
}
