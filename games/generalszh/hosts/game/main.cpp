// Zero Hour on the modern engine: window, fixed-step loop and renderer.
// Usage: generalszh [--install <dir>] [--map <path>] [--frames <n>] [--match-frames <n>] [--seed <n>] [--screenshot <file.png>] [--tick-per-frame] [--speed <x>]
// The install directory defaults to GENERALSZH_INSTALL_DIR.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <thread>
#include <algorithm>
#include <vector>
#include <span>

import engine.platform;
import engine.platform.adapters.sdl3;
import engine.time.simulation_clock;
import engine.time.simulation_time;
import engine.level.adapters.generals_map.map_reader;
import engine.level.presentation.terrain_mesh;
import games.generalszh.session.setup.skirmish_level;
import games.generalszh.content.install.install_mount;
import Engine.Core.Math.Vector3;
import Engine.Core.Math.AffineTransform3;
import Graphics.Frame.Runtime;
import Graphics.Frame.SceneRenderers;
import Graphics.Frame.RenderServices;
import Graphics.Capture.FrameCapture;
import Graphics.Scene.AffineTransform;
import Graphics.Scene.Views.CameraState;
import Graphics.Renderer2D;
import Assets.Runtime;
import games.generalszh.hosts.game.asset_source;
import games.generalszh.hosts.game.game_client;
import games.generalszh.hosts.game.front_end;
import games.generalszh.hosts.game.floating_text_view;
import games.generalszh.hosts.game.world_animation_view;
import games.generalszh.hosts.game.control_bar_layer;
import games.generalszh.hosts.game.match_end_layer;
import games.generalszh.hosts.game.superweapon_timers_view;
import games.generalszh.content.global.in_game_ui;
import games.generalszh.content.global.mouse;
import Graphics.Cursors.Ani;
import games.generalszh.content.global.campaigns;
import games.generalszh.content.global.videos;
import games.generalszh.hosts.game.movie_player;
import games.generalszh.shell.intro.intro_sequence;
import games.generalszh.hud.match_record;
import games.generalszh.shell.score.battle_honors;
import games.generalszh.session.setup.match_plan;
import games.generalszh.shell.save_load.save_game_file;
import games.generalszh.hosts.game.message_view;
import games.generalszh.hosts.game.military_caption_view;
import games.generalszh.hosts.game.named_timers_view;
import Graphics.Scene.Screen.FullscreenOverlay;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.hosts.game.world_scene;
import games.generalszh.content.ini.zero_hour_grammar;
import games.generalszh.content.global.game_data;
import games.generalszh.content.loading.content_loader;
import engine.localization.adapters.csf.csf_reader;
import engine.localization.adapters.str.str_reader;
import Engine.Core.Math.FixedPresentation;

