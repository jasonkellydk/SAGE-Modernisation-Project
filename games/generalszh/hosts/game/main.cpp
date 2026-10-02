// Zero Hour on the modern engine: window, fixed-step loop and renderer.
// Usage: generalszh [--install <dir>] [--map <path>] [--frames <n>] [--match-frames <n>] [--seed <n>] [--screenshot <file.png>] [--tick-per-frame] [--speed <x>]
// The install directory defaults to GENERALSZH_INSTALL_DIR.

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
import games.generalszh.hosts.game.match_stage;
import games.generalszh.hosts.game.match_flow;
import games.generalszh.hosts.game.shell_requests;
import games.generalszh.hosts.game.window_events;
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
import games.generalszh.hosts.game.popup_message_layer;
import games.generalszh.hosts.game.cinematic_text_view;
import games.generalszh.hosts.game.superweapon_timers_view;
import games.generalszh.content.global.in_game_ui;
import games.generalszh.content.global.mouse;
import Graphics.Cursors.Ani;
import games.generalszh.content.global.campaigns;
import games.generalszh.content.global.videos;
import games.generalszh.hosts.game.movie_player;
import games.generalszh.hosts.game.load_screen;
import games.generalszh.hosts.game.window_scale;
import games.generalszh.hosts.game.host_options;
import games.generalszh.hosts.game.frame_draws;
import games.generalszh.hosts.game.cursor_set;
import games.generalszh.hosts.game.overlay_views;
import games.generalszh.hosts.game.match_files;
import games.generalszh.hosts.game.match_setup;
import games.generalszh.hosts.game.match_results;
import games.generalszh.hosts.game.pointer_input;
import games.generalszh.shell.intro.intro_sequence;
import games.generalszh.hud.match_record;
import games.generalszh.shell.score.battle_honors;
import games.generalszh.session.setup.match_plan;
import games.generalszh.session.setup.replay_body;
import games.generalszh.shell.replay.replay_header;
import games.generalszh.shell.replay.replay_menu_view_model;
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

