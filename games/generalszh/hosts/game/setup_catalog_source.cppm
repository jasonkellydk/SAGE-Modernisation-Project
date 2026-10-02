export module games.generalszh.hosts.game.setup_catalog_source;
import std;

import engine.localization.model.string_table;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.loading.content_loader;
import games.generalszh.content.maps.map_cache;
import games.generalszh.content.global.multiplayer_settings;
import games.generalszh.content.global.player_templates;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.content.ini.zero_hour_grammar;
import games.generalszh.content.objects.object_catalog;
import games.generalszh.session.setup.map_metadata;
import engine.level.adapters.generals_map.map_reader;
import games.generalszh.content.maps.map_strings;
export import games.generalszh.shell.game_setup.setup_catalog;

// The game setup screens' catalog from the install's content: Multiplayer.ini's
// colours and money, PlayerTemplate.ini's pickable factions (with
// ChallengeMode.ini's locked generals left out) and Maps\MapCache.ini's maps,
// named from Generals.csf.
export namespace generalszh::host
{
namespace detail
{
// The original's user map folder (MapCache::getUserMapDir: the user data path's Maps) brought up to date as
// MapCache::updateCache does: its MapCache.ini read, its maps checked (loadUserMaps, addMap), the file written again
// when anything changed (writeCacheINI(TRUE)). Maps described anew are loaded from disk and their objects' kinds looked
// up in Object.ini (loaded only then).
inline content::MapCache UpdateUserMapCache(content::ContentLoader &loader, const std::filesystem::path &userData)
{
	content::MapCache cache;
	if (userData.empty())
		return cache;
	const std::filesystem::path folder = userData / "Maps";
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	const std::filesystem::path cacheFile = folder / "MapCache.ini";
	const auto readFile = [](const std::filesystem::path &path) -> std::optional<std::vector<std::byte>> {
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return std::nullopt;
		std::vector<char> text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::vector<std::byte> bytes(text.size());
		std::memcpy(bytes.data(), text.data(), text.size());
		return bytes;
	};
	if (const auto bytes = readFile(cacheFile))
	{
		engine::config::Diagnostics diagnostics;
		engine::config::Document document;
		const auto grammar = content::ZeroHourIniGrammar(diagnostics);
		engine::config::ini::Read(document,
			document.AddSource(cacheFile.string(), std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size())), grammar, diagnostics);
		engine::config::BindContext context{diagnostics, engine::time::FixedStep{30}};
		cache = content::BindMapCache(document, context);
	}
	// TheFileSystem->getFileListInDirectory(<folder>\, *.map, recursive): a sorted set of paths.
	std::vector<content::MapFile> found;
	for (std::filesystem::recursive_directory_iterator entry(folder, error), end; !error && entry != end; entry.increment(error))
	{
		if (!entry->is_regular_file(error))
			continue;
		std::string extension = entry->path().extension().string();
		std::ranges::transform(extension, extension.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
		if (extension != ".map")
			continue;
		found.push_back({entry->path().string(), static_cast<std::uint32_t>(entry->file_size(error))});
	}
	std::sort(found.begin(), found.end(), [](const content::MapFile &left, const content::MapFile &right) { return left.path < right.path; });
	std::optional<engine::config::DefinitionTable<content::ObjectDefinition>> objects;
	const auto describe = [&](const content::MapFile &file) -> std::optional<content::MapMetaData> {
		const auto bytes = readFile(file.path);
		if (!bytes)
			return std::nullopt;
		const auto read = engine::level::generals_map::Read(*bytes);
		if (!read)
			return std::nullopt;
		if (!objects)
		{
			const engine::config::Document &objectSet = loader.Load({"Data/INI/Default/Object", "Data/INI/Object"});
			engine::config::BindContext objectContext{loader.DiagnosticsFor(objectSet), engine::time::FixedStep{30}};
			objects = content::BuildObjectCatalog(objectSet, objectContext);
		}
		return session::setup::DescribeMap(read->level, content::MapFileCrc(*bytes), [&](std::string_view type) {
			const content::ObjectDefinition *definition = objects->Find(std::string(type));
			if (definition == nullptr)
				return session::setup::MapPreviewKind::None;
			if (definition->Is("TECH_BUILDING"))
				return session::setup::MapPreviewKind::TechBuilding;
			if (definition->Is("SUPPLY_SOURCE_ON_PREVIEW"))
				return session::setup::MapPreviewKind::SupplyOnPreview;
			return session::setup::MapPreviewKind::None;
		});
	};
	const std::string folderKey = folder.string();
	if (content::UpdateUserMaps(cache, folderKey, found, describe))
	{
		std::ofstream out(cacheFile, std::ios::binary | std::ios::trunc);
		out << content::WriteMapCacheIni(cache, folderKey, cacheFile.string());
	}
	return cache;
}
}

inline shell::SetupCatalog LoadSetupCatalog(content::ContentLoader &loader, const engine::filesystem::VirtualFileSystem &files,
	const engine::localization::StringTable &strings, const std::filesystem::path &userData = {})
{
	const auto text = [&strings](std::string_view label) {
		const std::u16string found = Localized(strings, label);
		return found.empty() ? std::u16string(label.begin(), label.end()) : found;
	};
	const engine::time::FixedStep step{30};
	shell::SetupCatalog catalog;

	const engine::config::Document &multiplayerSet = loader.Load({"Data/INI/Default/Multiplayer", "Data/INI/Multiplayer"});
	engine::config::BindContext multiplayerContext{loader.DiagnosticsFor(multiplayerSet), step};
	const content::MultiplayerSettings settings = content::BindMultiplayerSettings(multiplayerSet, multiplayerContext);
	for (const content::MultiplayerColor &color : settings.colors)
		catalog.colors.push_back({text(color.tooltipName),
			(std::uint32_t{color.day.r} << 24) | (std::uint32_t{color.day.g} << 16) | (std::uint32_t{color.day.b} << 8) | 0xFFu});
	catalog.startingMoney = settings.startingMoney;
	catalog.defaultStartingMoney = settings.defaultStartingMoney;
	catalog.startCountdown = settings.startCountdownTimer;

	// PopulatePlayerTemplateComboBox: in store order, one per side, only those with a starting building
	// and not locked away as a challenge general.
	const engine::config::Document &templateSet = loader.Load({"Data/INI/Default/PlayerTemplate", "Data/INI/PlayerTemplate"});
	const engine::config::Document &challengeSet = loader.Load({"Data/INI/ChallengeMode"});
	engine::config::BindContext templateContext{loader.DiagnosticsFor(templateSet), step};
	const content::PlayerTemplates templates = content::BindPlayerTemplates(templateSet, &challengeSet, templateContext);
	catalog.playerTemplateCount = static_cast<int>(templates.templates.size());
	for (const content::PlayerTemplateInfo &faction : templates.templates)
		catalog.sideIcons.push_back(faction.sideIconImage);
	std::set<std::string> sides;
	for (std::size_t index = 0; index < templates.templates.size(); ++index)
	{
		const content::PlayerTemplateInfo &faction = templates.templates[index];
		if (faction.startingBuilding.empty() || faction.startsLocked || !sides.insert(faction.side).second)
			continue;
		catalog.factions.push_back({static_cast<int>(index), text("SIDE:" + faction.side), faction.armyTooltip});
	}

	const engine::config::Document &mapSet = loader.Load({"Maps/MapCache"});
	engine::config::BindContext mapContext{loader.DiagnosticsFor(mapSet), step};
	content::MapCache cache = content::BindMapCache(mapSet, mapContext);
	// MapCache::updateCache: the user's maps first, the standard ones read over them ("we shall overwrite info from
	// matching user maps"): a user map is listed unless the shipped cache has the same key.
	// The user's maps are keyed by their full path in their MapCache.ini (getUserMapDir), and read through the files'
	// "Maps\<name>\" mount of that folder (main.cpp): listed by that path.
	const std::string userFolder = (userData / "Maps").string();
	content::MapCache userCache = detail::UpdateUserMapCache(loader, userData);
	for (content::MapMetaData &user : userCache.maps)
	{
		const auto listed = content::UserMapFileKey(user.file, userFolder);
		if (!listed)
			continue;
		user.file = *listed;
		if (cache.Find(user.file) == nullptr)
		{
			const auto at = std::lower_bound(cache.maps.begin(), cache.maps.end(), user.file,
				[](const content::MapMetaData &each, const std::string &name) { return each.file < name; });
			cache.maps.insert(at, std::move(user));
		}
	}
	for (const content::MapMetaData &map : cache.maps)
	{
		shell::SetupMap shown;
		shown.file = map.file;
		// A user map's name tag is its own map.str's (MapCache::addMap: initMapStringFile on the map's folder); the
		// original reads it only while caching the map, showing the bare label afterwards, a retail quirk fixed here.
		engine::localization::StringTable mapStrings;
		if (!map.official && !map.nameLookupTag.empty())
			mapStrings = content::ReadMapStrings(files, map.file);
		// GameTextManager::fetch: Generals.csf first, then the map's strings.
		shown.name = map.DisplayName([&](const std::string &tag) {
			if (strings.Find(tag) == nullptr && mapStrings.Find(tag) != nullptr)
				return Localized(mapStrings, tag);
			return text(tag);
		});
		shown.players = map.players;
		shown.multiplayer = map.multiplayer;
		shown.official = map.official;
		shown.crc = map.crc;
		shown.size = map.fileSize;
		const std::int64_t left = map.extentMin.x.Floor(), bottom = map.extentMin.y.Floor();
		const std::int64_t width = (map.extentMax.x - map.extentMin.x).Floor(), height = (map.extentMax.y - map.extentMin.y).Floor();
		shown.extentWidth = static_cast<int>(width > 0 ? width : 1);
		shown.extentHeight = static_cast<int>(height > 0 ? height : 1);
		// A point per 10000 of the extent, from the top left (the preview's y runs down).
		const auto point = [&](const auto &at) {
			return shell::MapPoint{static_cast<int>((at.x.Floor() - left) * 10000 / shown.extentWidth),
				static_cast<int>(10000 - (at.y.Floor() - bottom) * 10000 / shown.extentHeight)};
		};
		for (const auto &start : map.starts)
			shown.starts.push_back(point(start));
		for (const auto &supply : map.supplies)
			shown.supplies.push_back(point(supply));
		for (const auto &tech : map.techs)
			shown.techs.push_back(point(tech));
		// getMapPreviewImage: the .tga beside the map.
		const std::string preview = map.file.substr(0, map.file.size() - 4) + ".tga";
		if (files.Exists(preview))
			shown.preview = preview;
		catalog.maps.push_back(std::move(shown));
	}
	return catalog;
}
}
