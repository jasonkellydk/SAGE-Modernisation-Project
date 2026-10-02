export module games.generalszh.content.maps.map_cache;
import std;

export import engine.config.binding.schema;
import engine.core.text.quoted_printable;
import Engine.Core.Math.FixedPresentation;

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
	bool hasInitialCamera{false}; // the map has an InitialCameraPosition waypoint (written to the cache only then)
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
			{
				map.initialCamera = ReadVec3(field, context).value_or(Engine::Math::FixedVector3{});
				map.hasInitialCamera = true;
			}
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


// Common/crc.h's CRC (the release build's computeCRC): each byte added to the running value shifted left by one, with
// the bit shifted out added back (MapUtil.cpp calcCRC: the whole map file, read in 4096-byte blocks, from 0).
constexpr std::uint32_t MapFileCrc(std::span<const std::byte> bytes, std::uint32_t crc = 0) noexcept
{
	for (const std::byte byte : bytes)
	{
		const std::uint32_t high = crc >> 31;
		crc = (crc << 1) + std::to_integer<std::uint32_t>(byte) + high;
	}
	return crc;
}

// MapCache::addMap: a cached map still holds when the file's size is the one cached and its CRC is set (the CRC itself
// is not computed again: a file changed but for its size is not noticed).
constexpr bool MapCacheEntryHolds(const MapMetaData *cached, std::uint32_t fileSize) noexcept
{
	return cached != nullptr && cached->fileSize == fileSize && cached->crc != 0;
}

// A map file found in the user's map folder: its path (any case; '/' or '\') and size (FileInfo sizeLow).
struct MapFile
{
	std::string path;
	std::uint32_t size{0};
};

namespace detail
{
inline std::string CacheKey(std::string_view path)
{
	std::string key(path);
	for (char &c : key)
		c = c == '/' ? '\\' : (c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
	return key;
}
}

// A user map's cache key (its full path, lower case) as the files list it, the user's map folder being mounted as
// "Maps" (main.cpp): "maps\<rest>"; none when the key is not in `folder`.
inline std::optional<std::string> UserMapFileKey(std::string_view key, std::string_view folder)
{
	const std::string prefix = detail::CacheKey(folder);
	if (!key.starts_with(prefix) || key.size() <= prefix.size() + 1 || key[prefix.size()] != '\\')
		return std::nullopt;
	return "maps\\" + std::string(key.substr(prefix.size() + 1));
}

// MapCache::loadUserMaps (the user's map folder, `folder`, after its MapCache.ini was read into `cache`) with addMap:
// each map file must sit in a folder of its own name ("x\x.map", else it is skipped); one whose cached entry still
// holds is kept, any other is described anew (`describe`: MapUtil's loadMap and what addMap takes from it; none:
// unreadable, left out) with its size, as not official; cached maps of the folder no longer found go
// (clearUnseenMaps). True when anything was described or removed (the folder's MapCache.ini is then written again:
// writeCacheINI(TRUE)).
inline bool UpdateUserMaps(MapCache &cache, std::string_view folder, std::span<const MapFile> found,
	const std::function<std::optional<MapMetaData>(const MapFile &)> &describe)
{
	std::set<std::string> seen;
	bool parsed = false;
	for (const MapFile &file : found)
	{
		const std::string key = detail::CacheKey(file.path);
		const auto slash = key.find_last_of('\\');
		if (slash == std::string::npos || !key.ends_with(".map"))
			continue;
		const std::string name = key.substr(slash + 1, key.size() - slash - 1 - 4);
		if (!key.ends_with(name + "\\" + name + ".map"))
			continue; // "Found map in wrong spot"
		seen.insert(key);
		if (MapCacheEntryHolds(cache.Find(key), file.size))
			continue;
		std::optional<MapMetaData> described = describe ? describe(file) : std::nullopt;
		if (!described)
			continue;
		described->file = key;
		described->fileSize = file.size;
		described->official = false;
		const auto at = std::lower_bound(cache.maps.begin(), cache.maps.end(), key,
			[](const MapMetaData &each, const std::string &name) { return each.file < name; });
		if (at != cache.maps.end() && at->file == key)
			*at = std::move(*described);
		else
			cache.maps.insert(at, std::move(*described));
		parsed = true;
	}
	const std::string prefix = detail::CacheKey(folder);
	const auto before = cache.maps.size();
	std::erase_if(cache.maps, [&](const MapMetaData &map) { return map.file.starts_with(prefix) && !seen.contains(map.file); });
	return parsed || cache.maps.size() != before;
}

// MapCache::writeCacheINI for the maps of `folder` (written as `path`): the header, then per map its size, CRC,
// timestamps (not kept: 0), whether official and multiplayer, its players, extent (%2.2f), name tag, waypoints in name
// order (InitialCameraPosition, then Player_1_Start...), tech and supply positions.
inline std::string WriteMapCacheIni(const MapCache &cache, std::string_view folder, std::string_view path)
{
	const auto vector = [](const Engine::Math::FixedVector3 &at) {
		return std::format("X:{:.2f} Y:{:.2f} Z:{:.2f}", Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z));
	};
	std::string out = std::format("; FILE: {} /////////////////////////////////////////////////////////////\n", path);
	out += "; This INI file is auto-generated - do not modify\n";
	out += "; /////////////////////////////////////////////////////////////////////////////\n";
	const std::string prefix = detail::CacheKey(folder);
	for (const MapMetaData &map : cache.maps)
	{
		if (!map.file.starts_with(prefix))
			continue;
		out += std::format("\nMapCache {}\n", engine::core::text::EncodeQuotedPrintable(map.file));
		out += std::format("  fileSize = {}\n", map.fileSize);
		out += std::format("  fileCRC = {}\n", map.crc);
		out += "  timestampLo = 0\n  timestampHi = 0\n";
		out += std::format("  isOfficial = {}\n", map.official ? "yes" : "no");
		out += std::format("  isMultiplayer = {}\n", map.multiplayer ? "yes" : "no");
		out += std::format("  numPlayers = {}\n", map.players);
		out += "  extentMin = " + vector(map.extentMin) + "\n";
		out += "  extentMax = " + vector(map.extentMax) + "\n";
		out += "  nameLookupTag = " + map.nameLookupTag + "\n";
		if (map.hasInitialCamera)
			out += "  InitialCameraPosition = " + vector(map.initialCamera) + "\n";
		for (std::size_t player = 0; player < map.starts.size(); ++player)
			out += std::format("  Player_{}_Start = ", player + 1) + vector(map.starts[player]) + "\n";
		for (const auto &tech : map.techs)
			out += "  techPosition = " + vector(tech) + "\n";
		for (const auto &supply : map.supplies)
			out += "  supplyPosition = " + vector(supply) + "\n";
		out += "END\n\n";
	}
	return out;
}
}
