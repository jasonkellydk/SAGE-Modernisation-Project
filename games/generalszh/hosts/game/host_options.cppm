module;
#include <cstdio>

export module games.generalszh.hosts.game.host_options;
import std;

// The host's command line (generalszh [--install <dir>] [--map <path>] [--frames <n>] ...): what to run and the
// scripted input and captures the checks use. The install directory defaults to GENERALSZH_INSTALL_DIR.
export namespace generalszh::host
{
struct Options
{
	std::filesystem::path install;
	std::string map; // empty: GameData's ShellMapName
	std::uint64_t frames{0}; // 0 = run until closed
	std::uint64_t killEnemiesAt{0}; // this many frames into a match, the kill-all-enemies cheat (checks: a game's end)
	std::optional<std::int32_t> seed; // a started game's seed in place of the setup's random one (reproducible captures)
	std::uint64_t matchFrames{0}; // else: run (and capture) until this many frames into a match, however long the shell took
	std::filesystem::path screenshot;
	std::filesystem::path loadScreenShots; // --load-screen-shots: each load screen's frame, written there
	// One logic tick per rendered frame at a fixed 30 fps (reproducible captures).
	bool tickPerFrame{false};
	// Every script action the matches run, to stderr (diagnostics).
	bool scriptTrace{false};
	// Saves the game this many frames into a match (checks: GameState::saveGame without the menu).
	std::uint64_t saveAt{0};
	// Loads this saved game once the shell is up (checks: GameState::loadGame without the menu).
	std::optional<std::filesystem::path> loadFile;
	// Game speed: 1 is the original's 30 logic ticks per second.
	float speed{1.0f};
	std::uint32_t width{1024};
	std::uint32_t height{768};
	// A left-button drag to make once a match runs (x0, y0, x1, y1 in pixels).
	std::optional<std::array<float, 4>> drag;
	// Left clicks to make once a match runs (after the drag), one every 20 frames.
	std::vector<std::pair<float, float>> clicks;
	// The player's data folder (Options.ini): the original's, under Documents, unless given.
	std::filesystem::path userData;
	bool openOptions{false}; // start with the options menu open (captures, checks)
	std::string startScreen; // start on a screen (captures, checks)
	std::string movie; // a movie (Video.ini name) played over the shell at start (checks)
	bool nologo{false}; // -nologo: no intro
	bool intro{false};  // --intro: the intro even in an automated run (captures of it, checks)
	bool mapGiven{false}; // --map named (the shell map is filled in otherwise)
	bool gameDataIntro{true}; // GameData.ini's PlayIntro
	std::vector<std::string> presses; // buttons clicked in turn (captures, checks)
	std::vector<std::string> barPresses; // control bar buttons clicked in a match, from frame 90 one every 20 (captures, checks)
	std::vector<std::string> gamePresses; // buttons of the menus over a match clicked in turn while one shows (checks)
	std::optional<std::pair<float, float>> look; // a map point the camera stays on (captures, checks)
	bool lanLoopback{false};         // the LAN lobby kept on this machine
};

// The original's user data folder (GlobalData: My Documents\Command and Conquer Generals Zero Hour Data).
std::filesystem::path DefaultUserData()
{
	const char *profile = std::getenv("USERPROFILE");
	return profile == nullptr ? std::filesystem::path{} : std::filesystem::path(profile) / "Documents" / "Command and Conquer Generals Zero Hour Data";
}


std::optional<Options> ParseOptions(int argc, char **argv)
{
	Options options;
	if (const char *install = std::getenv("GENERALSZH_INSTALL_DIR"))
		options.install = install;
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		const bool hasValue = index + 1 < argc;
		if (argument == "--install" && hasValue)
			options.install = argv[++index];
		else if (argument == "--map" && hasValue)
		{
			options.map = argv[++index];
			options.mapGiven = true;
		}
		else if (argument == "--frames" && hasValue)
			options.frames = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--kill-enemies-at" && hasValue)
			options.killEnemiesAt = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--seed" && hasValue)
			options.seed = static_cast<std::int32_t>(std::strtol(argv[++index], nullptr, 10));
		else if (argument == "--match-frames" && hasValue)
			options.matchFrames = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--screenshot" && hasValue)
			options.screenshot = argv[++index];
		else if (argument == "--load-screen-shots" && hasValue)
			options.loadScreenShots = argv[++index];
		else if (argument == "--tick-per-frame")
			options.tickPerFrame = true;
		else if (argument == "--script-trace")
			options.scriptTrace = true;
		else if (argument == "--save-at" && hasValue)
			options.saveAt = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--load" && hasValue)
			options.loadFile = std::filesystem::path(argv[++index]);
		else if (argument == "--open-options")
			options.openOptions = true;
		else if (argument == "--nologo" || argument == "-nologo")
			options.nologo = true;
		else if (argument == "--intro")
			options.intro = true;
		else if (argument == "--movie" && hasValue)
			options.movie = argv[++index];
		else if (argument == "--screen" && hasValue)
			options.startScreen = argv[++index];
		else if (argument == "--press" && hasValue)
			options.presses.push_back(argv[++index]);
		else if (argument == "--bar-press" && hasValue)
			options.barPresses.push_back(argv[++index]);
		else if (argument == "--game-press" && hasValue)
			options.gamePresses.push_back(argv[++index]);
		else if (argument == "--look" && hasValue)
		{
			// x,y: the camera stays on that map point.
			const std::string point = argv[++index];
			const auto comma = point.find(',');
			if (comma != std::string::npos)
				options.look = std::pair{std::strtof(point.substr(0, comma).c_str(), nullptr), std::strtof(point.substr(comma + 1).c_str(), nullptr)};
		}
		else if (argument == "--drag" && hasValue)
		{
			// x0,y0,x1,y1: once in a match, the left button drags across that box (checks and captures).
			std::array<float, 4> box{};
			std::string text = argv[++index];
			for (float &value : box)
			{
				value = std::strtof(text.c_str(), nullptr);
				const auto comma = text.find(',');
				text = comma == std::string::npos ? std::string{} : text.substr(comma + 1);
			}
			options.drag = box;
		}
		else if (argument == "--click" && hasValue)
		{
			// x,y: once in a match, after any scripted drag, the left button clicks there (an order to what is selected);
			// given again, each next click comes 20 frames after the one before, the pointer moving there halfway.
			const std::string point = argv[++index];
			const auto comma = point.find(',');
			if (comma != std::string::npos)
				options.clicks.push_back(std::pair{std::strtof(point.substr(0, comma).c_str(), nullptr), std::strtof(point.substr(comma + 1).c_str(), nullptr)});
		}
		else if (argument == "--lan-loopback")
			options.lanLoopback = true;
		else if (argument == "--user-data" && hasValue)
			options.userData = argv[++index];
		else if (argument == "--speed" && hasValue)
			options.speed = std::strtof(argv[++index], nullptr);
		else
		{
			std::fprintf(stderr, "unknown argument '%s'\n", argument.c_str());
			return std::nullopt;
		}
	}
	if (options.install.empty())
	{
		std::fprintf(stderr, "no install directory: pass --install or set GENERALSZH_INSTALL_DIR\n");
		return std::nullopt;
	}
	if (!options.screenshot.empty() && options.frames == 0 && options.matchFrames == 0)
		options.frames = 30;
	return options;
}
}