int Run(int argc, char **argv)
{
	using generalszh::host::DefaultUserData;
	// Unbuffered, so progress survives a crash.
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	auto options = generalszh::host::ParseOptions(argc, argv);
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
	generalszh::host::MatchStage stage;
	generalszh::host::SetStageLevel(stage, std::move(level->level));

	// Platform window and renderer.
	engine::platform::sdl3::SDL3PlatformAdapter platform;
	engine::platform::WindowConfig windowConfig;
	windowConfig.title = "Command & Conquer Generals: Zero Hour";
	windowConfig.size = {static_cast<int>(options->width), static_cast<int>(options->height)};
	// Resizable at will: the game is drawn at its display resolution and stretched over the window (WindowScale).
	windowConfig.resizable = true;
	auto window = platform.windows().create(windowConfig);
	const auto handle = window ? window->native_handle() : std::nullopt;
	if (!handle || handle->window == nullptr)
	{
		std::fprintf(stderr, "window: %s\n", platform.last_error());
		return 1;
	}
	// Typed characters arrive as text input (the front end's entries take them).
	platform.text_input().start(window->id());
	generalszh::host::FrameCapture frameCapture;
	frameCapture.shaders = GENERALSZH_SHADER_DIRECTORY;
	frameCapture.captureFile = options->screenshot;
	Graphics::FrameDeviceOptions device;
	device.window = handle->window;
	device.width = options->width;
	device.height = options->height;
	const std::string shaderDirectory = frameCapture.shaders.string();
	device.shader_directory = shaderDirectory.c_str();
	if (!Graphics::Initialize_Frame_Device(device) ||
		!generalszh::host::RegisterFrameDraws(frameCapture))
	{
		std::fprintf(stderr, "graphics device could not be created\n");
		return 1;
	}
	// WinMain: the load screen (Install_Final.bmp) up while everything loads.
	generalszh::host::PresentLoadScreen(options->install);
	// Everything holding GPU or asset resources lives in this scope, so it is
	// released before the asset runtime and the frame device shut down.
	bool captured = false;
	{
		// Content loading (INI sets through the Zero Hour grammar).
		engine::config::Diagnostics grammarDiagnostics;
		generalszh::content::ContentLoader loader(files, generalszh::content::ZeroHourIniGrammar(grammarDiagnostics));

		// The mouse cursors (Mouse.ini's), the arrow first.
		generalszh::host::CursorSet cursors;
		cursors.Load(generalszh::content::BindMouse(loader.Load({"Data/INI/Mouse"})), files);
		cursors.Show({static_cast<std::uint8_t>(generalszh::content::MouseCursorKind::Arrow), 0});

		// The world's renderers: terrain (textured base + 3-way overlay), objects, water (Water.ini, map.ini overrides), particles.
		if (!generalszh::host::LoadStageScene(stage, files, loader, options->map))
		{
			std::fprintf(stderr, "terrain: %s\n", stage.terrainError.c_str());
			return 1;
		}
		if (!stage.waterError.empty())
			std::fprintf(stderr, "water: %s\n", stage.waterError.c_str());

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
		game.Load(loader, files, *stage.level, stage.Height(), stage.mesh->PlayableSize(),
			static_cast<float>(options->width) / static_cast<float>(options->height), 0x5EED);
		game.SetGameSpeed(options->speed);
		if (options->look)
			game.PinCamera(options->look->first, options->look->second);
		std::printf("game: %zu entities, simulation on %zu worker threads at %.1f ticks/s\n", game.EntityCount(), game.WorkerCount(),
			game.TicksPerSecond());
		std::printf("scripts: %s\n", game.UnportedSummary().c_str());
		std::printf("audio: %.*s\n", static_cast<int>(game.AudioOutputName().size()), game.AudioOutputName().data());
		game.Update(0.0f, 1.0f);
		stage.scene->Preload(game);
		std::printf("objects: %s\n", stage.scene->Summary(game).c_str());
		Graphics::CameraState camera;

		// The front end over the shell map (main menu, options, credits).
		// Floating texts over the world ("+$300" where a truck delivered): GUI:AddCash, in the display string font.
		game.SetAddCashText(generalszh::host::Localized(strings, "GUI:AddCash"));
		game.SetLoseCashText(generalszh::host::Localized(strings, "GUI:LoseCash"));
		game.SetLabels([&strings](std::string_view label) { return generalszh::host::Localized(strings, label); });
		const generalszh::content::LanguageFonts languageFonts = generalszh::content::ReadLanguageFonts(loader.Load({"Data/English/Language"}));
		// The in-game overlay's views (its fonts and images).
		generalszh::host::OverlayViews overlayViews;
		generalszh::host::LoadOverlayViews(overlayViews, files, strings, languageFonts,
			generalszh::content::BindInGameUi(loader.Load({"Data/INI/Default/InGameUI", "Data/INI/InGameUI"})), options->width, options->height);
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
		const bool introAllowed = !automated || options->intro;
		generalszh::shell::IntroSequence intro = generalszh::shell::MakeIntro(introAllowed && options->gameDataIntro, introAllowed);
		const auto introStart = std::chrono::steady_clock::now();
		// timeGetTime: milliseconds on a clock that is never 0 (a wait's end compares with it).
		const auto introNow = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - introStart).count()) + 1000; };

		auto movieClock = std::chrono::steady_clock::now();
		using generalszh::session::setup::MatchKind;
		using generalszh::session::setup::MatchPlan;
		// The matches playing and asked for (MatchFlow), and the host's parts they are made with.
		generalszh::host::MatchFlow flow;
		// INGAME_POPUP_MESSAGE's window (InGameUI::popupMessage), the message it shows and whether it paused the game.
		generalszh::host::PopupMessageLayer popup;
		std::uint32_t popupSerial = 0;
		bool popupPaused = false;
		// DISPLAY_CINEMATIC_TEXT over the letterbox; REFRESH_RADAR's requests seen.
		generalszh::host::CinematicTextView cinematicText;
		std::uint32_t radarRefreshes = 0;

		// The load screens (GameLogic::getLoadScreen) and the factions the multiplayer one shows.
		generalszh::host::LoadScreenLayer loadScreen;
		if (!options->loadScreenShots.empty())
			loadScreen.CaptureTo(frameCapture, options->loadScreenShots);
		const std::vector<generalszh::shell::LoadScreenFaction> loadScreenFactions = generalszh::host::LoadScreenFactions(loader, game.PlayerTemplates(), strings);
		generalszh::host::HostParts host{*options, files, loader, strings, campaigns, languageFonts, game, frontEnd, controlBar, controlBarLoaded, loadScreen,
			loadScreenFactions};
		// GameClient::update: once the intro is done the shell map loads (Shell::showShellMap), behind the title screen;
		// not when the game starts straight into a saved game.
		bool shellMapShown = false;
		const auto launch = [&](const MatchPlan &plan, std::span<const std::byte> checkpoint, generalszh::host::ReplayStart *replay = nullptr) {
			return generalszh::host::LaunchMatch(host, stage, flow, plan, checkpoint, replay);
		};
		const std::filesystem::path userData = options->userData.empty() ? DefaultUserData() : options->userData;
		const auto updateStats = [&](auto &&change) { generalszh::host::UpdateSkirmishStats(userData, change); };
		generalszh::host::BindGameMenus(host, flow);

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
		generalszh::host::WindowScale windowScale{static_cast<float>(options->width), static_cast<float>(options->height),
			static_cast<float>(options->width), static_cast<float>(options->height)};
		auto letterboxStart = previous;
		std::uint32_t letterboxChanges = 0;
		for (std::uint64_t frame = 0; (options->frames == 0 || frame < options->frames) && (options->matchFrames == 0 || inGameFrames < options->matchFrames); ++frame)
		{
			generalszh::host::WindowEvents window;
			engine::platform::PlatformEvent event;
			while (platform.events().poll(event))
			{
				// The window stretched: its size noted, the pointer taken back to the display's pixels.
				if (event.type == engine::platform::EventType::window_resized)
					windowScale.Resized(event.size);
				window.Note(event);
				windowScale.ToDisplay(event);
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
					if (!generalszh::host::PointMouse(pointer, event) && (event.type == EventType::key_down || event.type == EventType::key_up))
					{
						keyModifiers = event.modifiers;
						using engine::platform::KeyCode;
						// GeneralsExpPointsInput: Esc closes the General's Powers screen; else Esc (key down) is the OPTIONS
						// meta event (CommandMap.ini): ToggleQuitMenu.
						// isMovieAbortRequested: Esc skips a movie playing (and does nothing else then).
						// InGamePopupMessageInput: the popup has the keyboard; Enter or Esc let go press its OK.
						const bool popupKey = popup.Showing() && !frontEnd.GameMenuShown() &&
							(event.key == KeyCode::escape || event.key == KeyCode::enter || event.key == KeyCode::keypad_enter);
						if (popupKey)
						{
							if (event.type == EventType::key_up)
								popup.Key();
						}
						else if (event.type == EventType::key_down && event.key == KeyCode::escape && movie.Playing())
							skipMovie = true;
						else if (event.type == EventType::key_down && event.key == KeyCode::escape && !(controlBarLoaded && !frontEnd.GameMenuShown() && controlBar.Escape()))
							frontEnd.ToggleQuitMenu();
						// CommandMap.ini's PLACE_BEACON (Ctrl+B down) and DELETE_BEACON (Del down), in a match.
						if (event.type == EventType::key_down && frontEnd.InGame() && !frontEnd.GameMenuShown())
						{
							if (event.key == KeyCode::b && (event.modifiers & engine::platform::modifier_control) != 0 &&
								(event.modifiers & (engine::platform::modifier_alt | engine::platform::modifier_shift)) == 0)
								game.PlaceBeaconKey();
							else if (event.key == KeyCode::del && event.modifiers == 0)
								game.RemoveBeaconKey();
						}
						generalszh::host::HoldArrows(pointer, event);
					}
				}
			}
			if (window.closeRequested || frontEnd.QuitRequested())
				break;
			if (window.resizedTo)
				Graphics::Resize_Frame_Device(static_cast<std::uint32_t>(window.resizedTo->width), static_cast<std::uint32_t>(window.resizedTo->height), false);
			if (!frontEnd.InGame() && inGameFrames != 0)
				++inGameFrames;
			if (frontEnd.InGame())
			{
				++inGameFrames;
				if (options->saveAt != 0 && inGameFrames == options->saveAt)
				{
					if (const auto saved = generalszh::host::SaveMatch(host, flow, std::nullopt, u"Check"))
						std::printf("save: %s with %zu entities\n", saved->string().c_str(), game.EntityCount());
					else
						std::fprintf(stderr, "save: nothing saved\n");
				}
				// The checks' scripted clicks (--click) and drag (--drag).
				generalszh::host::ScriptPointer(pointer, options->clicks, options->drag, inGameFrames);
				pointer.shift = (keyModifiers & engine::platform::modifier_shift) != 0;
				pointer.ctrl = (keyModifiers & engine::platform::modifier_control) != 0;
				pointer.alt = (keyModifiers & engine::platform::modifier_alt) != 0;
				pointer.timeMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
				game.SetViewport(static_cast<float>(options->width), static_cast<float>(options->height));
				// The control bar takes the mouse over it (the world then takes no clicks).
				// The popup message first (modal while it pauses the game).
				if (popup.Showing() && popup.Point(pointer.x, pointer.y, pointer.pressed, pointer.released, pointer.timeMs))
					pointer.overInterface = true;
				else
					pointer.overInterface = controlBarLoaded && controlBar.Point(pointer.x, pointer.y, pointer.pressed, pointer.released, pointer.timeMs);
				// The debug cheat MSG_META_DEMO_KILL_ALL_ENEMIES, scripted (--kill-enemies-at).
				if (options->killEnemiesAt != 0 && inGameFrames == options->killEnemiesAt)
					game.Submit(generalszh::commands::KillAllEnemies{});
				// doDisableInput: once the game is won or lost the player gives no more orders.
				game.Point(game.Settings().inputDisabled ? generalszh::host::LookOnlyPointer(pointer) : pointer);
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
			if (!frontEnd.ShellMapShown() || (flow.paused && frontEnd.InGame()) || !generalszh::shell::IntroDone(intro))
				accumulator = {};
			while (accumulator >= tickLength)
			{
				clock.Advance();
				game.Tick();
				accumulator -= tickLength;
				if (frontEnd.InGame() && shownEnd != generalszh::host::ClientSettings::MatchEnd::None)
					++endTicks;
			}
			// RecorderClass::handleCRCMessage: a replay whose state went another way than recorded says so once and pauses.
			if (flow.replaying && !flow.replayMismatchShown && frontEnd.InGame())
				if (const auto playback = game.PlaybackState(); playback && playback->mismatch)
				{
					flow.replayMismatchShown = true;
					game.ShowMessage(generalszh::host::Localized(strings, "GUI:CRCMismatch"));
					std::fprintf(stderr, "replay: out of sync after tick %llu\n", static_cast<unsigned long long>(*playback->mismatch));
					flow.paused = true;
				}
			// The cursor the match asks for; the arrow in the shell.
			cursors.Show(frontEnd.InGame() ? game.MouseCursor().value_or(std::array<std::uint8_t, 2>{2, 0})
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
				// A replay played to its end leaves too (RecorderClass::stopPlayback: exitGame).
				const bool replayOver = flow.replaying && game.PlaybackState() && game.PlaybackState()->done;
				if (((shownEnd == MatchEnd::Victory || shownEnd == MatchEnd::Defeat) && endTicks >= 120) || (shownEnd == MatchEnd::QuickVictory && endTicks >= 1) ||
					std::exchange(flow.exitRequested, false) || replayOver)
				{
					// GameLogic::exitGame (clearGameData): the score screen over the shell, from the game as it ended. A campaign
					// won goes on to its next mission from there (CampaignManager::gotoNextMission), lost it tries the same one
					// again; a skirmish just shows the scores.
					generalszh::shell::ScoreScreenSetup scores;
					scores.players = game.ScoreBoard();
					if (flow.playing && flow.playing->SinglePlayer())
					{
						scores.kind = generalszh::shell::ScoreScreenKind::SinglePlayer;
						scores.victorious = shownEnd == MatchEnd::Victory || shownEnd == MatchEnd::QuickVictory;
						const generalszh::content::Campaign *campaign = campaigns.Find(flow.playing->campaign);
						const generalszh::content::Mission *next = scores.victorious && campaign != nullptr
							? campaign->NextMission(campaign->FindMission(flow.playing->mission)) : nullptr;
						scores.campaignOver = scores.victorious && next == nullptr;
						// finishSinglePlayerInit: the campaign won to its end is recorded at its difficulty, and its final
						// victory movie plays over the score screen (PlayMovieAndBlock).
						if (scores.campaignOver)
						{
							updateStats([&](engine::config::Preferences &stats) {
								generalszh::shell::RecordCampaignComplete(stats, flow.playing->campaign, flow.playing->difficulty);
							});
							if (campaign != nullptr && !campaign->finalVictoryMovie.empty())
								movie.Play(videos, options->install, "English", campaign->finalVictoryMovie);
						}
						flow.afterScores = *flow.playing;
						// ScoreScreen initSinglePlayer: CampaignManager::setRankPoints(the local player's skill points), carried
						// into the next mission or the retry alike.
						flow.afterScores->rankPoints = game.LocalSkillPoints();
						if (next != nullptr)
						{
							flow.afterScores->mission = next->name;
							// finishSinglePlayerInit: won with a mission to follow, an automatic mission save of it.
							scores.gameSaved = true; // shown whatever the save's result, as the original
							if (const auto saved = generalszh::host::SaveMission(host, *flow.afterScores))
								std::printf("save: %s (mission)\n", saved->string().c_str());
							else
								std::fprintf(stderr, "save: the mission could not be saved\n");
						}
						else if (scores.victorious)
							flow.afterScores.reset();
						for (const auto &player : scores.players)
							if (player.local)
								scores.localScoreScreenImage = player.scoreScreenImage;
					}
					else
					{
						// populatePlayerInfo, SCORESCREEN_SKIRMISH: the local player's record, with the other seats as they were
						// set up (a computer's level; on the local player's team: isSlotLocalAlly), unless a sandbox (every
						// other seat on the local player's team: GameInfo::isSandbox).
						if (flow.playing && flow.playing->kind == MatchKind::Skirmish && !flow.replaying)
							if (const auto local = game.LocalPlayer(); local && game.View() != nullptr)
							{
								const auto result = generalszh::host::ReadSkirmishResult(*game.View(), *local, *flow.playing);
								updateStats([&](engine::config::Preferences &stats) { generalszh::shell::RecordSkirmishGame(stats, result.record, result.sandbox); });
							}
						// grabMultiPlayerInfo: the seats' players (player<slot>), in slot order.
						scores.players = generalszh::host::SeatScorePlayers(flow.playing ? &*flow.playing : nullptr, scores.players);
						flow.afterScores.reset();
					}
					frontEnd.ShowScoreScreen(scores);
					// Back to the shell over the shell map.
					flow.paused = false;
					if (!flow.replaying && flow.playing && (flow.playing->kind == MatchKind::Skirmish || flow.playing->kind == MatchKind::Lan))
						if (const auto *recording = game.Recording())
							generalszh::host::WriteLastReplay(userData, *flow.playing, *recording, flow.matchStartTime, generalszh::host::Localized(strings, "GUI:LastReplay"));
					flow.replaying.reset();
					flow.replayMismatchShown = false;
					flow.playing.reset();
					matchEnd.Close();
					shownEnd = MatchEnd::None;
					localDefeatShown = false;
					generalszh::host::LoadShellMap(host, stage);
					frontEnd.LeaveGame();
				}
			}

			if (!shellMapShown && generalszh::shell::IntroDone(intro))
			{
				shellMapShown = true;
				if (!options->loadFile && !frontEnd.InGame())
					generalszh::host::LoadShellMap(host, stage);
			}
			// What the shell asked for this frame, then the match asked for (a new mission's intro movie first).
			generalszh::host::TakeShellRequests(host, stage, flow);
			generalszh::host::StartPendingMatch(host, stage, flow, movie, videos);
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
			// A mission's movie plays in its load screen, stepping its bar (SinglePlayerLoadScreen::init).
			if (flow.briefing)
				loadScreen.MovieProgress(movie.Frame(), movie.Playing() && !skipMovie);
			if (std::exchange(skipMovie, false) || !movie.Playing())
			{
				movie.Stop();
				if (flow.briefing)
				{
					const MatchPlan plan = std::move(*flow.briefing);
					flow.briefing.reset();
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
			// CAMERA_ENABLE_SLAVE_MODE: the view is its unit's bone this frame (W3DView::getCameraTransform).
			if (const auto slave = game.CameraSlave(); slave && stage.scene)
				if (const auto bone = stage.scene->SlaveBone(game, *slave))
					game.SlaveView(*bone);
			game.ApplyView(camera);
			const bool introShowing = !generalszh::shell::IntroDone(intro);
			if (frontEnd.ShellMapShown() && !introShowing)
				stage.scene->Draw(game, camera, stage.timeOfDay, (static_cast<float>(clock.Current().Tick()) + alpha) / static_cast<float>(step.TicksPerSecond()),
					game.InfantryLightScale(stage.level->lighting.current));
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
				// W3DRadar::refreshTerrain (REFRESH_RADAR): the radar's terrain picture built again from the ground and water now.
				if (settings.radarRefreshes != radarRefreshes)
				{
					radarRefreshes = settings.radarRefreshes;
					if (controlBarLoaded && stage.scene)
						controlBar.SetRadarTerrain(stage.scene->BuildRadarTerrain(*stage.level, game));
				}
				// InGameUI::popupMessage: a new message's window (the game paused while it shows when it asks); dismissed, the
				// game goes on (clearPopupMessageData).
				// (None showing: whatever comes next is new, a new match's first message included.)
				if (game.Popup() == nullptr && !popup.Showing())
					popupSerial = 0;
				if (const auto *message = game.Popup(); message != nullptr && message->serial != popupSerial)
				{
					popupSerial = message->serial;
					std::string popupError;
					if (popup.Show(files, strings, *message, options->width, options->height,
							generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment)), popupError))
					{
						if (message->pause)
						{
							popupPaused = true;
							flow.paused = true;
						}
					}
					else
						std::fprintf(stderr, "popup message: %s\n", popupError.c_str());
				}
				if (popup.TakeClosed() || (popup.Showing() && game.Popup() == nullptr))
				{
					popup.Close();
					game.ClosePopup();
					if (std::exchange(popupPaused, false))
						flow.paused = false;
				}
				generalszh::host::DrawOverlay(overlayViews, overlay, static_cast<float>(options->width), static_cast<float>(options->height), Graphics::Get_Renderer2D());
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
				popup.Draw(Graphics::Get_Renderer2D());
				matchEnd.Draw(Graphics::Get_Renderer2D());
				// W3DDisplay::renderLetterBox, over everything, fading over a second of real time from its last change.
				letterboxLevel = generalszh::host::StepLetterbox(letterboxLevel, settings.letterbox,
					std::chrono::duration<float, std::milli>(now - letterboxStart).count() / 1000.0f);
				generalszh::host::DrawLetterbox(Graphics::Get_Renderer2D(), letterboxLevel, settings.letterbox, static_cast<float>(options->width),
					static_cast<float>(options->height));
				// W3DDisplay::draw: the cinematic text over the black.
				if (const auto *cinematic = game.CinematicText())
					cinematicText.Draw(*cinematic, generalszh::host::FontScale(options->width, options->height, Engine::Math::ToFloat(languageFonts.resolutionAdjustment)),
						static_cast<std::int32_t>(options->width), static_cast<std::int32_t>(options->height), Graphics::Get_Renderer2D());
			}
			// The display's movie over everything else while it plays.
			if (flow.briefing && loadScreen.Shown())
				loadScreen.Draw(Graphics::Get_Renderer2D());
			else if (movie.Playing())
				movie.Draw(Graphics::Get_Renderer2D(), static_cast<float>(options->width), static_cast<float>(options->height));
			if (!(Graphics::Graphics_Execute_Queued_Draws() && Graphics::Graphics_End_Frame() && Graphics::Graphics_Present()))
				Graphics::Graphics_Abort_Frame();
		}
		std::printf("audio: %s\n", game.AudioSummary().c_str());
		if (!options->movie.empty())
			std::printf("movie: frame %llu of %llu, %llu sound frames queued\n", static_cast<unsigned long long>(movie.Frame()),
				static_cast<unsigned long long>(movie.FrameCount()), static_cast<unsigned long long>(movie.SoundQueued()));
		std::printf("effects: %s\n", game.EffectsSummary().c_str());
		std::printf("objects: %zu instances, %s\n%s", game.Objects().size(), stage.scene->Summary(game).c_str(), stage.scene->Failures(game).c_str());
		if (!options->screenshot.empty())
		{
			std::printf(frameCapture.captured ? "screenshot written to %s\n" : "screenshot FAILED (%s)\n", options->screenshot.string().c_str());
		}
		captured = frameCapture.captured;
		// The world's renderers let go of their GPU resources inside this scope.
		stage.scene.reset();
	}
	Assets::Shutdown_Asset_Runtime();
	Graphics::Get_Render_Services().Shutdown();
	// The scene renderers free their GPU resources while the device still exists
	// (left to their static destructors, they freed them on a destroyed device at exit).
	Graphics::Shutdown_Scene_Renderers();
	Graphics::Graphics_Shutdown_Shared_Frame();
	generalszh::host::ReleaseFrameDraws();
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