namespace
{
// The in-game overlay: the selection box being dragged, and the selected objects' health bars
// (Drawable::drawHealthBar: an outline and a fill from red to green by health, averaged towards red when really
// damaged and towards green when undamaged; blue to cyan while disabled; the outline at half the colour).
void DrawInGameOverlay(Graphics::Renderer2D &renderer, const generalszh::host::InGameOverlay &overlay)
{
	if (overlay.boxActive)
		renderer.Add_Outline({overlay.box[0], overlay.box[1], overlay.box[2], overlay.box[3]}, 1.0f, Graphics::Color2D{1.0f, 1.0f, 0.0f, 1.0f});
	for (const generalszh::host::SelectedMarker &marker : overlay.selected)
	{
		Graphics::Color2D fill{0.0f, 0.0f, 0.0f, 1.0f}, outline{0.0f, 0.0f, 0.0f, 1.0f};
		if (marker.disabled)
		{
			fill = {0.0f, marker.health, 1.0f, 1.0f};
			outline = {0.0f, marker.health * 128.0f / 255.0f, 128.0f / 255.0f, 1.0f};
		}
		else
		{
			float red = marker.health >= 0.5f ? 1.0f - (marker.health - 0.5f) / 0.5f : 1.0f;
			float green = marker.health >= 0.5f ? 1.0f : 1.0f - (0.5f - marker.health) / 0.5f;
			outline = {red * 0.5f, green * 0.5f, 0.0f, 1.0f};
			if (marker.reallyDamaged)
			{
				red = (1.0f + red) * 0.5f;
				green *= 0.5f;
			}
			else if (!marker.damaged)
			{
				green = (1.0f + green) * 0.5f;
				red *= 0.5f;
			}
			fill = {red, green, 0.0f, 1.0f};
		}
		const float height = 3.0f;
		renderer.Add_Outline({marker.x, marker.y, marker.x + marker.width, marker.y + height}, 1.0f, outline);
		renderer.Add_Rect({marker.x + 1.0f, marker.y + 1.0f, marker.x + 1.0f + (marker.width - 2.0f) * marker.health, marker.y + height - 1.0f}, fill);
	}
}

struct Options
{
	std::filesystem::path install;
	std::string map; // empty: GameData's ShellMapName
	std::uint64_t frames{0}; // 0 = run until closed
	std::uint64_t killEnemiesAt{0}; // this many frames into a match, the kill-all-enemies cheat (checks: a game's end)
	std::optional<std::int32_t> seed; // a started game's seed in place of the setup's random one (reproducible captures)
	std::uint64_t matchFrames{0}; // else: run (and capture) until this many frames into a match, however long the shell took
	std::filesystem::path screenshot;
	// One logic tick per rendered frame at a fixed 30 fps (reproducible captures).
	bool tickPerFrame{false};
	// Every script action the matches run, to stderr (diagnostics).
	bool scriptTrace{false};
	// Saves the game this many frames into a match (checks: GameState::saveGame without the menu).
	std::uint64_t saveAt{0};
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
		else if (argument == "--tick-per-frame")
			options.tickPerFrame = true;
		else if (argument == "--script-trace")
			options.scriptTrace = true;
		else if (argument == "--save-at" && hasValue)
			options.saveAt = std::strtoull(argv[++index], nullptr, 10);
		else if (argument == "--open-options")
			options.openOptions = true;
		else if (argument == "--nologo" || argument == "-nologo")
			options.nologo = true;
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

// What the renderer's frame callbacks need. The graphics frame API takes
// plain function pointers without user data, so the host keeps this one
// pointer for the duration of the run (a limitation of that API).
struct FrameCapture
{
	std::filesystem::path shaders;
	bool captureNextFrame{false};
	std::filesystem::path captureFile;
	bool captured{false};
	// The screen fade over this frame's view (W3DStatusCircle's full-screen blend): 0 none, 1 add, 2 subtract,
	// 3 saturate, 4 multiply; its value.
	std::uint8_t fade{0};
	float fadeValue{0.0f};
};
FrameCapture *g_frameCapture = nullptr;

bool InitializeRenderers(Graphics::Device &device)
{
	return Graphics::Initialize_Scene_Renderers(device, g_frameCapture->shaders) && Graphics::Get_Render_Services().Initialize();
}

bool ExecuteFrameDraws(Graphics::Device &device, Graphics::CommandList &, const Graphics::FrameTargets &targets) noexcept
{
	FrameCapture &scene = *g_frameCapture;
	// The screen fade over the view, under the interface (W3DStatusCircle): the value's grey added, taken away, twice
	// colour-multiplied (saturate) or multiplied.
	if (scene.fade != 0)
	{
		const float intensity = std::clamp(scene.fadeValue, 0.0f, 1.0f);
		Graphics::FullscreenOverlayDescription overlay;
		overlay.color = {intensity, intensity, intensity, 1.0f};
		switch (scene.fade)
		{
		default:
		case 1: overlay.blend_mode = Graphics::RHIBlendMode::Additive; break;
		case 2:
			overlay.blend_mode = Graphics::RHIBlendMode::Additive;
			overlay.blend_operation = Graphics::RHIBlendOperation::ReverseSubtract;
			break;
		case 3:
			overlay.blend_mode = Graphics::RHIBlendMode::ColorMultiply;
			overlay.draw_count = 2;
			break;
		case 4: overlay.blend_mode = Graphics::RHIBlendMode::Multiply; break;
		}
		Graphics::FullscreenOverlayRenderer &fades = Graphics::GetFullscreenOverlayRenderer();
		if (fades.Set_Overlay(overlay))
			fades.Render(device.Immediate_Command_List(), targets.backbuffer.texture, {0, 0, targets.backbuffer.width, targets.backbuffer.height, 0, 1});
	}
	// 2D overlay (shell menus) recorded this frame.
	if (!Graphics::Get_Renderer2D().Execute(device, device.Immediate_Command_List(), targets.backbuffer.texture, targets.depth.texture,
			{0, 0, targets.backbuffer.width, targets.backbuffer.height, 0, 1}))
		return false;
	if (scene.captureNextFrame)
	{
		scene.captureNextFrame = false;
		Graphics::FrameCapture capture;
		// The swap chain uses the device's RGBA8 backbuffer format.
		const auto frame = capture.Read(device, targets.backbuffer.texture, targets.backbuffer.width,
			targets.backbuffer.height, Graphics::RHITextureFormat::RGBA8_UNorm);
		if (frame.Is_Valid())
		{
			std::vector<std::uint8_t> rgba(frame.pixels.size());
			std::memcpy(rgba.data(), frame.pixels.data(), rgba.size());
			for (std::size_t pixel = 3; pixel < rgba.size(); pixel += 4)
				rgba[pixel] = 255;
			scene.captured = stbi_write_png(scene.captureFile.string().c_str(), static_cast<int>(frame.width),
				static_cast<int>(frame.height), 4, rgba.data(), static_cast<int>(frame.row_pitch)) != 0;
		}
	}
	return true;
}

}

int Run(int argc, char **argv)
{
	// Unbuffered, so progress survives a crash.
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	auto options = ParseOptions(argc, argv);
	if (!options)
		return 2;

	// Content: the install's files and the level.
	engine::filesystem::VirtualFileSystem files;
	const auto mount = generalszh::content::MountInstall(files, options->install);
	for (const std::string &error : mount.errors)
		std::fprintf(stderr, "mount: %s\n", error.c_str());
	// The shell's map (GameEngine::init: GlobalData ShellMapName), unless one is asked for; and PlayIntro.
	{
		engine::config::Diagnostics shellDiagnostics;
		generalszh::content::ContentLoader shellLoader(files, generalszh::content::ZeroHourIniGrammar(shellDiagnostics));
		const engine::config::Document &gameDataSet = shellLoader.Load({"Data/INI/Default/GameData", "Data/INI/GameData"});
		engine::config::BindContext gameDataContext{shellLoader.DiagnosticsFor(gameDataSet), engine::time::FixedStep{30}};
		const auto gameData = generalszh::content::BindGameData(gameDataSet, gameDataContext);
		options->gameDataIntro = gameData.playIntro;
		if (options->map.empty())
		{
			options->map = gameData.shellMapName;
			for (char &c : options->map)
				c = c == '\\' ? '/' : c;
		}
	}
	const auto mapBytes = files.Read(options->map);
	if (!mapBytes)
	{
		std::fprintf(stderr, "map '%s' not found in %s\n", options->map.c_str(), options->install.string().c_str());
		return 1;
	}
	auto level = engine::level::generals_map::Read(*mapBytes);
	if (!level)
	{
		std::fprintf(stderr, "map '%s': %s\n", options->map.c_str(), level.error().c_str());
		return 1;
	}
	// The match's level and terrain (replaced when a game starts from the shell).
	auto currentLevel = std::make_unique<engine::level::Level>(std::move(level->level));
	const auto timeOfDayOf = [](const engine::level::Level &of) -> const engine::level::LightingSet * {
		const auto &lighting = of.lighting;
		return lighting.sets.empty() ? nullptr : &lighting.sets[lighting.current];
	};
	const engine::level::LightingSet *timeOfDay = timeOfDayOf(*currentLevel);
	auto mesh = std::make_unique<engine::level::presentation::TerrainMesh>(*currentLevel, engine::level::presentation::TerrainLightingInput{timeOfDay, 3});

	// Platform window and renderer.
	engine::platform::sdl3::SDL3PlatformAdapter platform;
	engine::platform::WindowConfig windowConfig;
	windowConfig.title = "Command & Conquer Generals: Zero Hour";
	windowConfig.size = {static_cast<int>(options->width), static_cast<int>(options->height)};
	windowConfig.resizable = false;
	auto window = platform.windows().create(windowConfig);
	const auto handle = window ? window->native_handle(engine::platform::NativeWindowSystem::win32) : std::nullopt;
	if (!handle || handle->window == nullptr)
	{
		std::fprintf(stderr, "window: %s\n", platform.last_error());
		return 1;
	}
	// Typed characters arrive as text input (the front end's entries take them).
	platform.text_input().start(window->id());
	FrameCapture frameCapture;
	frameCapture.shaders = GENERALSZH_SHADER_DIRECTORY;
	frameCapture.captureFile = options->screenshot;
	g_frameCapture = &frameCapture;
	Graphics::FrameDeviceOptions device;
	device.window = handle->window;
	device.width = options->width;
	device.height = options->height;
	const std::string shaderDirectory = frameCapture.shaders.string();
	device.shader_directory = shaderDirectory.c_str();
	if (!Graphics::Initialize_Frame_Device(device) ||
		!Graphics::Register_Frame_Draw_Executor(&InitializeRenderers, &ExecuteFrameDraws))
	{
		std::fprintf(stderr, "graphics device could not be created\n");
		return 1;
	}
	// Everything holding GPU or asset resources lives in this scope, so it is
	// released before the asset runtime and the frame device shut down.
	bool captured = false;
	{
		// Content loading (INI sets through the Zero Hour grammar).
		engine::config::Diagnostics grammarDiagnostics;
		generalszh::content::ContentLoader loader(files, generalszh::content::ZeroHourIniGrammar(grammarDiagnostics));

		// The mouse cursors: WinCursors (GlobalData's default) makes them system cursors, each of Mouse.ini's MouseCursor
		// blocks' Data/Cursors/<Texture>.ANI (one per direction), all loaded up front (Win32Mouse::initCursorResources).
		// The arrow shows first (W3DMouse::init) and in the shell; a match asks for the others (InGameUI::setMouseCursor).
		constexpr std::size_t cursorKinds = static_cast<std::size_t>(generalszh::content::MouseCursorKind::Count);
		std::array<std::vector<std::unique_ptr<Graphics::Cursor>>, cursorKinds> cursors;
		{
			const generalszh::content::MouseContent mouse = generalszh::content::BindMouse(loader.Load({"Data/INI/Mouse"}));
			for (std::size_t kind = 0; kind < cursorKinds; ++kind)
			{
				const auto &definition = mouse.cursors[kind];
				for (int direction = 0; direction < definition.directions && !definition.texture.empty(); ++direction)
				{
					const std::string path = generalszh::content::MouseCursorFile(definition, direction);
					auto cursor = std::make_unique<Graphics::Cursor>();
					const auto bytes = files.Read(path);
					if (!bytes || !Graphics::Load_Ani_Cursor(*cursor, *bytes, static_cast<int>(definition.fps.Floor()), definition.hotSpot[0], definition.hotSpot[1]))
					{
						std::fprintf(stderr, "mouse: cursor '%s' could not be read\n", path.c_str());
						cursor.reset();
					}
					cursors[kind].push_back(std::move(cursor));
				}
			}
		}
		std::array<std::uint8_t, 2> shownCursor{0xFF, 0xFF};
		// Win32Mouse::setCursor: NONE (or no cursor for it) shows none.
		const auto showCursor = [&](std::array<std::uint8_t, 2> wanted) {
			if (wanted == shownCursor)
				return;
			shownCursor = wanted;
			const auto &directions = cursors[std::min<std::size_t>(wanted[0], cursorKinds - 1)];
			const Graphics::Cursor *cursor = wanted[1] < directions.size() ? directions[wanted[1]].get() : nullptr;
			if (cursor == nullptr || !cursor->Show())
				std::fprintf(stderr, "mouse: cursor %u (direction %u) could not be shown\n", wanted[0], wanted[1]);
		};
		showCursor({static_cast<std::uint8_t>(generalszh::content::MouseCursorKind::Arrow), 0});

		// The world's renderers: terrain (textured base + 3-way overlay), objects, water (Water.ini, map.ini overrides), particles.
		auto scene = std::make_unique<generalszh::host::WorldScene>();
		std::string terrainError;
		std::string waterError;
		if (!scene->Load(files, loader, *currentLevel, *mesh, options->map, terrainError, waterError))
		{
			std::fprintf(stderr, "terrain: %s\n", terrainError.c_str());
			return 1;
		}
		if (!waterError.empty())
			std::fprintf(stderr, "water: %s\n", waterError.c_str());

		// Localized text (Data/<language>/Generals.csf).
		engine::localization::StringTable strings;
		if (const auto csf = files.Read("Data/English/Generals.csf"))
			if (const auto read = engine::localization::csf::Read(*csf, strings); !read)
				std::fprintf(stderr, "strings: %s\n", read.error().c_str());
		// Community patches ship their new labels as Data/Patch.str (e.g. GUI:CustomMission for their
		// main menu's WND); the original reads no such file, so it only matters when a patch is installed.
		if (const auto patch = files.ReadText("Data/Patch.str"))
			if (const auto read = engine::localization::str::Read(*patch, strings); !read)
				std::fprintf(stderr, "strings: Patch.str: %s\n", read.error().c_str());

		// Assets (textures, fonts, models) come from the install's files.
		if (!Assets::Initialize_Asset_Runtime(generalszh::host::GameAssetSource(files), generalszh::host::GameModelAdapters()))
		{
			std::fprintf(stderr, "asset runtime could not start\n");
			return 1;
		}
		// The shell map's scripts and the tactical camera they drive.
		// The map runs through the ordinary game session; the shell map is a map
		// its scripts play by themselves.
		generalszh::host::GameClient game;
		game.Load(loader, files, *currentLevel, [terrain = mesh.get()](float x, float y) { return terrain->HeightAt(x, y); }, mesh->PlayableSize(),
			static_cast<float>(options->width) / static_cast<float>(options->height), 0x5EED);
		game.SetGameSpeed(options->speed);
		if (options->look)
			game.PinCamera(options->look->first, options->look->second);
		std::printf("game: %zu entities, simulation on %zu worker threads at %.1f ticks/s\n", game.EntityCount(), game.WorkerCount(),
			game.TicksPerSecond());
		std::printf("scripts: %s\n", game.UnportedSummary().c_str());
		std::printf("audio: %.*s\n", static_cast<int>(game.AudioOutputName().size()), game.AudioOutputName().data());
		game.Update(0.0f, 1.0f);
		scene->Preload(game);
		std::printf("objects: %s\n", scene->Summary().c_str());
		Graphics::CameraState camera;

		// The front end over the shell map (main menu, options, credits).
		// Floating texts over the world ("+$300" where a truck delivered): GUI:AddCash, in the display string font.
		game.SetAddCashText(generalszh::host::Localized(strings, "GUI:AddCash"));
		game.SetLoseCashText(generalszh::host::Localized(strings, "GUI:LoseCash"));
		game.SetLabels([&strings](std::string_view label) { return generalszh::host::Localized(strings, label); });
		generalszh::host::WorldAnimationView worldAnimations;
		if (!worldAnimations.Load(files))
			std::fprintf(stderr, "world animations: the mapped images could not be read\n");
		generalszh::host::FloatingTextView floatingTexts;
		const generalszh::content::LanguageFonts languageFonts = generalszh::content::ReadLanguageFonts(loader.Load({"Data/English/Language"}));
		if (!floatingTexts.Load(languageFonts.displayString, generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment))))
			std::fprintf(stderr, "floating text: its font could not be made\n");
		// The messages at the top of the screen, in Language.ini's MessageFont (InGameUI.ini's Arial 10 bold without one).
		generalszh::host::MessageView messageView;
		if (!messageView.Load(languageFonts.message.name.empty() ? generalszh::content::LanguageFont{"Arial", 10, true} : languageFonts.message,
				generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment))))
			std::fprintf(stderr, "messages: their font could not be made\n");
		// The military caption, in Language.ini's MilitaryCaption fonts (InGameUI.ini's without them).
		generalszh::host::MilitaryCaptionView militaryCaption;
		{
			const generalszh::content::InGameUiContent ui = generalszh::content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"}));
			const auto pick = [](const generalszh::content::LanguageFont &language, const std::string &name, int size, bool bold) {
				return language.name.empty() ? generalszh::content::LanguageFont{name, size, bold} : language;
			};
			if (!militaryCaption.Load(pick(languageFonts.militaryCaptionTitle, ui.militaryCaptionTitleFont, ui.militaryCaptionTitlePointSize, ui.militaryCaptionTitleBold),
					pick(languageFonts.militaryCaption, ui.militaryCaptionFont, ui.militaryCaptionPointSize, ui.militaryCaptionBold),
					generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment))))
				std::fprintf(stderr, "military caption: its fonts could not be made\n");
		}
		// The named timers, in Language.ini's NamedTimerCountdown fonts (InGameUI.ini's without them).
		generalszh::host::NamedTimersView namedTimers;
		{
			const generalszh::content::InGameUiContent ui = generalszh::content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"}));
			const auto pick = [](const generalszh::content::LanguageFont &language, const std::string &name, int size, bool bold) {
				return language.name.empty() ? generalszh::content::LanguageFont{name, size, bold} : language;
			};
			if (!namedTimers.Load(pick(languageFonts.namedTimerNormal, ui.namedTimerNormalFont, ui.namedTimerNormalPointSize, ui.namedTimerNormalBold),
					pick(languageFonts.namedTimerReady, ui.namedTimerReadyFont, ui.namedTimerReadyPointSize, ui.namedTimerReadyBold),
					generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment))))
				std::fprintf(stderr, "named timers: their fonts could not be made\n");
		}
		// The superweapon countdowns, in Language.ini's SuperweaponCountdown fonts (InGameUI.ini's without them).
		generalszh::host::SuperweaponTimersView superweaponTimers;
		{
			const generalszh::content::InGameUiContent ui = generalszh::content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"}));
			const auto pick = [](const generalszh::content::LanguageFont &language, const std::string &name, int size, bool bold) {
				return language.name.empty() ? generalszh::content::LanguageFont{name, size, bold} : language;
			};
			if (!superweaponTimers.Load(pick(languageFonts.superweaponNormal, ui.superweaponNormalFont, ui.superweaponNormalPointSize, ui.superweaponNormalBold),
					pick(languageFonts.superweaponReady, ui.superweaponReadyFont, ui.superweaponReadyPointSize, ui.superweaponReadyBold), strings,
					generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment))))
				std::fprintf(stderr, "superweapon countdowns: their fonts could not be made\n");
		}
		generalszh::host::FrontEnd frontEnd;
		// The control bar, loaded as a match starts (ControlBar.wnd with the local player's scheme).
		generalszh::host::ControlBarLayer controlBar;
		bool controlBarLoaded = false;
		// The end of a game as its scripts declare it: the window it shows, and the ticks since (the end game timer:
		// FRAMES_TO_SHOW_WIN_LOSE_MESSAGE, 120, then GameLogic::exitGame; the close window timer likewise).
		generalszh::host::MatchEndLayer matchEnd;
		auto shownEnd = generalszh::host::ClientSettings::MatchEnd::None;
		std::uint64_t endTicks = 0;
		bool localDefeatShown = false;
		frontEnd.Load(files, strings, loader, game, {options->userData.empty() ? DefaultUserData() : options->userData, options->width, options->height, options->openOptions, options->startScreen, options->presses, options->lanLoopback,
			!options->screenshot.empty() || !options->presses.empty(), options->gamePresses});
		game.TraceScripts(options->scriptTrace);
		// Campaign.ini (CampaignManager): the campaigns. `playing`: what the match playing was started from (a skirmish's
		// setup, a campaign's or challenge's mission: its restart, its saves, the next mission after a victory);
		// `pending`: the match to start next.
		generalszh::content::CampaignCatalog campaigns;
		{
			const engine::config::Document &campaignSet = loader.Load({"Data/INI/Campaign"});
			engine::config::BindContext campaignContext{loader.DiagnosticsFor(campaignSet), engine::time::FixedStep{30}};
			campaigns = generalszh::content::BindCampaigns(campaignSet, campaignContext);
		}
		// Video.ini (VideoPlayer::init) and the display's movie (Display::playMovie).
		const generalszh::content::VideoCatalog videos = generalszh::content::BindVideos(loader.Load({"Data/INI/Default/Video", "Data/INI/Video"}));
		generalszh::host::MoviePlayer movie(&game.AudioMixer());
		if (!options->movie.empty() && !movie.Play(videos, options->install, "English", options->movie))
			std::fprintf(stderr, "movie: '%s' could not be played\n", options->movie.c_str());
		bool skipMovie = false; // Esc this frame (GameClient::isMovieAbortRequested)
		// The intro (GameClient::init's Intro, PlayIntro / PlaySizzle): not with -nologo, nor when the host starts on
		// something or captures (as the original's map, replay and load-save switches leave it out). While it runs the
		// shell and its map neither show nor run (showShellMap / showShell once it is done).
		// PlayIntro (GameData.ini) allows the logo; the sizzle movie is always allowed (PlaySizzle has no INI field).
		const bool automated = options->nologo || options->mapGiven || !options->startScreen.empty() || !options->movie.empty() ||
			!options->presses.empty() || !options->screenshot.empty() || options->frames != 0 || options->matchFrames != 0 || options->openOptions;
		generalszh::shell::IntroSequence intro = generalszh::shell::MakeIntro(!automated && options->gameDataIntro, !automated);
		const auto introStart = std::chrono::steady_clock::now();
		// timeGetTime: milliseconds on a clock that is never 0 (a wait's end compares with it).
		const auto introNow = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - introStart).count()) + 1000; };

		auto movieClock = std::chrono::steady_clock::now();
		using generalszh::session::setup::MatchKind;
		using generalszh::session::setup::MatchPlan;
		// A new mission waiting for its intro movie (SinglePlayerLoadScreen::init plays the mission's movie before the
		// load goes on).
		std::optional<MatchPlan> briefing;
		std::optional<MatchPlan> playing;
		std::optional<MatchPlan> pending;
		std::optional<MatchPlan> afterScores; // the score screen's Continue: the next mission, or the same one again
		// A saved game's own block: this version, the match's plan and its checkpoint.
		constexpr std::uint32_t SaveGameVersion = 2; // 2: the plan's campaign rank points
		std::string playingMap; // the map of the match playing
		bool paused = false;       // GameLogic::setGamePaused (the quit menu over a single-player game or a skirmish)
		bool exitRequested = false; // the quit menu's Exit (GameLogic::quit): back to the shell

		// A match in place of the one playing (GameLogic::startNewGame), made from its plan; from `checkpoint` when a saved
		// game goes on. A skirmish: its map made ready for its setup, its seats' players starting there, the local
		// player's side scripts and MultiplayerScripts.scb's run locally. A campaign's mission (GAME_SINGLE_PLAYER): its
		// map with its own players, the local human playing its human side at the campaign's difficulty; a Generals'
		// Challenge's: its map with the human added as the general, playing "ThePlayer"'s part. False: not started.
		const auto launch = [&](const MatchPlan &plan, std::span<const std::byte> checkpoint) -> bool {
			std::string path;
			std::optional<engine::level::Level> level;
			std::vector<std::string> seats;
			std::vector<generalszh::host::PlayerStart> starts;
			std::vector<engine::level::ScriptList> localLists;
			std::string cameraMarker = "InitialCameraPosition";
			std::optional<std::uint8_t> solo;
			std::uint64_t seed = 0;
			std::size_t players = 0;
			const bool challenge = plan.kind == MatchKind::Challenge;
			const bool lan = plan.kind == MatchKind::Lan;
			std::optional<generalszh::session::NetworkMatchOptions> network;
			const generalszh::content::Mission *mission = nullptr;
			if (plan.FromSetup())
				path = plan.setup.map;
			else
			{
				const generalszh::content::Campaign *campaign = campaigns.Find(plan.campaign);
				mission = campaign != nullptr ? campaign->FindMission(plan.mission) : nullptr;
				if (mission == nullptr)
				{
					std::fprintf(stderr, "campaign: '%s' has no mission '%s'\n", plan.campaign.c_str(), plan.mission.c_str());
					return false;
				}
				path = mission->map;
				solo = plan.difficulty;
			}
			for (char &c : path)
				c = c == '\\' ? '/' : c;
			const auto bytes = files.Read(path);
			auto read = bytes ? std::optional(engine::level::generals_map::Read(*bytes)) : std::nullopt;
			if (!read || !*read)
			{
				std::fprintf(stderr, "game: map '%s' could not be read\n", path.c_str());
				return false;
			}
			int spots = 0;
			for (const auto &marker : (*read)->level.markers)
				spots += marker.name.starts_with("Player_") && marker.name.ends_with("_Start") ? 1 : 0;
			if (plan.FromSetup())
			{
				seed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(plan.setup.seed));
				// The computer players' standard scripts and teams (SidesList::prepareForMP_or_Skirmish).
				std::optional<engine::level::generals_map::ScriptFile> skirmishScripts;
				if (const auto scb = files.Read("Data/Scripts/SkirmishScripts.scb"))
					if (auto parsed = engine::level::generals_map::ReadScriptFile(*scb))
						skirmishScripts = std::move(*parsed);
				auto skirmish = generalszh::session::setup::PrepareSkirmishLevel((*read)->level, plan.setup, game.PlayerTemplates(), spots,
					skirmishScripts ? &*skirmishScripts : nullptr);
				players = skirmish.players.size();
				// The humans' seats: a skirmish's one human in seat 0; a LAN game's humans in slot order (the relay's seats),
				// this machine's the one in its slot.
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
							cameraMarker = "Player_" + std::to_string(player.startPosition + 1) + "_Start";
						}
					}
					starts.push_back({player.name, player.team, player.playerTemplate, player.startPosition});
				}
				std::ranges::sort(humans);
				for (const auto &human : humans)
					seats.push_back(human.second);
				if (lan)
				{
					network.emplace();
					network->seat = static_cast<std::uint32_t>(std::ranges::find(seats, localPlayer) - seats.begin());
					network->players = static_cast<std::uint32_t>(seats.size());
					if (!plan.hosting)
						network->hostAddress = plan.hostAddress;
				}
				// The local human's side scripts (the civilian skirmish side's: which music plays, on the local player's side
				// and losses) answer for "<Local Player>": they run on this machine only, beside MultiplayerScripts.scb's
				// (Player::initFromDict gives them to the human; each machine hears its own player's). Every other human's
				// run on its own machine.
				for (auto &side : skirmish.level.scenario.participants)
				{
					const std::string name = side.properties.Get<std::string>("playerName").value_or("");
					if (std::ranges::find(seats, name) == seats.end() || (side.scripts.scripts.empty() && side.scripts.groups.empty()))
						continue;
					if (name == localPlayer)
						localLists.push_back(std::move(side.scripts));
					side.scripts = {};
				}
				// The names its messages give the players (GameSlot::getName: a human's own, an AI's by its level).
				std::vector<std::pair<std::string, std::u16string>> names;
				for (const auto &player : skirmish.players)
				{
					const auto &slot = plan.setup.slots[static_cast<std::size_t>(player.slot)];
					using State = generalszh::session::setup::SlotState;
					names.emplace_back(player.name, slot.Human() ? slot.name
						: generalszh::host::Localized(strings, slot.state == State::EasyAI ? "GUI:EasyAI" : slot.state == State::MediumAI ? "GUI:MediumAI" : "GUI:HardAI"));
				}
				game.SetPlayerNames(std::move(names), generalszh::host::Localized(strings, "GUI:PlayerHasBeenDefeated"));
				// GameLogic::startNewGame: a game of more than one team adds MultiplayerScripts.scb's victory and defeat
				// scripts (its first list), here the local player's own (see UseMatchScripts).
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
							localLists.insert(localLists.begin(), std::move(scripts->lists.front()));
						else
							std::fprintf(stderr, "game: MultiplayerScripts.scb could not be read\n");
					}
				level = std::move(skirmish.level);
			}
			else if (challenge)
			{
				// The general's PlayerTemplate in the one human slot, beside the map's own players.
				int templateIndex = -1;
				const auto &templates = game.PlayerTemplates();
				for (std::size_t index = 0; index < templates.templates.size(); ++index)
					if (templates.templates[index].name == plan.playerTemplate)
						templateIndex = static_cast<int>(index);
				auto prepared = generalszh::session::setup::PrepareChallengeLevel((*read)->level, templateIndex, templates, spots, 0,
					generalszh::session::setup::GameSetup{}.startingCash);
				const auto &human = prepared.players.front();
				seats.push_back(human.name);
				starts.push_back({human.name, human.team, human.playerTemplate, human.startPosition});
				cameraMarker = "Player_" + std::to_string(human.startPosition + 1) + "_Start";
				level = std::move(prepared.level);
			}
			else
			{
				for (const auto &side : (*read)->level.scenario.participants)
					if (side.properties.Get<bool>("playerIsHuman").value_or(false))
					{
						seats.push_back(side.properties.Get<std::string>("playerName").value_or(""));
						break;
					}
				level = std::move((*read)->level);
			}
			// MainMenu doGameStart / ScoreScreen startNextCampaignGame / ChallengeMenu: InitRandom(0).
			if (!plan.FromSetup() && options->seed)
				seed = static_cast<std::uint64_t>(static_cast<std::uint32_t>(*options->seed));
			scene.reset();
			currentLevel = std::make_unique<engine::level::Level>(std::move(*level));
			timeOfDay = timeOfDayOf(*currentLevel);
			mesh = std::make_unique<engine::level::presentation::TerrainMesh>(*currentLevel, engine::level::presentation::TerrainLightingInput{timeOfDay, 3});
			scene = std::make_unique<generalszh::host::WorldScene>();
			if (!scene->Load(files, loader, *currentLevel, *mesh, path, terrainError, waterError))
				std::fprintf(stderr, "terrain: %s\n", terrainError.c_str());
			if (!game.Start(*currentLevel, [terrain = mesh.get()](float x, float y) { return terrain->HeightAt(x, y); }, mesh->PlayableSize(),
					static_cast<float>(options->width) / static_cast<float>(options->height), seed, std::move(seats), std::move(starts), cameraMarker, solo,
					challenge, checkpoint, network, plan.rankPoints))
			{
				std::fprintf(stderr, "game: the saved game does not fit its map\n");
				return false;
			}
			if (!localLists.empty())
				game.UseMatchScripts(std::move(localLists));
			game.Update(0.0f, 1.0f);
			scene->Preload(game);
			frontEnd.EnterGame();
			if (!controlBarLoaded)
			{
				std::string barError;
				controlBarLoaded = controlBar.Load(files, strings, loader, game, options->width, options->height,
					generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment)), barError);
				if (!controlBarLoaded)
					std::fprintf(stderr, "control bar: %s\n", barError.c_str());
			}
			// Radar::newMap: the level's radar picture.
			if (controlBarLoaded && scene)
				controlBar.SetRadarTerrain(scene->BuildRadarTerrain(*currentLevel, game));
			playing = plan;
			playingMap = path;
			if (mission != nullptr)
				std::printf("campaign %s: %s (%s), %zu entities\n", plan.campaign.c_str(), mission->name.c_str(), path.c_str(), game.EntityCount());
			else
				std::printf("game: %s with %zu players, %zu entities\n", path.c_str(), players, game.EntityCount());
			return true;
		};
		// SkirmishStats.ini (SkirmishBattleHonors): read, changed and written back at once, as the score screen does.
		const std::filesystem::path userData = options->userData.empty() ? DefaultUserData() : options->userData;
		const auto updateStats = [&](auto &&change) {
			const std::filesystem::path file = userData / "SkirmishStats.ini";
			std::string text;
			if (std::ifstream in{file, std::ios::binary})
				text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			engine::config::Preferences stats = engine::config::Preferences::Parse(text);
			change(stats);
			std::error_code error;
			std::filesystem::create_directories(userData, error);
			std::ofstream(file, std::ios::binary) << stats.Write();
		};
		// The date now, as a save records it (SYSTEMTIME fields).
		const auto saveDateNow = [] {
			const std::time_t now = std::time(nullptr);
			std::tm local{};
			localtime_s(&local, &now);
			return generalszh::shell::SaveDate{static_cast<std::uint16_t>(local.tm_year + 1900), static_cast<std::uint16_t>(local.tm_mon + 1),
				static_cast<std::uint16_t>(local.tm_mday), static_cast<std::uint16_t>(local.tm_wday), static_cast<std::uint16_t>(local.tm_hour),
				static_cast<std::uint16_t>(local.tm_min), static_cast<std::uint16_t>(local.tm_sec), 0};
		};
		// A save file written in the save folder: over `overwrite`, else under the first free number (findNextSaveFilename:
		// "00000000.sav" on). The file; none when it could not be written.
		const auto writeSave = [&](const generalszh::shell::SaveGameInfo &info, std::span<const std::byte> body,
								   const std::optional<std::string> &overwrite) -> std::optional<std::filesystem::path> {
			const std::vector<std::byte> file = generalszh::shell::WriteSaveGame(info, body);
			const std::filesystem::path folder = frontEnd.SaveFolder();
			std::error_code error;
			std::filesystem::create_directories(folder, error);
			if (overwrite)
			{
				std::ofstream out(folder / *overwrite, std::ios::binary | std::ios::trunc);
				out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
				return out ? std::optional(folder / *overwrite) : std::nullopt;
			}
			for (int number = 0; number < 100000000; ++number)
			{
				char name[16];
				std::snprintf(name, sizeof(name), "%08d.sav", number);
				const std::filesystem::path target = folder / name;
				if (std::filesystem::exists(target, error))
					continue;
				std::ofstream out(target, std::ios::binary);
				out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
				if (!out)
					return std::nullopt;
				return target;
			}
			return std::nullopt;
		};
		// GameState::saveGame: the match playing, its info (the date now, the description) first, then its plan,
		// checkpoint and client state. The file; none when there is no match to save.
		const auto saveGame = [&](std::optional<std::string> overwrite, std::u16string description) -> std::optional<std::filesystem::path> {
			if (!playing)
				return std::nullopt;
			const std::vector<std::byte> checkpoint = game.Checkpoint();
			if (checkpoint.empty())
				return std::nullopt;
			engine::core::serialization::ByteWriter writer;
			writer.U32(SaveGameVersion);
			generalszh::session::setup::WriteMatchPlan(writer, *playing);
			writer.Blob(checkpoint);
			game.SaveClientState(writer);
			generalszh::shell::SaveGameInfo info;
			info.missionMap = playingMap;
			for (char &c : info.missionMap)
				c = c == '/' ? '\\' : c;
			info.date = saveDateNow();
			info.description = std::move(description);
			return writeSave(info, writer.Bytes(), overwrite);
		};
		// GameState::missionSave (the score screen after a mission won with one to follow): a SAVE_FILE_TYPE_MISSION save
		// holding only the campaign's state (the next mission's plan: its campaign, mission, difficulty and rank points),
		// no game (an empty checkpoint); described by "GUI:MissionSave" with the campaign's name and the mission's number.
		const auto missionSave = [&](const MatchPlan &next) -> std::optional<std::filesystem::path> {
			const generalszh::content::Campaign *campaign = campaigns.Find(next.campaign);
			const generalszh::content::Mission *mission = campaign != nullptr ? campaign->FindMission(next.mission) : nullptr;
			if (mission == nullptr)
				return std::nullopt;
			engine::core::serialization::ByteWriter writer;
			writer.U32(SaveGameVersion);
			generalszh::session::setup::WriteMatchPlan(writer, next);
			writer.Blob({});
			generalszh::shell::SaveGameInfo info;
			info.type = generalszh::shell::SaveFileType::Mission;
			info.missionMap = mission->map;
			info.date = saveDateNow();
			info.description = generalszh::shell::MissionSaveDescription(generalszh::host::Localized(strings, "GUI:MissionSave"),
				generalszh::host::Localized(strings, campaign->nameLabel.c_str()), campaign->MissionNumber(mission) + 1);
			return writeSave(info, writer.Bytes(), std::nullopt);
		};

		// The menus over a game (QuitMenu, PopupSaveLoad): what they do to the game.
		{
			generalszh::host::FrontEnd::GameMenus menus;
			menus.quit.exit = [&] { exitRequested = true; };
			// restartMissionMenu: the same match again from its start (a skirmish on its seed, a mission on 0).
			menus.quit.restart = [&] {
				if (playing)
					pending = *playing;
			};
			menus.quit.pause = [&](bool on) { paused = on; };
			// surrenderQuitMenu: MSG_SELF_DESTRUCT, its assets to a living ally.
			menus.quit.surrender = [&] { game.Submit(generalszh::commands::SelfDestruct{true}); };
			menus.mode = [&] {
				if (playing && playing->kind == MatchKind::Lan)
					return generalszh::shell::QuitMenuMode::Multiplayer;
				return playing && playing->kind == MatchKind::Skirmish ? generalszh::shell::QuitMenuMode::Skirmish : generalszh::shell::QuitMenuMode::SinglePlayer;
			};
			menus.inputEnabled = [&] { return !game.Settings().inputDisabled; };
			menus.beaten = [] { return false; };
			menus.save = [&](const std::optional<std::string> &file, const std::u16string &description) {
				if (const auto saved = saveGame(file, description))
					std::printf("save: %s\n", saved->string().c_str());
				else
					std::fprintf(stderr, "save: the game could not be saved\n");
			};
			// setEditDescription: the campaign's name and mission number, else the map's file name without its extension.
			menus.defaultDescription = [&]() -> std::u16string {
				if (playing && playing->SinglePlayer())
					if (const auto *campaign = campaigns.Find(playing->campaign))
						return generalszh::host::Localized(strings, campaign->nameLabel.c_str()) + u' ' +
							[&] {
								const std::string number = std::to_string(campaign->MissionNumber(campaign->FindMission(playing->mission)) + 1);
								return std::u16string(number.begin(), number.end());
							}();
				std::string leaf = playingMap.substr(playingMap.find_last_of("/\\") + 1);
				if (leaf.size() >= 4 && leaf[leaf.size() - 4] == '.')
					leaf.resize(leaf.size() - 4);
				return std::u16string(leaf.begin(), leaf.end());
			};
			frontEnd.SetGameMenus(std::move(menus));
			controlBar.SetOptionsAction([&frontEnd] { frontEnd.ToggleQuitMenu(); });
		}

		// Fixed logic ticks (30 per second at game speed 1), rendering uncapped.
		const engine::time::FixedStep step{30};
		engine::time::SimulationClock clock{step};
		auto previous = std::chrono::steady_clock::now();
		auto frameStart = previous;
		std::chrono::nanoseconds accumulator{};
		generalszh::host::PointerState pointer;
		std::uint32_t keyModifiers = 0;
		std::uint64_t inGameFrames = 0; // frames since a match started (on after it ends: --match-frames captures the shell after it)
		// The letterbox bars' fade (W3DDisplay m_letterBoxFadeLevel, m_letterBoxFadeStartTime) and the scripts' changes seen.
		float letterboxLevel = 0.0f;
		auto letterboxStart = previous;
		std::uint32_t letterboxChanges = 0;
		for (std::uint64_t frame = 0; (options->frames == 0 || frame < options->frames) && (options->matchFrames == 0 || inGameFrames < options->matchFrames); ++frame)
		{
			bool quit = false;
			engine::platform::PlatformEvent event;
			while (platform.events().poll(event))
			{
				quit = quit || event.type == engine::platform::EventType::quit || event.type == engine::platform::EventType::window_close_requested;
				// WindowXlat: during the intro, Esc (let go) skips its stage; nothing reaches the shell.
				if (!generalszh::shell::IntroDone(intro))
				{
					bool stop = false;
					if (event.type == engine::platform::EventType::key_up && event.key == engine::platform::KeyCode::escape &&
						generalszh::shell::SkipIntroStage(intro, stop) && stop)
						movie.Stop();
					continue;
				}
				frontEnd.Handle(event);
				// In a match the pointer is the world's (left 1, right 3 in the platform's numbering), but under a menu over it.
				if (frontEnd.InGame() && (!frontEnd.GameMenuShown() || event.type == engine::platform::EventType::key_down))
				{
					using engine::platform::EventType;
					const std::uint8_t button = event.code == 1 ? 1u : event.code == 3 ? 2u : 0u;
					switch (event.type)
					{
					case EventType::mouse_moved: pointer.x = static_cast<float>(event.position.x); pointer.y = static_cast<float>(event.position.y); break;
					case EventType::mouse_button_down:
						pointer.x = static_cast<float>(event.position.x);
						pointer.y = static_cast<float>(event.position.y);
						pointer.down |= button;
						pointer.pressed |= button;
						break;
					case EventType::mouse_button_up:
						pointer.x = static_cast<float>(event.position.x);
						pointer.y = static_cast<float>(event.position.y);
						pointer.down &= static_cast<std::uint8_t>(~button);
						pointer.released |= button;
						break;
					case EventType::key_down:
					case EventType::key_up:
					{
						keyModifiers = event.modifiers;
						using engine::platform::KeyCode;
						const std::uint8_t arrow = event.key == KeyCode::up ? 1u : event.key == KeyCode::down ? 2u : event.key == KeyCode::left ? 4u : event.key == KeyCode::right ? 8u : 0u;
						// GeneralsExpPointsInput: Esc closes the General's Powers screen; else Esc (key down) is the OPTIONS
						// meta event (CommandMap.ini): ToggleQuitMenu.
						// isMovieAbortRequested: Esc skips a movie playing (and does nothing else then).
						if (event.type == EventType::key_down && event.key == KeyCode::escape && movie.Playing())
							skipMovie = true;
						else if (event.type == EventType::key_down && event.key == KeyCode::escape && !(controlBarLoaded && !frontEnd.GameMenuShown() && controlBar.Escape()))
							frontEnd.ToggleQuitMenu();
						if (event.type == EventType::key_down)
							pointer.arrows |= arrow;
						else
							pointer.arrows &= static_cast<std::uint8_t>(~arrow);
						break;
					}
					case EventType::mouse_wheel: pointer.wheel += event.flipped ? -event.y : event.y; break;
					default: break;
					}
				}
			}
			if (!frontEnd.InGame() && inGameFrames != 0)
				++inGameFrames;
			if (frontEnd.InGame())
			{
				// A scripted drag (--drag): down 60 frames into the match, across the next, up the one after.
				++inGameFrames;
				if (options->saveAt != 0 && inGameFrames == options->saveAt)
				{
					if (const auto saved = saveGame(std::nullopt, u"Check"))
						std::printf("save: %s with %zu entities\n", saved->string().c_str(), game.EntityCount());
					else
						std::fprintf(stderr, "save: nothing saved\n");
				}
				// Scripted clicks (--click): down and up at 70 frames into the match, then every 20 frames; the pointer
				// arrives 10 frames before each (a ghost being placed follows it there).
				for (std::size_t click = 0; click < options->clicks.size(); ++click)
				{
					const std::uint64_t at = 70 + click * 20;
					if (inGameFrames + 10 == at || inGameFrames == at || inGameFrames == at + 1)
					{
						pointer.x = options->clicks[click].first;
						pointer.y = options->clicks[click].second;
					}
					if (inGameFrames == at)
					{
						pointer.down |= 1u;
						pointer.pressed |= 1u;
					}
					else if (inGameFrames == at + 1)
					{
						pointer.down &= static_cast<std::uint8_t>(~1u);
						pointer.released |= 1u;
					}
				}
				if (options->drag && inGameFrames >= 60 && inGameFrames <= 62)
				{
					const auto &box = *options->drag;
					pointer.x = inGameFrames == 60 ? box[0] : box[2];
					pointer.y = inGameFrames == 60 ? box[1] : box[3];
					if (inGameFrames == 60)
					{
						pointer.down |= 1u;
						pointer.pressed |= 1u;
					}
					else if (inGameFrames == 62)
					{
						pointer.down &= static_cast<std::uint8_t>(~1u);
						pointer.released |= 1u;
					}
				}
				pointer.shift = (keyModifiers & engine::platform::modifier_shift) != 0;
				pointer.ctrl = (keyModifiers & engine::platform::modifier_control) != 0;
				pointer.alt = (keyModifiers & engine::platform::modifier_alt) != 0;
				pointer.timeMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
				game.SetViewport(static_cast<float>(options->width), static_cast<float>(options->height));
				// The control bar takes the mouse over it (the world then takes no clicks).
				pointer.overInterface = controlBarLoaded && controlBar.Point(pointer.x, pointer.y, pointer.pressed, pointer.released, pointer.timeMs);
				// The debug cheat MSG_META_DEMO_KILL_ALL_ENEMIES, scripted (--kill-enemies-at).
				if (options->killEnemiesAt != 0 && inGameFrames == options->killEnemiesAt)
					game.Submit(generalszh::commands::KillAllEnemies{});
				// doDisableInput: once the game is won or lost the player gives no more orders.
				if (!game.Settings().inputDisabled)
					game.Point(pointer);
				pointer.pressed = pointer.released = 0;
				pointer.wheel = 0;
			}
			const auto now = std::chrono::steady_clock::now();
			accumulator += now - previous;
			previous = now;
			const auto tickLength = std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / game.TicksPerSecond()));
			if (options->tickPerFrame)
				accumulator = tickLength;
			// The shell map stops while the credits show (CreditsMenuInit: showShellMap(FALSE)); a paused game stops.
			if (!frontEnd.ShellMapShown() || (paused && frontEnd.InGame()) || !generalszh::shell::IntroDone(intro))
				accumulator = {};
			while (accumulator >= tickLength)
			{
				clock.Advance();
				game.Tick();
				accumulator -= tickLength;
				if (frontEnd.InGame() && shownEnd != generalszh::host::ClientSettings::MatchEnd::None)
					++endTicks;
			}
			// The cursor the match asks for; the arrow in the shell.
			showCursor(frontEnd.InGame() ? game.MouseCursor().value_or(std::array<std::uint8_t, 2>{2, 0})
										 : std::array<std::uint8_t, 2>{static_cast<std::uint8_t>(generalszh::content::MouseCursorKind::Arrow), 0});
			// What the local player's scripts declared: its window (ObserverQuit.wnd for an observer or one who already
			// lost), closed after the close timer for their own defeat, the game left after the end timer otherwise.
			if (frontEnd.InGame())
			{
				using MatchEnd = generalszh::host::ClientSettings::MatchEnd;
				const MatchEnd declared = game.Settings().matchEnd;
				if (declared != shownEnd && declared != MatchEnd::None)
				{
					shownEnd = declared;
					endTicks = 0;
					const bool observer = !game.LocalPlayer().has_value();
					std::printf("game end: %s%s\n", declared == MatchEnd::Victory ? "victory" : declared == MatchEnd::QuickVictory ? "quick victory"
						: declared == MatchEnd::Defeat ? "defeat" : "local defeat", observer ? " (observer)" : "");
					const char *wnd = declared == MatchEnd::LocalDefeat ? "Window/Menus/LocalDefeat.wnd"
						: observer || localDefeatShown ? "Window/Menus/ObserverQuit.wnd"
						: declared == MatchEnd::Victory ? "Window/Menus/Victorious.wnd" : "Window/Menus/Defeat.wnd";
					localDefeatShown = localDefeatShown || declared == MatchEnd::LocalDefeat;
					std::string endError;
					if ((declared == MatchEnd::LocalDefeat && observer) || declared == MatchEnd::QuickVictory)
						matchEnd.Close(); // doQuickVictory closes the windows and shows none
					else if (!matchEnd.Show(files, wnd, strings, options->width, options->height,
							 generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment)), endError))
						std::fprintf(stderr, "game end: %s\n", endError.c_str());
				}
				if (shownEnd == MatchEnd::LocalDefeat && endTicks >= 120)
					matchEnd.Close();
				// The end timer: FRAMES_TO_SHOW_WIN_LOSE_MESSAGE after a victory or defeat, one frame after a quick victory.
				if (((shownEnd == MatchEnd::Victory || shownEnd == MatchEnd::Defeat) && endTicks >= 120) || (shownEnd == MatchEnd::QuickVictory && endTicks >= 1) ||
					std::exchange(exitRequested, false))
				{
					// GameLogic::exitGame (clearGameData): the score screen over the shell, from the game as it ended. A campaign
					// won goes on to its next mission from there (CampaignManager::gotoNextMission), lost it tries the same one
					// again; a skirmish just shows the scores.
					generalszh::shell::ScoreScreenSetup scores;
					scores.players = game.ScoreBoard();
					if (playing && playing->SinglePlayer())
					{
						scores.kind = generalszh::shell::ScoreScreenKind::SinglePlayer;
						scores.victorious = shownEnd == MatchEnd::Victory || shownEnd == MatchEnd::QuickVictory;
						const generalszh::content::Campaign *campaign = campaigns.Find(playing->campaign);
						const generalszh::content::Mission *next = scores.victorious && campaign != nullptr
							? campaign->NextMission(campaign->FindMission(playing->mission)) : nullptr;
						scores.campaignOver = scores.victorious && next == nullptr;
						// finishSinglePlayerInit: the campaign won to its end is recorded at its difficulty, and its final
						// victory movie plays over the score screen (PlayMovieAndBlock).
						if (scores.campaignOver)
						{
							updateStats([&](engine::config::Preferences &stats) {
								generalszh::shell::RecordCampaignComplete(stats, playing->campaign, playing->difficulty);
							});
							if (campaign != nullptr && !campaign->finalVictoryMovie.empty())
								movie.Play(videos, options->install, "English", campaign->finalVictoryMovie);
						}
						afterScores = *playing;
						// ScoreScreen initSinglePlayer: CampaignManager::setRankPoints(the local player's skill points), carried
						// into the next mission or the retry alike.
						afterScores->rankPoints = game.LocalSkillPoints();
						if (next != nullptr)
						{
							afterScores->mission = next->name;
							// finishSinglePlayerInit: won with a mission to follow, an automatic mission save of it.
							scores.gameSaved = true; // shown whatever the save's result, as the original
							if (const auto saved = missionSave(*afterScores))
								std::printf("save: %s (mission)\n", saved->string().c_str());
							else
								std::fprintf(stderr, "save: the mission could not be saved\n");
						}
						else if (scores.victorious)
							afterScores.reset();
						for (const auto &player : scores.players)
							if (player.local)
								scores.localScoreScreenImage = player.scoreScreenImage;
					}
					else
					{
						// populatePlayerInfo, SCORESCREEN_SKIRMISH: the local player's record, with the other seats as they were
						// set up (a computer's level; on the local player's team: isSlotLocalAlly), unless a sandbox (every
						// other seat on the local player's team: GameInfo::isSandbox).
						if (playing && playing->kind == MatchKind::Skirmish)
							if (const auto local = game.LocalPlayer(); local && game.View() != nullptr)
							{
								generalszh::shell::SkirmishGameRecord record = generalszh::hud::ReadGameRecord(*game.View(), *local);
								record.map = playing->setup.map;
								const auto &slots = playing->setup.slots;
								const int localSlot = playing->localSlot;
								const int localTeam = slots[static_cast<std::size_t>(localSlot)].team;
								bool sandbox = true;
								for (int slot = 0; slot < generalszh::session::setup::MaxSlots; ++slot)
								{
									if (slot == localSlot)
										continue;
									const auto &seat = slots[static_cast<std::size_t>(slot)];
									const bool ally = seat.team >= 0 && seat.team == localTeam;
									if (seat.Occupied() && !ally)
										sandbox = false;
									record.others.push_back({seat.AI() ? static_cast<std::uint8_t>(seat.state) : std::uint8_t{0}, ally, seat.Occupied()});
								}
								updateStats([&](engine::config::Preferences &stats) { generalszh::shell::RecordSkirmishGame(stats, record, sandbox); });
							}
						// grabMultiPlayerInfo: the seats' players (player<slot>), in slot order.
						std::vector<generalszh::shell::ScorePlayer> seats;
						if (playing)
							for (int slot = 0; slot < generalszh::session::setup::MaxSlots; ++slot)
								if (playing->setup.slots[static_cast<std::size_t>(slot)].Occupied())
								{
									const std::string name = "player" + std::to_string(slot);
									for (const auto &player : scores.players)
										if (player.playerName == name)
											seats.push_back(player);
								}
						scores.players = std::move(seats);
						afterScores.reset();
					}
					frontEnd.ShowScoreScreen(scores);
					// Back to the shell over the shell map.
					paused = false;
					playing.reset();
					matchEnd.Close();
					shownEnd = MatchEnd::None;
					localDefeatShown = false;
					if (const auto shellBytes = files.Read(options->map))
						if (auto shell = engine::level::generals_map::Read(*shellBytes))
						{
							scene.reset();
							currentLevel = std::make_unique<engine::level::Level>(std::move(shell->level));
							timeOfDay = timeOfDayOf(*currentLevel);
							mesh = std::make_unique<engine::level::presentation::TerrainMesh>(*currentLevel,
								engine::level::presentation::TerrainLightingInput{timeOfDay, 3});
							scene = std::make_unique<generalszh::host::WorldScene>();
							if (!scene->Load(files, loader, *currentLevel, *mesh, options->map, terrainError, waterError))
								std::fprintf(stderr, "terrain: %s\n", terrainError.c_str());
							game.Start(*currentLevel, [terrain = mesh.get()](float x, float y) { return terrain->HeightAt(x, y); }, mesh->PlayableSize(),
								static_cast<float>(options->width) / static_cast<float>(options->height), 0x5EED);
							game.Update(0.0f, 1.0f);
							scene->Preload(game);
						}
					frontEnd.LeaveGame();
				}
			}

			// The score screen's choice: Continue plays on (startNextCampaignGame), OK goes back to the main menu.
			if (const auto choice = frontEnd.TakeScoreChoice(); choice != generalszh::shell::ScoreChoice::None)
			{
				if (choice == generalszh::shell::ScoreChoice::Continue && afterScores)
					pending = *afterScores;
				afterScores.reset();
			}
			// A campaign chosen in the shell: its first mission (CampaignManager::setCampaign).
			if (auto chosen = frontEnd.TakeCampaignStart())
			{
				const generalszh::content::Campaign *campaign = campaigns.Find(chosen->campaign);
				const generalszh::content::Mission *first = campaign != nullptr ? campaign->NextMission(nullptr) : nullptr;
				if (first == nullptr)
					std::fprintf(stderr, "campaign: '%s' has no missions\n", chosen->campaign.c_str());
				else
				{
					MatchPlan plan;
					plan.kind = campaign->challenge && !chosen->playerTemplate.empty() ? MatchKind::Challenge : MatchKind::Campaign;
					plan.campaign = campaign->name;
					plan.mission = first->name;
					plan.difficulty = chosen->difficulty;
					plan.playerTemplate = chosen->playerTemplate;
					pending = std::move(plan);
				}
			}
			// A game started from the shell (the skirmish screen's Start).
			if (auto start = frontEnd.TakeGameStart())
			{
				if (options->seed)
					start->seed = *options->seed;
				MatchPlan plan;
				plan.setup = std::move(*start);
				pending = std::move(plan);
			}
			// A LAN game started (LANAPI::OnGameStart: MSG_NEW_GAME with GAME_LAN, InitRandom(its seed)).
			if (auto start = frontEnd.TakeLanStart())
			{
				MatchPlan plan;
				plan.kind = MatchKind::Lan;
				plan.setup = std::move(start->setup);
				plan.localSlot = start->localSlot;
				plan.hostAddress = start->hostAddress;
				plan.hosting = start->hosting;
				pending = std::move(plan);
			}
			// A saved game chosen on the load screen (GameState::loadGame): its level made again from its plan, the game
			// going on from its checkpoint.
			if (auto file = frontEnd.TakeLoadRequest())
			{
				std::ifstream in(*file, std::ios::binary);
				std::vector<std::byte> bytes;
				for (char c; in.get(c);)
					bytes.push_back(static_cast<std::byte>(c));
				const auto saved = generalszh::shell::ReadSaveGame(bytes);
				engine::core::serialization::ByteReader reader(saved ? std::span<const std::byte>(*saved) : std::span<const std::byte>{});
				const auto version = reader.U32();
				const auto plan = version && *version == SaveGameVersion ? generalszh::session::setup::ReadMatchPlan(reader) : std::nullopt;
				const auto checkpoint = plan ? reader.Blob() : std::nullopt;
				// A mission save (no game in it: loadGame's SAVE_FILE_TYPE_MISSION) starts its mission anew (MSG_NEW_GAME with
				// the campaign's difficulty and rank points, InitRandom(0)).
				if (checkpoint && checkpoint->empty() && plan->SinglePlayer())
				{
					if (!launch(*plan, {}))
						std::fprintf(stderr, "load: '%s' has a mission that could not start\n", file->string().c_str());
				}
				else if (!checkpoint || !launch(*plan, *checkpoint))
					std::fprintf(stderr, "load: '%s' is no saved game of this version\n", file->string().c_str());
				else if (!game.LoadClientState(reader))
					std::fprintf(stderr, "load: '%s' has no view of the game\n", file->string().c_str());
			}
			if (pending)
			{
				MatchPlan plan = std::move(*pending);
				pending.reset();
				// A new campaign or challenge mission: its intro movie first, if it has one that plays.
				const generalszh::content::Campaign *campaign = plan.SinglePlayer() ? campaigns.Find(plan.campaign) : nullptr;
				const generalszh::content::Mission *mission = campaign != nullptr ? campaign->FindMission(plan.mission) : nullptr;
				if (mission != nullptr && movie.Play(videos, options->install, "English", mission->introMovie))
					briefing = std::move(plan);
				else
					launch(plan, {});
			}
			// Intro::update: its next stage once no movie plays and its wait is over; the movie it starts.
			if (!generalszh::shell::IntroDone(intro))
			{
				const auto step = generalszh::shell::UpdateIntro(intro, introNow(), movie.Playing());
				if (!step.movie.empty())
					std::printf("intro: %s%s\n", step.movie.c_str(),
						movie.Play(videos, options->install, "English", step.movie) ? "" : " (not found)");
				if (step.done)
					std::printf("intro: done\n");
			}
			// A movie the match's scripts asked for (MOVIE_PLAY_FULLSCREEN / MOVIE_PLAY_RADAR): the latest one plays.
			if (const auto asked = game.TakeMovies(); !asked.empty())
			{
				if (!movie.Play(videos, options->install, "English", asked.back()))
					std::fprintf(stderr, "movie: '%s' could not be played\n", asked.back().c_str());
			}
			// The movie on by the frame's real time; over (or skipped), the mission waiting for it starts.
			movie.Update(options->tickPerFrame ? 1.0 / 30.0 : std::chrono::duration<double>(now - movieClock).count());
			movieClock = now;
			if (std::exchange(skipMovie, false) || !movie.Playing())
			{
				movie.Stop();
				if (briefing)
				{
					const MatchPlan plan = std::move(*briefing);
					briefing.reset();
					launch(plan, {});
				}
			}

			frameCapture.captureNextFrame = !options->screenshot.empty() &&
				(options->matchFrames != 0 ? inGameFrames == options->matchFrames : frame + 1 == options->frames);
			if (!Graphics::Graphics_Begin_Frame())
				continue;
			Graphics::RenderBeginOptions begin;
			begin.clear = true;
			begin.clear_value = {0.20f, 0.30f, 0.45f, 1.0f};
			Graphics::Get_Render_Services().Begin_Render(begin);
			const float frameSeconds = options->tickPerFrame ? 1.0f / 30.0f : std::chrono::duration<float>(now - frameStart).count();
			frameStart = now;
			const float alpha = options->tickPerFrame ? 1.0f : static_cast<float>(accumulator.count()) / static_cast<float>(tickLength.count());
			game.Update(frameSeconds, alpha);
			frontEnd.Step(frameSeconds);
			game.ApplyView(camera);
			const bool introShowing = !generalszh::shell::IntroDone(intro);
			if (frontEnd.ShellMapShown() && !introShowing)
				scene->Draw(game, camera, timeOfDay, (static_cast<float>(clock.Current().Tick()) + alpha) / static_cast<float>(step.TicksPerSecond()));
			Graphics::Get_Renderer2D().Begin(options->width, options->height);
			// During the intro only its movies show, over black.
			if (introShowing)
				Graphics::Get_Renderer2D().Add_Rect({0.0f, 0.0f, static_cast<float>(options->width), static_cast<float>(options->height)}, {0.0f, 0.0f, 0.0f, 1.0f});
			else
				frontEnd.Draw(Graphics::Get_Renderer2D());
			frameCapture.fade = 0;
			if (frontEnd.InGame())
			{
				const auto overlay = game.Overlay();
				frameCapture.fade = overlay.fade;
				frameCapture.fadeValue = overlay.fadeValue;
				// W3DDisplay::enableLetterBox: a change starts the bars' fade (a second, on real time) from now.
				const auto &settings = game.Settings();
				if (settings.letterboxChanges != letterboxChanges)
				{
					letterboxChanges = settings.letterboxChanges;
					letterboxStart = now;
				}
				DrawInGameOverlay(Graphics::Get_Renderer2D(), overlay);
				worldAnimations.Draw(overlay.images, Graphics::Get_Renderer2D());
				floatingTexts.Draw(overlay.texts, Graphics::Get_Renderer2D());
				messageView.Draw(overlay.messages, overlay.messageAt, Graphics::Get_Renderer2D());
				militaryCaption.Draw(overlay.caption, static_cast<float>(options->width), static_cast<float>(options->height), Graphics::Get_Renderer2D());
				namedTimers.Draw(overlay.namedTimers, overlay.namedTimerAt, static_cast<float>(options->width), static_cast<float>(options->height), Graphics::Get_Renderer2D());
				superweaponTimers.Draw(overlay.superweapons, overlay.superweaponAt, static_cast<float>(options->width), static_cast<float>(options->height), Graphics::Get_Renderer2D());
				if (controlBarLoaded)
				{
					controlBar.Update(game, frameSeconds);
					for (std::size_t press = 0; press < options->barPresses.size(); ++press)
						if (inGameFrames == 90 + 20 * press)
							controlBar.Press(options->barPresses[press]);
					// DOZER_CONSTRUCT: the structure's ghost follows the mouse until placed or given up.
					if (const std::string placing = controlBar.TakePlacement(); !placing.empty())
						game.BeginPlacement(controlBar.Selected(), placing);
					// GUI_COMMAND_SPECIAL_POWER needing a target: the world waits for it (MOUSEMODE_GUI_COMMAND).
					if (const std::string button = controlBar.TakeTargeting(); !button.empty())
						game.BeginTargeting(controlBar.Selected(), button);
					if (const auto shortcut = controlBar.TakeShortcutTargeting())
						game.BeginTargeting(shortcut->source, shortcut->button, shortcut->type);
					// doLetterBoxMode: HideControlBar(TRUE) while the bars are on.
					if (!settings.letterbox)
						controlBar.Draw(Graphics::Get_Renderer2D());
				}
				matchEnd.Draw(Graphics::Get_Renderer2D());
				// W3DDisplay::renderLetterBox, over everything: black bars leaving a 16:9 picture, fading in over a second;
				// once off, the top one fading out (the bottom one goes at once, as the original draws it).
				const float elapsed = std::chrono::duration<float, std::milli>(now - letterboxStart).count() / 1000.0f;
				if (settings.letterbox)
					letterboxLevel = letterboxLevel != 1.0f ? std::min(elapsed, 1.0f) : 1.0f;
				else if (letterboxLevel != 0.0f)
					letterboxLevel = std::max(1.0f - elapsed, 0.0f);
				if (letterboxLevel > 0.0f)
				{
					const float width = static_cast<float>(options->width), height = static_cast<float>(options->height);
					const float bar = (height - 9.0f / 16.0f * width) * 0.5f;
					const Graphics::Color2D black{0.0f, 0.0f, 0.0f, static_cast<float>(static_cast<int>(letterboxLevel * 255.0f)) / 255.0f};
					Graphics::Get_Renderer2D().Add_Rect({0.0f, 0.0f, width, bar}, black);
					if (settings.letterbox)
						Graphics::Get_Renderer2D().Add_Rect({0.0f, height - bar, width, height}, black);
				}
			}
			// The display's movie over everything else while it plays.
			if (movie.Playing())
				movie.Draw(Graphics::Get_Renderer2D(), static_cast<float>(options->width), static_cast<float>(options->height));
			if (!(Graphics::Graphics_Execute_Queued_Draws() && Graphics::Graphics_End_Frame() && Graphics::Graphics_Present()))
				Graphics::Graphics_Abort_Frame();
		}
		std::printf("audio: %s\n", game.AudioSummary().c_str());
		if (!options->movie.empty())
			std::printf("movie: frame %llu of %llu, %llu sound frames queued\n", static_cast<unsigned long long>(movie.Frame()),
				static_cast<unsigned long long>(movie.FrameCount()), static_cast<unsigned long long>(movie.SoundQueued()));
		std::printf("effects: %s\n", game.EffectsSummary().c_str());
		std::printf("objects: %zu instances, %s\n%s", game.Objects().size(), scene->Summary().c_str(), scene->Failures().c_str());
		if (!options->screenshot.empty())
		{
			std::printf(frameCapture.captured ? "screenshot written to %s\n" : "screenshot FAILED (%s)\n", options->screenshot.string().c_str());
		}
		captured = frameCapture.captured;
	}
	Assets::Shutdown_Asset_Runtime();
	Graphics::Get_Render_Services().Shutdown();
	// The scene renderers free their GPU resources while the device still exists
	// (left to their static destructors, they freed them on a destroyed device at exit).
	Graphics::Shutdown_Scene_Renderers();
	Graphics::Graphics_Shutdown_Shared_Frame();
	g_frameCapture = nullptr;
	return captured || options->screenshot.empty() ? 0 : 1;
}

namespace generalszh::host
{
void InstallCrashReport(); // crash_report.cpp
}

int main(int argc, char **argv)
{
	generalszh::host::InstallCrashReport();
	// Anything thrown on the way (a bad install, a broken system schedule) is reported, not a silent abort.
	try
	{
		return Run(argc, argv);
	}
	catch (const std::exception &error)
	{
		std::fprintf(stderr, "fatal: %s\n", error.what());
		return 3;
	}
}
