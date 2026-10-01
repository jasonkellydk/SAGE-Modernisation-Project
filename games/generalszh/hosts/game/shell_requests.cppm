export module games.generalszh.hosts.game.shell_requests;
import std;

export import games.generalszh.hosts.game.match_flow;
export import games.generalszh.hosts.game.movie_player;
export import games.generalszh.content.global.videos;
import games.generalszh.hosts.game.match_files;
import games.generalszh.shell.save_load.save_game_file;
import engine.core.serialization.byte_stream;
import Graphics.Renderer2D;

// What the shell asks of the host's matches each frame: the score screen's choice (Continue plays the next mission:
// startNextCampaignGame), a campaign chosen (CampaignManager::setCampaign: its first mission), a skirmish or LAN game
// started (MSG_NEW_GAME), a replay chosen (RecorderClass::playbackFile) and a saved game chosen (GameState::loadGame);
// then the match asked for starts, a new mission's intro movie first (SinglePlayerLoadScreen::init).
export namespace generalszh::host
{
inline void TakeShellRequests(HostParts &host, MatchStage &stage, MatchFlow &flow)
{
	FrontEnd &frontEnd = host.frontEnd;
	if (const auto choice = frontEnd.TakeScoreChoice(); choice != shell::ScoreChoice::None)
	{
		if (choice == shell::ScoreChoice::Continue && flow.afterScores)
			flow.pending = *flow.afterScores;
		flow.afterScores.reset();
	}
	if (auto chosen = frontEnd.TakeCampaignStart())
	{
		const content::Campaign *campaign = host.campaigns.Find(chosen->campaign);
		const content::Mission *first = campaign != nullptr ? campaign->NextMission(nullptr) : nullptr;
		if (first == nullptr)
			std::println(std::cerr, "campaign: '{}' has no missions", chosen->campaign);
		else
		{
			MatchPlan plan;
			plan.kind = campaign->challenge && !chosen->playerTemplate.empty() ? MatchKind::Challenge : MatchKind::Campaign;
			plan.campaign = campaign->name;
			plan.mission = first->name;
			plan.difficulty = chosen->difficulty;
			plan.playerTemplate = chosen->playerTemplate;
			flow.pending = std::move(plan);
		}
	}
	if (auto start = frontEnd.TakeGameStart())
	{
		if (host.options.seed)
			start->seed = *host.options.seed;
		MatchPlan plan;
		plan.setup = std::move(*start);
		flow.pending = std::move(plan);
	}
	// LANAPI::OnGameStart: MSG_NEW_GAME with GAME_LAN, InitRandom(its seed).
	if (auto start = frontEnd.TakeLanStart())
	{
		MatchPlan plan;
		plan.kind = MatchKind::Lan;
		plan.setup = std::move(start->setup);
		plan.localSlot = start->localSlot;
		plan.hostAddress = start->hostAddress;
		plan.hosting = start->hosting;
		flow.pending = std::move(plan);
	}
	// A replay: its game started from its plan, played from its recorded ticks. Not one of this port's (the original's
	// GameMessage stream): it cannot play.
	if (auto file = frontEnd.TakeReplayRequest())
	{
		auto body = ReadReplayFile(*file);
		if (!body)
			std::println(std::cerr, "replay: '{}' cannot be played back", file->string());
		else
		{
			flow.replaying = ReplayStart{std::move(body->recording), 0};
			flow.replayMismatchShown = false;
			if (!LaunchMatch(host, stage, flow, body->plan, {}, &*flow.replaying))
			{
				flow.replaying.reset();
				std::println(std::cerr, "replay: '{}' could not start", file->string());
			}
		}
	}
	// A saved game: its level made again from its plan, the game going on from its checkpoint.
	std::optional<std::filesystem::path> requested = frontEnd.TakeLoadRequest();
	if (!requested && host.options.loadFile && !frontEnd.InGame())
		requested = std::exchange(host.options.loadFile, std::nullopt);
	if (auto file = requested)
	{
		const auto saved = shell::ReadSaveGame(ReadFileBytes(*file));
		engine::core::serialization::ByteReader reader(saved ? std::span<const std::byte>(*saved) : std::span<const std::byte>{});
		const auto match = ReadSavedMatch(reader);
		// A mission save (no game in it: loadGame's SAVE_FILE_TYPE_MISSION) starts its mission anew (MSG_NEW_GAME with the
		// campaign's difficulty and rank points, InitRandom(0)).
		if (match && match->checkpoint.empty() && match->plan.SinglePlayer())
		{
			if (!LaunchMatch(host, stage, flow, match->plan, {}))
				std::println(std::cerr, "load: '{}' has a mission that could not start", file->string());
		}
		else if (!match || !LaunchMatch(host, stage, flow, match->plan, match->checkpoint))
			std::println(std::cerr, "load: '{}' is no saved game of this version", file->string());
		else if (!host.game.LoadClientState(reader))
			std::println(std::cerr, "load: '{}' has no view of the game", file->string());
		else
			std::println("load: {} with {} entities", file->string(), host.game.EntityCount());
	}
}

// The match asked for starts: a new campaign or challenge mission with an intro movie that plays waits for it
// (briefing), anything else at once.
inline void StartPendingMatch(HostParts &host, MatchStage &stage, MatchFlow &flow, MoviePlayer &movie, const content::VideoCatalog &videos)
{
	if (!flow.pending)
		return;
	MatchPlan plan = std::move(*flow.pending);
	flow.pending.reset();
	const content::Campaign *campaign = plan.SinglePlayer() ? host.campaigns.Find(plan.campaign) : nullptr;
	const content::Mission *mission = campaign != nullptr ? campaign->FindMission(plan.mission) : nullptr;
	if (mission != nullptr && movie.Play(videos, host.options.install, "English", mission->introMovie))
	{
		ShowBriefingLoadScreen(host, plan, static_cast<int>(movie.FrameCount()));
		host.loadScreen.SetVideoSource([&movie](Graphics::Renderer2D &renderer) { return movie.FrameImage(renderer); });
		flow.briefing = std::move(plan);
	}
	else
		LaunchMatch(host, stage, flow, plan, {});
}
}
