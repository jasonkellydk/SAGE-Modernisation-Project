export module games.generalszh.shell.load_screen.load_screen_kind;
import std;

// Which load screen a game loads behind (GameLogic::getLoadScreen) and the progress its load reports
// (GameLogic.cpp's LOAD_PROGRESS_* stops, passed to LoadScreen::update).
export namespace generalszh::shell
{
enum class LoadScreenKind : std::uint8_t
{
	None,          // GAME_NONE: no load screen
	ShellGame,     // ShellGameLoadScreen.wnd
	SinglePlayer,  // SinglePlayerLoadScreen.wnd
	Challenge,     // ChallengeLoadScreen.wnd
	MultiPlayer,   // MultiplayerLoadScreen.wnd
};

// GameLogic's game modes as a load screen is chosen for them.
enum class LoadGameMode : std::uint8_t
{
	None,
	Shell,        // GAME_SHELL: the shell map
	SinglePlayer, // GAME_SINGLE_PLAYER: a campaign's or challenge's mission, or a saved game of one
	Skirmish,     // GAME_SKIRMISH
	Lan,          // GAME_LAN
	Replay,       // GAME_REPLAY
};

// GameLogic::getLoadScreen: the shell map, a replay and a saved single-player game load behind the shell's screen; a
// mission of a campaign behind the single-player one, of the Generals' Challenge behind the challenge one; skirmish and
// LAN games behind the multiplayer one.
constexpr LoadScreenKind LoadScreenFor(LoadGameMode mode, bool challengeCampaign, bool loadingSaveGame) noexcept
{
	switch (mode)
	{
	case LoadGameMode::Shell:
	case LoadGameMode::Replay:
		return LoadScreenKind::ShellGame;
	case LoadGameMode::SinglePlayer:
		if (loadingSaveGame)
			return LoadScreenKind::ShellGame;
		return challengeCampaign ? LoadScreenKind::Challenge : LoadScreenKind::SinglePlayer;
	case LoadGameMode::Skirmish:
	case LoadGameMode::Lan:
		return LoadScreenKind::MultiPlayer;
	case LoadGameMode::None:
		break;
	}
	return LoadScreenKind::None;
}

// GameLogic.cpp's LOAD_PROGRESS_* (MAX_SLOTS 8): where startNewGame's load stands as it reports it.
namespace load_progress
{
inline constexpr int Start = 0;
inline constexpr int PostParticleIniLoad = Start + 1;
inline constexpr int PostLoadMap = PostParticleIniLoad + 1;
inline constexpr int SidePopulation = PostLoadMap + 1;
inline constexpr int PostSideListInit = SidePopulation + 1 + 8;
inline constexpr int PostPlayerListReset = PostSideListInit + 1;
inline constexpr int PostScriptEngineNewMap = PostPlayerListReset + 1;
inline constexpr int PostVictoryConditionSetup = PostScriptEngineNewMap + 2;
inline constexpr int PostVictoryConditionSetVictoryCondition = PostVictoryConditionSetup + 1;
inline constexpr int PostGhostObjectManagerReset = PostVictoryConditionSetVictoryCondition + 1;
inline constexpr int PostTerrainLogicNewMap = PostGhostObjectManagerReset + 1;
inline constexpr int PostBridgeLoad = PostTerrainLogicNewMap + 1;
inline constexpr int PostPathfinderNewMap = PostBridgeLoad + 1;
inline constexpr int LoopAllTheObjects = PostBridgeLoad + 1;
inline constexpr int MaxAllTheObjects = 80;
inline constexpr int LoopInitialNetworkBuildings = MaxAllTheObjects + 1;
inline constexpr int PostInitialNetworkBuildings = LoopInitialNetworkBuildings + 8 + 1;
inline constexpr int PostPreloadAssets = PostInitialNetworkBuildings + 1;
inline constexpr int PostStartingCamera = PostPreloadAssets + 1;
inline constexpr int PostStartingCamera2 = PostStartingCamera + 1;
inline constexpr int End = 100;
inline constexpr int Done = 101; // startNewGame's last update: "keep greater then 100"
}
}
