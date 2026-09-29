export module games.generalszh.hosts.game.setup_catalog_source;
import std;

import engine.localization.model.string_table;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.loading.content_loader;
import games.generalszh.content.maps.map_cache;
import games.generalszh.content.global.multiplayer_settings;
import games.generalszh.content.global.player_templates;
import games.generalszh.hosts.game.shell_menu;
export import games.generalszh.shell.game_setup.setup_catalog;

// The game setup screens' catalog from the install's content: Multiplayer.ini's
// colours and money, PlayerTemplate.ini's pickable factions (with
// ChallengeMode.ini's locked generals left out) and Maps\MapCache.ini's maps,
// named from Generals.csf.
export namespace generalszh::host
{
inline shell::SetupCatalog LoadSetupCatalog(content::ContentLoader &loader, const engine::filesystem::VirtualFileSystem &files,
	const engine::localization::StringTable &strings)
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
	std::set<std::string> sides;
	for (std::size_t index = 0; index < templates.templates.size(); ++index)
	{
		const content::PlayerTemplateInfo &faction = templates.templates[index];
		if (faction.startingBuilding.empty() || faction.startsLocked || !sides.insert(faction.side).second)
			continue;
		catalog.factions.push_back({static_cast<int>(index), text("SIDE:" + faction.side)});
	}

	const engine::config::Document &mapSet = loader.Load({"Maps/MapCache"});
	engine::config::BindContext mapContext{loader.DiagnosticsFor(mapSet), step};
	const content::MapCache cache = content::BindMapCache(mapSet, mapContext);
	for (const content::MapMetaData &map : cache.maps)
	{
		shell::SetupMap shown;
		shown.file = map.file;
		shown.name = map.DisplayName([&](const std::string &tag) { return text(tag); });
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
