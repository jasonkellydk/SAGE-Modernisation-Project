export module games.generalszh.hosts.game.match_flow;
import std;

export import games.generalszh.session.setup.match_plan;
export import games.generalszh.hosts.game.match_stage;
export import games.generalszh.hosts.game.game_client;
export import games.generalszh.hosts.game.front_end;
export import games.generalszh.hosts.game.control_bar_layer;
export import games.generalszh.hosts.game.host_options;
export import games.generalszh.hosts.game.load_screen_layer;
export import games.generalszh.content.global.campaigns;
export import games.generalszh.content.global.language_fonts;
export import engine.localization.model.string_table;
import games.generalszh.hosts.game.match_setup;
import engine.level.adapters.generals_map.map_reader;
import games.generalszh.hosts.game.match_files;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.shell.save_load.save_game_file;
import Engine.Core.Math.FixedPresentation;

// The host's matches (GameLogic::startNewGame, GameState's saves, the quit menu): which match plays and was asked
// for (MatchFlow, plain data), the host's parts a match is made with (HostParts, references), and what starting,
// saving and the menus over a game do with them.
export namespace generalszh::host
{
using session::setup::MatchKind;
using session::setup::MatchPlan;

struct MatchFlow
{
	// What the match playing was started from (a skirmish's setup, a campaign's or challenge's mission: its restart,
	// its saves, the next mission after a victory), the match to start next, a new mission waiting for its intro movie
	// (SinglePlayerLoadScreen::init plays it first) and the score screen's Continue (the next mission, or the same).
	std::optional<MatchPlan> playing;
	std::optional<MatchPlan> pending;
	std::optional<MatchPlan> briefing;
	std::optional<MatchPlan> afterScores;
	std::string playingMap;      // the map of the match playing
	bool paused{false};          // GameLogic::setGamePaused (the quit menu over a single-player game or a skirmish)
	bool exitRequested{false};   // the quit menu's Exit (GameLogic::quit): back to the shell
	// A replay playing back (RecorderClass::playbackFile), and whether its CRC mismatch has been shown.
	std::optional<ReplayStart> replaying;
	bool replayMismatchShown{false};
	std::uint32_t matchStartTime{0}; // time() as the match started (a replay header's start time)
};

// What a match is made with: the host's options, the install's files and content, its strings, campaigns and fonts,
// the game client, the front end and the control bar (loaded with the first match).
struct HostParts
{
	Options &options;
	const engine::filesystem::VirtualFileSystem &files;
	content::ContentLoader &loader;
	const engine::localization::StringTable &strings;
	const content::CampaignCatalog &campaigns;
	const content::LanguageFonts &fonts;
	GameClient &game;
	FrontEnd &frontEnd;
	ControlBarLayer &controlBar;
	bool &controlBarLoaded;
	LoadScreenLayer &loadScreen;
	const std::vector<shell::LoadScreenFaction> &factions; // PlayerTemplate.ini's, as the multiplayer load screen shows them
};

// MultiPlayerLoadScreen::init's game: the setup as chosen, what the match resolved (each seat's template and start spot
// from its player "player<slot>"), the players' names, the factions, Multiplayer.ini's colours and the map's entry.
inline shell::MultiplayerLoadScreenSetup MultiplayerLoadSetup(const HostParts &host, const MatchPlan &plan, const PreparedMatch &match)
{
	shell::MultiplayerLoadScreenSetup game;
	game.chosen = plan.setup;
	game.localSlot = plan.kind == MatchKind::Lan ? plan.localSlot : 0;
	for (int slot = 0; slot < session::setup::MaxSlots; ++slot)
	{
		const auto at = static_cast<std::size_t>(slot);
		const auto &chosen = plan.setup.slots[at];
		game.playerTemplate[at] = chosen.playerTemplate;
		game.color[at] = chosen.color;
		game.startPos[at] = chosen.startPos;
		const std::string player = "player" + std::to_string(slot);
		for (const PlayerStart &start : match.starts)
			if (start.player == player)
			{
				game.playerTemplate[at] = start.playerTemplate;
				game.startPos[at] = start.startPosition;
				game.color[at] = start.color;
			}
		if (match.playerNames)
			for (const auto &[name, shown] : *match.playerNames)
				if (name == player)
					game.names[at] = shown;
	}
	game.factions = host.factions;
	for (std::size_t index = 0; index < host.factions.size(); ++index)
		if (host.factions[index].name == "FactionObserver")
			game.observerTemplate = static_cast<int>(index);
	const shell::SetupCatalog &catalog = host.frontEnd.SetupCatalog();
	for (const shell::SetupColor &color : catalog.colors)
		game.colors.push_back((color.rgba >> 8) | (color.rgba << 24));
	game.map = catalog.Find(plan.setup.map);
	const content::MultiplayerSettings &settings = host.game.MultiplayerSettings();
	game.showRandomTemplate = settings.showRandomPlayerTemplate;
	game.showRandomColor = settings.showRandomColor;
	game.showRandomStart = settings.showRandomStartPos;
	const engine::localization::StringTable &strings = host.strings;
	game.text = [&strings](std::string_view label) { return Localized(strings, label); };
	return game;
}

// GameLogic::getLoadScreen and LoadScreen::init for the match `plan` (`saved`: a saved game goes on; `replay`: played
// back): a mission's after its movie (SinglePlayerLoadScreen::init plays it first: its side's look stays).
inline void ShowMatchLoadScreen(HostParts &host, const MatchPlan &plan, const PreparedMatch &match, bool saved, bool replay)
{
	using shell::LoadGameMode;
	const LoadGameMode mode = replay ? LoadGameMode::Replay
		: plan.kind == MatchKind::Skirmish ? LoadGameMode::Skirmish
		: plan.kind == MatchKind::Lan      ? LoadGameMode::Lan
										   : LoadGameMode::SinglePlayer;
	const shell::LoadScreenKind kind = shell::LoadScreenFor(mode, plan.kind == MatchKind::Challenge, saved);
	const Options &options = host.options;
	const float fontScale = FontScale(options.width, options.height, Engine::Math::ToFloat(host.fonts.resolutionAdjustment));
	host.loadScreen.Show(kind, host.files, host.strings, options.width, options.height, fontScale, [&](LoadScreenLayer &screen) {
		if (kind == shell::LoadScreenKind::ShellGame)
		{
			screen.shellGame.Init(!screen.shellLoadedOnce);
			screen.shellLoadedOnce = true;
		}
		else if (kind == shell::LoadScreenKind::MultiPlayer)
		{
			shell::MultiplayerLoadScreenSetup game = MultiplayerLoadSetup(host, plan, match);
			game.imageKnown = [&screen](std::string_view name) { return screen.ImageKnown(name); };
			screen.multiplayer.Init(game);
		}
		else
			screen.mission.Init(kind == shell::LoadScreenKind::Challenge);
	});
	host.loadScreen.Update(shell::load_progress::Start);
}

// A new mission's load screen with its movie (SinglePlayerLoadScreen / ChallengeLoadScreen::init): the screen up, the
// movie (`frames` long) in its background window; the load goes on behind the same screen once the movie is over.
inline void ShowBriefingLoadScreen(HostParts &host, const MatchPlan &plan, int frames)
{
	const shell::LoadScreenKind kind = shell::LoadScreenFor(shell::LoadGameMode::SinglePlayer, plan.kind == MatchKind::Challenge, false);
	const Options &options = host.options;
	const float fontScale = FontScale(options.width, options.height, Engine::Math::ToFloat(host.fonts.resolutionAdjustment));
	host.loadScreen.Show(kind, host.files, host.strings, options.width, options.height, fontScale, [&](LoadScreenLayer &screen) {
		screen.mission.Init(kind == shell::LoadScreenKind::Challenge);
		screen.mission.StartMovie(plan.campaign, frames);
	});
}

// GameLogic::startNewGame: the match `requested` in place of the one playing; from `checkpoint` when a saved game goes
// on, played back when `replay`. Its scenery as it first started (a replay's, a saved game's), else from this
// machine's detail now. A skirmish: its map made ready for its setup, its seats' players starting there, the local
// player's side scripts and MultiplayerScripts.scb's run locally. A campaign's mission (GAME_SINGLE_PLAYER): its map
// with its own players, the local human playing its human side at the campaign's difficulty; a Generals' Challenge's:
// its map with the human added as the general, playing "ThePlayer"'s part. False: not started.
inline bool LaunchMatch(HostParts &host, MatchStage &stage, MatchFlow &flow, const MatchPlan &requested, std::span<const std::byte> checkpoint,
	ReplayStart *replay = nullptr)
{
	GameClient &game = host.game;
	MatchPlan plan = requested;
	if (!plan.sceneryKnown)
	{
		const auto *detail = game.Detail();
		const auto scenery = session::ScenerySetupFor(detail != nullptr ? detail->level : 2, detail == nullptr || detail->useTrees, plan.kind == MatchKind::Lan);
		plan.sceneryKnown = true;
		plan.useTrees = scenery.useTrees;
		plan.forceFluffToProp = scenery.forceFluffToProp;
	}
	auto prepared = PrepareMatch(plan, host.files, host.campaigns, game.PlayerTemplates(), host.strings, host.options.seed,
		replay != nullptr ? &replay->seat : nullptr, &game.MultiplayerSettings().colors);
	if (!prepared)
	{
		host.loadScreen.Close();
		return false;
	}
	PreparedMatch &match = *prepared;
	// A mission whose movie played loads on behind the screen its movie played in.
	const bool briefed = host.loadScreen.Shown() && checkpoint.empty() && plan.SinglePlayer() &&
		host.loadScreen.Kind() == shell::LoadScreenFor(shell::LoadGameMode::SinglePlayer, plan.kind == MatchKind::Challenge, false);
	if (!briefed)
		ShowMatchLoadScreen(host, plan, match, !checkpoint.empty(), replay != nullptr);
	else
		host.loadScreen.Update(shell::load_progress::Start);
	host.loadScreen.Update(shell::load_progress::PostLoadMap);
	if (match.playerNames)
		game.SetPlayerNames(std::move(*match.playerNames), match.defeatedText);
	const std::string path = match.path;
	const bool lan = plan.kind == MatchKind::Lan;
	if (!RestageLevel(stage, std::move(match.level), host.files, host.loader, path))
		std::println(std::cerr, "terrain: {}", stage.terrainError);
	host.loadScreen.Update(shell::load_progress::PostPathfinderNewMap);
	const Options &options = host.options;
	if (!game.Start(*stage.level, stage.Height(), stage.mesh->PlayableSize(), static_cast<float>(options.width) / static_cast<float>(options.height), match.seed,
			std::move(match.seats), std::move(match.starts), match.cameraMarker, match.solo, match.challenge, checkpoint, match.network, plan.rankPoints, replay,
			// RecorderClass::updateRecord: MSG_NEW_GAME of a skirmish or LAN game starts recording (not a loaded save).
			replay == nullptr && checkpoint.empty() && (plan.kind == MatchKind::Skirmish || lan), session::ScenerySetup{plan.useTrees, plan.forceFluffToProp}))
	{
		std::println(std::cerr, "game: the saved game does not fit its map");
		host.loadScreen.Close();
		return false;
	}
	host.loadScreen.Update(shell::load_progress::PostInitialNetworkBuildings);
	if (!match.localLists.empty())
		game.UseMatchScripts(std::move(match.localLists));
	game.Update(0.0f, 1.0f);
	stage.scene->Preload(game);
	host.loadScreen.Update(shell::load_progress::PostPreloadAssets);
	host.frontEnd.EnterGame();
	if (!host.controlBarLoaded)
	{
		std::string barError;
		host.controlBarLoaded = host.controlBar.Load(host.files, host.strings, host.loader, game, options.width, options.height,
			FontScale(options.width, options.height, Engine::Math::ToFloat(host.fonts.resolutionAdjustment)), barError);
		if (!host.controlBarLoaded)
			std::println(std::cerr, "control bar: {}", barError);
	}
	// Radar::newMap: the level's radar picture.
	if (host.controlBarLoaded && stage.scene)
		host.controlBar.SetRadarTerrain(stage.scene->BuildRadarTerrain(*stage.level, game));
	host.loadScreen.Update(shell::load_progress::End);
	host.loadScreen.Update(shell::load_progress::Done);
	host.loadScreen.Close();
	flow.playing = plan;
	flow.playingMap = path;
	flow.matchStartTime = static_cast<std::uint32_t>(std::time(nullptr));
	if (const auto *mission = match.mission)
		std::printf("campaign %s: %s (%s), %zu entities\n", plan.campaign.c_str(), mission->name.c_str(), path.c_str(), game.EntityCount());
	else
		std::printf("game: %s with %zu players, %zu entities\n", path.c_str(), match.players, game.EntityCount());
	return true;
}

// Shell::showShellMap: the shell map (GAME_SHELL, InitRandom(0)) loaded behind the shell's load screen; the game's first
// load (after the intro) shows the title screen. False: its map could not be read.
inline bool LoadShellMap(HostParts &host, MatchStage &stage)
{
	const Options &options = host.options;
	const float fontScale = FontScale(options.width, options.height, Engine::Math::ToFloat(host.fonts.resolutionAdjustment));
	host.loadScreen.Show(shell::LoadScreenKind::ShellGame, host.files, host.strings, options.width, options.height, fontScale, [](LoadScreenLayer &screen) {
		screen.shellGame.Init(!screen.shellLoadedOnce);
		screen.shellLoadedOnce = true;
	});
	host.loadScreen.Update(shell::load_progress::Start);
	bool loaded = false;
	if (const auto shellBytes = host.files.Read(options.map))
		if (auto shellMap = engine::level::generals_map::Read(*shellBytes))
		{
			host.loadScreen.Update(shell::load_progress::PostLoadMap);
			if (!RestageLevel(stage, std::move(shellMap->level), host.files, host.loader, options.map))
				std::println(std::cerr, "terrain: {}", stage.terrainError);
			host.loadScreen.Update(shell::load_progress::PostPathfinderNewMap);
			host.game.Start(*stage.level, stage.Height(), stage.mesh->PlayableSize(), static_cast<float>(options.width) / static_cast<float>(options.height), 0x5EED);
			host.loadScreen.Update(shell::load_progress::PostInitialNetworkBuildings);
			host.game.Update(0.0f, 1.0f);
			stage.scene->Preload(host.game);
			host.loadScreen.Update(shell::load_progress::PostPreloadAssets);
			loaded = true;
		}
	host.loadScreen.Update(shell::load_progress::End);
	host.loadScreen.Update(shell::load_progress::Done);
	host.loadScreen.Close();
	return loaded;
}

// GameState::saveGame: the match playing, its info (the date now, the description) first, then its plan, checkpoint
// and client state. The file; none when there is no match to save.
inline std::optional<std::filesystem::path> SaveMatch(HostParts &host, const MatchFlow &flow, std::optional<std::string> overwrite, std::u16string description)
{
	if (!flow.playing)
		return std::nullopt;
	const std::vector<std::byte> checkpoint = host.game.Checkpoint();
	if (checkpoint.empty())
		return std::nullopt;
	shell::SaveGameInfo info;
	info.missionMap = flow.playingMap;
	for (char &c : info.missionMap)
		c = c == '/' ? '\\' : c;
	info.date = SaveDateNow();
	info.description = std::move(description);
	return WriteSaveFile(host.frontEnd.SaveFolder(), info, SavedMatchBody(*flow.playing, checkpoint, [&](auto &writer) { host.game.SaveClientState(writer); }),
		overwrite);
}

// GameState::missionSave (the score screen after a mission won with one to follow): a SAVE_FILE_TYPE_MISSION save
// holding only the campaign's state (the next mission's plan: its campaign, mission, difficulty and rank points), no
// game (an empty checkpoint); described by "GUI:MissionSave" with the campaign's name and the mission's number.
inline std::optional<std::filesystem::path> SaveMission(HostParts &host, const MatchPlan &next)
{
	const content::Campaign *campaign = host.campaigns.Find(next.campaign);
	const content::Mission *mission = campaign != nullptr ? campaign->FindMission(next.mission) : nullptr;
	if (mission == nullptr)
		return std::nullopt;
	shell::SaveGameInfo info;
	info.type = shell::SaveFileType::Mission;
	info.missionMap = mission->map;
	info.date = SaveDateNow();
	info.description = shell::MissionSaveDescription(Localized(host.strings, "GUI:MissionSave"), Localized(host.strings, campaign->nameLabel.c_str()),
		campaign->MissionNumber(mission) + 1);
	return WriteSaveFile(host.frontEnd.SaveFolder(), info, SavedMatchBody(next, {}), std::nullopt);
}

// The menus over a game (QuitMenu, PopupSaveLoad): what they do to the game and the match flow.
inline void BindGameMenus(HostParts &host, MatchFlow &flow)
{
	GameClient &game = host.game;
	FrontEnd::GameMenus menus;
	menus.quit.exit = [&flow] { flow.exitRequested = true; };
	// restartMissionMenu: the same match again from its start (a skirmish on its seed, a mission on 0).
	menus.quit.restart = [&flow] {
		if (flow.playing)
			flow.pending = *flow.playing;
	};
	menus.quit.pause = [&flow](bool on) { flow.paused = on; };
	// surrenderQuitMenu: MSG_SELF_DESTRUCT, its assets to a living ally.
	menus.quit.surrender = [&game] { game.Submit(commands::SelfDestruct{true}); };
	menus.mode = [&flow] {
		if (flow.playing && flow.playing->kind == MatchKind::Lan)
			return shell::QuitMenuMode::Multiplayer;
		return flow.playing && flow.playing->kind == MatchKind::Skirmish ? shell::QuitMenuMode::Skirmish : shell::QuitMenuMode::SinglePlayer;
	};
	menus.inputEnabled = [&game] { return !game.Settings().inputDisabled; };
	menus.beaten = [] { return false; };
	menus.save = [&host, &flow](const std::optional<std::string> &file, const std::u16string &description) {
		if (const auto saved = SaveMatch(host, flow, file, description))
			std::printf("save: %s\n", saved->string().c_str());
		else
			std::println(std::cerr, "save: the game could not be saved");
	};
	// setEditDescription: the campaign's name and mission number, else the map's file name without its extension.
	menus.defaultDescription = [&host, &flow]() -> std::u16string {
		if (flow.playing && flow.playing->SinglePlayer())
			if (const auto *campaign = host.campaigns.Find(flow.playing->campaign))
			{
				const std::string number = std::to_string(campaign->MissionNumber(campaign->FindMission(flow.playing->mission)) + 1);
				return Localized(host.strings, campaign->nameLabel.c_str()) + u' ' + std::u16string(number.begin(), number.end());
			}
		std::string leaf = flow.playingMap.substr(flow.playingMap.find_last_of("/\\") + 1);
		if (leaf.size() >= 4 && leaf[leaf.size() - 4] == '.')
			leaf.resize(leaf.size() - 4);
		return std::u16string(leaf.begin(), leaf.end());
	};
	host.frontEnd.SetGameMenus(std::move(menus));
	FrontEnd &frontEnd = host.frontEnd;
	host.controlBar.SetOptionsAction([&frontEnd] { frontEnd.ToggleQuitMenu(); });
}
}
