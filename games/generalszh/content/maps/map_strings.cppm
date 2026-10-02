export module games.generalszh.content.maps.map_strings;
import std;

export import engine.localization.model.string_table;
export import engine.filesystem.core.virtual_file_system;
import engine.localization.adapters.str.str_reader;

// A map's own strings (GameLogic::startNewGame: TheGameText->initMapStringFile("<map folder>\map.str") when the file is
// there; GameTextManager::fetch looks a label up in Generals.csf first, then in them; GameEngine::reset drops them with
// the game). The map is named by its .map path.
export namespace generalszh::content
{
// "<folder>/<name>.map" -> "<folder>/map.str" ('/' or '\').
inline std::string MapStringsPath(std::string_view mapPath)
{
	const auto slash = mapPath.find_last_of("/\\");
	return slash == std::string_view::npos ? std::string("map.str") : std::string(mapPath.substr(0, slash + 1)) + "map.str";
}

// The map's map.str as a table (empty: none, or unreadable).
inline engine::localization::StringTable ReadMapStrings(const engine::filesystem::VirtualFileSystem &files, std::string_view mapPath)
{
	engine::localization::StringTable table;
	if (const auto text = files.ReadText(MapStringsPath(mapPath)))
		(void)engine::localization::str::Read(*text, table);
	return table;
}

// GameTextManager::fetch's order: the game's own strings first, then the map's.
inline const engine::localization::LocalizedString *FindGameOrMapString(const engine::localization::StringTable &game,
	const engine::localization::StringTable &map, std::string_view label)
{
	if (const auto *found = game.Find(label))
		return found;
	return map.Find(label);
}
}
