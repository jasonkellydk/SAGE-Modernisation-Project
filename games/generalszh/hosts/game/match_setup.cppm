module;
#include <cstdio>

export module games.generalszh.hosts.game.match_setup;
import std;

import engine.filesystem.core.virtual_file_system;
import engine.level.adapters.generals_map.map_reader;
export import engine.level.model.level;
export import engine.localization.model.string_table;
export import games.generalszh.content.global.campaigns;
export import games.generalszh.content.global.player_templates;
export import games.generalszh.session.setup.match_plan;
export import games.generalszh.session.session_view;
export import games.generalszh.hosts.game.game_client;
import games.generalszh.session.setup.skirmish_level;
import games.generalszh.hosts.game.shell_menu;

// GameLogic::startNewGame's preparation of a match from its plan, before the game starts on it: the map read and made
// ready for its kind (a skirmish or LAN game's setup, a campaign's mission, a Generals' Challenge), the humans' seats,
// the players' starts, the local player's own scripts, where the camera starts, the difficulty and seed.
export namespace generalszh::host
{
struct PreparedMatch
{
	std::string path; // the map
	engine::level::Level level;
	std::vector<std::string> seats; // the humans' players, in seat order
	std::vector<PlayerStart> starts;
	std::vector<engine::level::ScriptList> localLists; // the scripts that run on this machine only
	std::string cameraMarker{"InitialCameraPosition"};
	std::optional<std::uint8_t> solo; // a campaign's difficulty (GAME_SINGLE_PLAYER)
	std::uint64_t seed{0};
	std::size_t players{0};
	bool challenge{false};
	std::optional<session::NetworkMatchOptions> network;
	const content::Mission *mission{nullptr};
	// The names its messages give the players and the defeat message's (a game from a setup only).
	std::optional<std::vector<std::pair<std::string, std::u16string>>> playerNames;
	std::u16string defeatedText;
};

// A skirmish: its map made ready for its setup, its seats' players starting there, the local player's side scripts and
// MultiplayerScripts.scb's run locally. A campaign's mission (GAME_SINGLE_PLAYER): its map with its own players, the
// local human playing its human side at the campaign's difficulty; a Generals' Challenge's: its map with the human added
// as the general, playing "ThePlayer"'s part. A replay (`replaySeat`) plays the recorded seat's view on this machine
// alone. `seed`: a given seed in place of a mission's 0. None: the map could not be read or the mission is not known.
inline std::optional<PreparedMatch> PrepareMatch(const session::setup::MatchPlan &plan, const engine::filesystem::VirtualFileSystem &files,
	const content::CampaignCatalog &campaigns, const content::PlayerTemplates &templates, const engine::localization::StringTable &strings,
	std::optional<std::int32_t> seed, std::uint32_t *replaySeat, const std::vector<content::MultiplayerColor> *colors = nullptr)
{
	using session::setup::MatchKind;
	PreparedMatch match;
	match.challenge = plan.kind == MatchKind::Challenge;
	const bool lan = plan.kind == MatchKind::Lan;
	if (plan.FromSetup())
		match.path = plan.setup.map;
	else
	{
		const content::Campaign *campaign = campaigns.Find(plan.campaign);
		match.mission = campaign != nullptr ? campaign->FindMission(plan.mission) : nullptr;
		if (match.mission == nullptr)
		{
			std::fprintf(stderr, "campaign: '%s' has no mission '%s'\n", plan.campaign.c_str(), plan.mission.c_str());
			return std::nullopt;
		}
		match.path = match.mission->map;
		match.solo = plan.difficulty;
	}
	for (char &c : match.path)
		c = c == '\\' ? '/' : c;
	const auto bytes = files.Read(match.path);
	auto read = bytes ? std::optional(engine::level::generals_map::Read(*bytes)) : std::nullopt;
	if (!read || !*read)
	{
		std::fprintf(stderr, "game: map '%s' could not be read\n", match.path.c_str());
		return std::nullopt;
	}
	int spots = 0;
	for (const auto &marker : (*read)->level.markers)
		spots += marker.name.starts_with("Player_") && marker.name.ends_with("_Start") ? 1 : 0;
	if (plan.FromSetup())
	{
		match.seed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.setup.seed));
		// The computer players' standard scripts and teams (SidesList::prepareForMP_or_Skirmish).
		std::optional<engine::level::generals_map::ScriptFile> skirmishScripts;
		if (const auto scb = files.Read("Data/Scripts/SkirmishScripts.scb"))
			if (auto parsed = engine::level::generals_map::ReadScriptFile(*scb))
				skirmishScripts = std::move(*parsed);
		auto skirmish = session::setup::PrepareSkirmishLevel((*read)->level, plan.setup, templates, spots, skirmishScripts ? &*skirmishScripts : nullptr, colors);
		match.players = skirmish.players.size();
		// The humans' seats: a skirmish's one human in seat 0; a LAN game's humans in slot order (the relay's seats), this
		// machine's the one in its slot.
		std::string localPlayer;
		std::vector<std::pair<int, std::string>> humans;
		for (const auto &player : skirmish.players)
		{
			if (player.human)
			{
				humans.emplace_back(player.slot, player.name);
				if (!lan || player.slot == plan.localSlot)
				{
					localPlayer = player.name;
					match.cameraMarker = "Player_" + std::to_string(player.startPosition + 1) + "_Start";
				}
			}
			match.starts.push_back({player.name, player.team, player.playerTemplate, player.startPosition, player.color});
		}
		std::ranges::sort(humans);
		for (const auto &human : humans)
			match.seats.push_back(human.second);
		// A replay plays the recorded seat's view on this machine alone (no relay: its ticks are the file's).
		const auto localSeat = static_cast<std::uint32_t>(std::ranges::find(match.seats, localPlayer) - match.seats.begin());
		if (replaySeat != nullptr)
			*replaySeat = localSeat;
		if (lan && replaySeat == nullptr)
		{
			match.network.emplace();
			match.network->seat = localSeat;
			match.network->players = static_cast<std::uint32_t>(match.seats.size());
			if (!plan.hosting)
				match.network->hostAddress = plan.hostAddress;
		}
		// The local human's side scripts (the civilian skirmish side's: which music plays, on the local player's side and
		// losses) answer for "<Local Player>": they run on this machine only, beside MultiplayerScripts.scb's
		// (Player::initFromDict gives them to the human; each machine hears its own player's). Every other human's run on
		// its own machine.
		for (auto &side : skirmish.level.scenario.participants)
		{
			const std::string name = side.properties.Get<std::string>("playerName").value_or("");
			if (std::ranges::find(match.seats, name) == match.seats.end() || (side.scripts.scripts.empty() && side.scripts.groups.empty()))
				continue;
			if (name == localPlayer)
				match.localLists.push_back(std::move(side.scripts));
			side.scripts = {};
		}
		// The names its messages give the players (GameSlot::getName: a human's own, an AI's by its level).
		auto &names = match.playerNames.emplace();
		for (const auto &player : skirmish.players)
		{
			const auto &slot = plan.setup.slots[static_cast<std::size_t>(player.slot)];
			using State = session::setup::SlotState;
			names.emplace_back(player.name,
				slot.Human() ? slot.name : Localized(strings, slot.state == State::EasyAI ? "GUI:EasyAI" : slot.state == State::MediumAI ? "GUI:MediumAI" : "GUI:HardAI"));
		}
		match.defeatedText = Localized(strings, "GUI:PlayerHasBeenDefeated");
		// GameLogic::startNewGame: a game of more than one team adds MultiplayerScripts.scb's victory and defeat scripts
		// (its first list), here the local player's own (see UseMatchScripts).
		int teams = 0, lastTeam = -1;
		for (const auto &slot : plan.setup.slots)
			if (slot.Occupied() && !slot.Observer() && (slot.team == -1 || slot.team != lastTeam))
			{
				++teams;
				lastTeam = slot.team;
			}
		if (teams > 1)
			if (const auto scriptBytes = files.Read("Data/Scripts/MultiplayerScripts.scb"))
			{
				if (auto scripts = engine::level::generals_map::ReadScriptFile(*scriptBytes); scripts && !scripts->lists.empty())
					match.localLists.insert(match.localLists.begin(), std::move(scripts->lists.front()));
				else
					std::fprintf(stderr, "game: MultiplayerScripts.scb could not be read\n");
			}
		match.level = std::move(skirmish.level);
	}
	else if (match.challenge)
	{
		// The general's PlayerTemplate in the one human slot, beside the map's own players.
		int templateIndex = -1;
		for (std::size_t index = 0; index < templates.templates.size(); ++index)
			if (templates.templates[index].name == plan.playerTemplate)
				templateIndex = static_cast<int>(index);
		auto prepared = session::setup::PrepareChallengeLevel((*read)->level, templateIndex, templates, spots, 0, session::setup::GameSetup{}.startingCash);
		const auto &human = prepared.players.front();
		match.seats.push_back(human.name);
		match.starts.push_back({human.name, human.team, human.playerTemplate, human.startPosition});
		match.cameraMarker = "Player_" + std::to_string(human.startPosition + 1) + "_Start";
		match.level = std::move(prepared.level);
	}
	else
	{
		for (const auto &side : (*read)->level.scenario.participants)
			if (side.properties.Get<bool>("playerIsHuman").value_or(false))
			{
				match.seats.push_back(side.properties.Get<std::string>("playerName").value_or(""));
				break;
			}
		match.level = std::move((*read)->level);
	}
	// MainMenu doGameStart / ScoreScreen startNextCampaignGame / ChallengeMenu: InitRandom(0).
	if (!plan.FromSetup() && seed)
		match.seed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(*seed));
	return match;
}
}
