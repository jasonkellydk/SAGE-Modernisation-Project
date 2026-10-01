module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module games.generalszh.hosts.game.front_end;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Input;
import Engine.UI.WND.Bindings;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.hosts.game.credits_view;
import games.generalszh.shell.main_menu.main_menu_view;
import games.generalszh.shell.options.options_view;
import games.generalszh.shell.credits.credits_view_model;
import games.generalszh.shell.replay.replay_menu_view;
import games.generalszh.shell.save_load.save_load_view;
import games.generalszh.shell.skirmish.skirmish_view;
import games.generalszh.shell.lan.lan_view;
import games.generalszh.shell.challenge.challenge_view;
import games.generalszh.content.global.challenge_generals;
import games.generalszh.hosts.game.setup_catalog_source;
import games.generalszh.hosts.game.window_transitions_source;
import Engine.UI.WND.Transitions;
import games.generalszh.shell.dialog.message_box_view;
import games.generalszh.shell.quit_menu.quit_menu_view;
import games.generalszh.shell.score.score_screen_view;
import engine.config.adapters.preferences.preferences_file;
import engine.net.transport.udp_socket;
import Engine.Core.Math.FixedPresentation;

namespace generalszh::host
{
namespace
{
std::string ReadFileText(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return file ? std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()) : std::string{};
}

void ReportMissing(const char *screen, const Engine::UI::WND::WNDBindings &bindings)
{
	for (const std::string &missing : bindings.MissingWindows())
		std::fprintf(stderr, "%s: layout has no window '%s'\n", screen, missing.c_str());
}
}

struct FrontEnd::State
{
	GameClient *game{nullptr};
	std::optional<session::setup::GameSetup> gameStart; // Start pressed, not yet taken by the host
	std::optional<FrontEnd::LanStart> lanStart;         // a LAN game started, not yet taken by the host
	std::optional<FrontEnd::CampaignStart> campaignStart; // a campaign chosen, not yet taken by the host
	std::optional<std::filesystem::path> loadRequest;       // a saved game chosen, not yet taken by the host
	std::optional<std::filesystem::path> replayRequest;     // a replay chosen, not yet taken by the host
	std::filesystem::path saveFolder;
	bool inGame{false};
	const engine::localization::StringTable *strings{nullptr};
	Setup setup;
	shell::ShellModel model;
	// The main menu.
	ShellMenu mainMenu;
	std::optional<shell::MainMenuViewModel> mainMenuViewModel;
	std::optional<Engine::UI::WND::WNDBindings> mainMenuBindings;
	// The options (Options.ini) and their menu over the others.
	std::filesystem::path optionsFile;
	engine::config::Preferences optionsPreferences;
	shell::OptionDefaults optionDefaults;
	shell::UserOptions userOptions;
	ShellMenu optionsMenu;
	std::optional<shell::OptionsViewModel> optionsViewModel;
	std::optional<Engine::UI::WND::WNDBindings> optionsBindings;
	// The credits.
	ShellMenu creditsMenu;
	CreditsView creditsView;
	shell::CreditsSettings creditsSettings;
	std::optional<shell::CreditsViewModel> credits;
	float creditFrames{0.0f};
	// The skirmish setup (Skirmish.ini) and its map list over it.
	ShellMenu skirmishMenu, mapSelectMenu;
	shell::SetupCatalog setupCatalog;
	engine::config::Preferences skirmishPreferences;
	std::optional<shell::SkirmishViewModel> skirmishViewModel;
	std::optional<Engine::UI::WND::WNDBindings> skirmishBindings, mapSelectBindings;
	std::optional<shell::SkirmishView> skirmishView;
	// The LAN screens (Network.ini) and the lobby they share, on a UDP socket while they show.
	ShellMenu lanLobbyMenu, gameInfoMenu, directConnectMenu, lanOptionsMenu, lanMapMenu;
	engine::config::Preferences lanPreferences;
	std::unique_ptr<engine::net::UdpSocket> lanSocket;
	std::unique_ptr<generalszh::network::lan::LanLobby> lanLobby;
	std::optional<shell::LanLobbyViewModel> lanLobbyViewModel;
	std::optional<shell::DirectConnectViewModel> directConnectViewModel;
	std::optional<shell::LanGameOptionsViewModel> lanOptionsViewModel;
	std::optional<Engine::UI::WND::WNDBindings> lanLobbyBindings, gameInfoBindings, directConnectBindings, lanOptionsBindings, lanMapBindings;
	std::optional<shell::LanLobbyView> lanLobbyView;
	std::optional<shell::GameInfoView> gameInfoView;
	std::optional<shell::LanGameOptionsView> lanOptionsView;
	generalszh::network::lan::LobbyServices lobbyServices;
	std::uint32_t lanAddress{0};
	// The load screen, over the player's Save folder.
	ShellMenu loadMenu;
	std::optional<shell::SaveLoadViewModel> loadViewModel;
	std::optional<Engine::UI::WND::WNDBindings> loadBindings;
	// The replay menu, over the player's Replays folder.
	ShellMenu replayMenu;
	std::optional<shell::ReplayMenuViewModel> replayViewModel;
	std::optional<Engine::UI::WND::WNDBindings> replayBindings;
	// The Generals' Challenge menu (ChallengeMode.ini's generals), its 30 Hz updates on real time.
	ShellMenu challengeMenu;
	std::optional<shell::ChallengeViewModel> challengeViewModel;
	std::optional<Engine::UI::WND::WNDBindings> challengeBindings;
	float challengeFrames{0.0f};
	// Message boxes, over everything.
	ShellMenu messageBox;
	shell::MessageBoxViewModel messages;
	std::optional<Engine::UI::WND::WNDBindings> messageBindings;
	// The menus over a game: QuitMenu.wnd (QuitNoSave.wnd in a multiplayer game) and PopupSaveLoad.wnd.
	ShellMenu quitMenu, quitNoSaveMenu, popupSaveMenu;
	// The score screen (ScoreScreen.wnd).
	ShellMenu scoreMenu;
	std::optional<shell::ScoreScreenViewModel> scoreViewModel;
	std::optional<Engine::UI::WND::WNDBindings> scoreBindings;
	std::optional<shell::QuitMenuViewModel> quitViewModel;
	std::optional<shell::SaveLoadViewModel> popupSaveViewModel;
	std::optional<Engine::UI::WND::WNDBindings> quitBindings, quitNoSaveBindings, popupSaveBindings;
	bool popupSaveOpen{false};
	FrontEnd::GameMenus gameMenus;
	shell::SaveFiles saveFiles; // the Save folder (the load screen's and the save popup's)
	shell::SaveLoadTexts saveTexts;
	// The pointer over the layout showing.
	Engine::UI::WND::WNDPointer pointer;
	std::pair<float, float> lastPointer{0.0f, 0.0f}; // wheel events carry no position
	bool look{true};
	engine::gui::mvvm::SubscriptionId screenSubscription{0}, savedSubscription{0}, replaySubscription{0}, loadSubscription{0}, skirmishSubscription{0}, lanSubscription{0}, optionsOpenSubscription{0}, challengeSubscription{0};
	std::pair<ShellMenu *, Engine::UI::WND::WNDBindings *> wasActive{nullptr, nullptr};
	std::size_t pressed{0};     // scripted clicks done
	std::size_t gamePressed{0}; // scripted clicks on the menus over a game done

	void ApplyOptions()
	{
		game->SetUserVolumes(userOptions.musicVolume, userOptions.sound2DVolume, userOptions.sound3DVolume, userOptions.speechVolume);
		// GameLODManager::init: the chosen level, or for Custom the player's own options (OptionPreferences).
		presentation::CustomDetail custom;
		custom.shadowVolumes = userOptions.shadowVolumes;
		custom.shadowDecals = userOptions.shadowDecals;
		custom.cloudShadows = userOptions.cloudShadows;
		custom.lightMap = userOptions.groundLighting;
		custom.softWaterEdge = userOptions.smoothWater;
		custom.extraAnimations = userOptions.extraAnimations;
		custom.dynamicLod = userOptions.dynamicLod;
		custom.heatEffects = userOptions.heatEffects;
		custom.trees = userOptions.trees;
		custom.buildingOcclusion = userOptions.buildingOcclusion;
		custom.maxParticleCount = userOptions.maxParticleCount;
		custom.textureReduction = userOptions.textureReduction;
		game->SetDetail(std::clamp(userOptions.staticLod, 0, presentation::detail_level::Custom), custom);
		game->SetRetaliation(userOptions.retaliation);
	}

	// The layout and bindings the pointer works on: a message box while open, else the options
	// while open, else the screen's.
	std::pair<ShellMenu *, Engine::UI::WND::WNDBindings *> Active()
	{
		if (messages.open.Get())
			return {&messageBox, &*messageBindings};
		if (model.optionsOpen.Get())
			return {&optionsMenu, &*optionsBindings};
		if (inGame)
		{
			if (popupSaveOpen && popupSaveBindings)
				return {&popupSaveMenu, &*popupSaveBindings};
			if (quitViewModel && quitViewModel->open.Get())
				return quitViewModel->saveLoadShown.Get() ? std::pair{&quitMenu, &*quitBindings} : std::pair{&quitNoSaveMenu, &*quitNoSaveBindings};
			return {nullptr, nullptr};
		}
		switch (Screen())
		{
		case shell::Screen::MainMenu: return {&mainMenu, &*mainMenuBindings};
		case shell::Screen::ReplayMenu: return {&replayMenu, &*replayBindings};
		case shell::Screen::LoadGame: return {&loadMenu, &*loadBindings};
		case shell::Screen::ScoreScreen: return {&scoreMenu, &*scoreBindings};
		case shell::Screen::ChallengeMenu: return {&challengeMenu, &*challengeBindings};
		case shell::Screen::NetworkLobby: return {&lanLobbyMenu, &*lanLobbyBindings};
		case shell::Screen::DirectConnect: return {&directConnectMenu, &*directConnectBindings};
		case shell::Screen::LanGameOptions:
			if (lanOptionsViewModel->mapSelectOpen.Get())
				return {&lanMapMenu, &*lanMapBindings};
			return {&lanOptionsMenu, &*lanOptionsBindings};
		case shell::Screen::Skirmish:
			if (skirmishViewModel->mapSelectOpen.Get())
				return {&mapSelectMenu, &*mapSelectBindings};
			return {&skirmishMenu, &*skirmishBindings};
		default: return {nullptr, nullptr};
		}
	}
	Engine::UI::WND::WNDDocument *Layout() { return Active().first != nullptr ? &Active().first->Document() : nullptr; }
	Engine::UI::WND::WNDBindings *Bindings() { return Active().second; }
	// GBM_MOUSE_ENTERING / GBM_MOUSE_LEAVING on the main menu: the scripts hear some buttons highlighted,
	// and the side buttons grow their faction art (not once a side is picked, not while panels move).
	void Hovered(const std::string &entered, const std::string &left)
	{
		if (shownScreen != shell::Screen::MainMenu)
			return;
		struct Hover
		{
			const char *window, *hook, *faction;
		};
		static constexpr Hover Hovers[] = {{"MainMenu.wnd:ButtonOnline", "ShellMainMenuOnline", nullptr},
			{"MainMenu.wnd:ButtonNetwork", "ShellMainMenuNetwork", nullptr}, {"MainMenu.wnd:ButtonOptions", "ShellMainMenuOptions", nullptr},
			{"MainMenu.wnd:ButtonExit", "ShellMainMenuExit", nullptr}, {"MainMenu.wnd:ButtonChallenge", nullptr, "MainMenuFactionTraining"},
			{"MainMenu.wnd:ButtonSkirmish", nullptr, "MainMenuFactionSkirmish"}, {"MainMenu.wnd:ButtonUSA", nullptr, "MainMenuFactionUS"},
			{"MainMenu.wnd:ButtonGLA", nullptr, "MainMenuFactionGLA"}, {"MainMenu.wnd:ButtonChina", nullptr, "MainMenuFactionChina"}};
		const bool still = !mainMenuViewModel->FactionHoverAllowed();
		for (const Hover &hover : Hovers)
		{
			if (left == hover.window)
			{
				if (hover.hook != nullptr)
					game->SignalUiInteraction(std::string(hover.hook) + "Unhighlighted");
				if (hover.faction != nullptr && !still)
					transitions->Reverse(hover.faction);
			}
			if (entered == hover.window)
			{
				if (hover.hook != nullptr)
					game->SignalUiInteraction(std::string(hover.hook) + "Highlighted");
				if (hover.faction != nullptr && !still)
					transitions->SetGroup(hover.faction);
			}
		}
	}

	std::vector<ShellMenu *> Menus()
	{
		return {&mainMenu, &optionsMenu, &creditsMenu, &replayMenu, &loadMenu, &skirmishMenu, &mapSelectMenu, &lanLobbyMenu, &gameInfoMenu,
			&directConnectMenu, &lanOptionsMenu, &lanMapMenu, &challengeMenu, &messageBox};
	}

	// The window transition groups a screen plays as it shows (reversed as it goes).
	static std::string_view ScreenFade(shell::Screen screen)
	{
		switch (screen)
		{
		case shell::Screen::Skirmish: return "SkirmishGameOptionsMenuFade";
		case shell::Screen::NetworkLobby: return "LanLobbyFade";
		case shell::Screen::LanGameOptions: return "LanGameOptionsFade";
		case shell::Screen::DirectConnect: return "NetworkDirectConnectFade";
		case shell::Screen::ReplayMenu: return "ReplayMenuFade";
		case shell::Screen::LoadGame: return "SaveLoadMenuFade";
		case shell::Screen::ChallengeMenu: return "ChallengeMenuFade";
		default: return {};
		}
	}

	// The screen showing: it follows the one asked for once the way out has played (Shell::isAnimFinished).
	shell::Screen Screen() const { return shownScreen; }
	shell::Screen shownScreen{shell::Screen::MainMenu};
	float fontScale{1.0f}; // every WND font's size times this (GlobalLanguage::adjustFontSize)
	// The first launch: the screen fades in from black and the menu waits for the mouse to move
	// (more than 20 pixels from where it first was) or a key (MainMenuInput while notShown).
	bool menuWaiting{false};
	std::optional<std::pair<float, float>> firstPointer;
	bool leaving{false};
	// The window transitions (WindowTransitions.ini) over every layout, and the main menu's panel motion.
	std::optional<Engine::UI::WND::WNDTransitions> transitions;
	std::optional<shell::MainMenuMotion> mainMenuMotion;
	Engine::UI::WND::Renderer overlayRenderer;
	Engine::UI::WND::DrawList overlays{2048};
};

FrontEnd::FrontEnd() : m_state(std::make_unique<State>()) {}

FrontEnd::~FrontEnd()
{
	if (m_state)
	{
		m_state->model.requestedScreen.Unsubscribe(m_state->screenSubscription);
		m_state->model.optionsSaved.Unsubscribe(m_state->savedSubscription);
		m_state->model.requestedScreen.Unsubscribe(m_state->replaySubscription);
		m_state->model.requestedScreen.Unsubscribe(m_state->loadSubscription);
		m_state->model.requestedScreen.Unsubscribe(m_state->skirmishSubscription);
		m_state->model.requestedScreen.Unsubscribe(m_state->lanSubscription);
		m_state->model.optionsOpen.Unsubscribe(m_state->optionsOpenSubscription);
		m_state->model.requestedScreen.Unsubscribe(m_state->challengeSubscription);
	}
}

void FrontEnd::Load(const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings, content::ContentLoader &loader,
	GameClient &game, const Setup &setup)
{
	State &state = *m_state;
	state.game = &game;
	state.strings = &strings;
	state.setup = setup;
	std::string error;
	// Fonts grow with the screen (GlobalLanguage::adjustFontSize): at Options.ini's ResolutionFontAdjustment
	// (percent) when it names one, else Language.ini's (0.7).
	const content::LanguageFonts languageFonts = content::ReadLanguageFonts(loader.Load({"Data/English/Language"}));
	float fontAdjustment = Engine::Math::ToFloat(languageFonts.resolutionAdjustment);
	{
		const engine::config::Preferences options = engine::config::Preferences::Parse(ReadFileText(setup.userData / "Options.ini"));
		if (const auto user = options.Find("ResolutionFontAdjustment"))
			if (const float percent = std::strtof(std::string(*user).c_str(), nullptr) / 100.0f; percent >= 0.0f)
				fontAdjustment = percent;
	}
	state.fontScale = FontScale(setup.width, setup.height, fontAdjustment);
	const auto load = [&](ShellMenu &menu, const char *layout, const char *name) {
		if (!menu.Load(files, layout, strings, setup.width, setup.height, error, state.fontScale))
			std::fprintf(stderr, "%s: %s\n", name, error.c_str());
	};

	// Window transitions: every layout's windows by name, the interface's sounds.
	{
		const engine::config::Document &set = loader.Load({"Data/INI/WindowTransitions"});
		engine::config::BindContext context{loader.DiagnosticsFor(set), engine::time::FixedStep{30}};
		Engine::UI::WND::TransitionHost host;
		host.find = [&state](std::string_view name) {
			for (ShellMenu *menu : state.Menus())
				if (Engine::UI::WND::WNDWindow *window = menu->Document().Find_Window(name))
					return Engine::UI::WND::TransitionTarget{window, menu->ScaleX(), menu->ScaleY()};
			return Engine::UI::WND::TransitionTarget{};
		};
		host.play = [&state](std::string_view sound) { state.game->PlayInterfaceSound(sound); };
		host.image = [&state](std::string_view image) { return state.mainMenu.Image(image); };
		host.screen = {0.0f, 0.0f, static_cast<float>(setup.width), static_cast<float>(setup.height)};
		state.transitions.emplace(BindWindowTransitions(set, context), std::move(host));
	}

	// Main menu (MVVM: shell model -> view model -> WND view).
	load(state.mainMenu, "Window/Menus/MainMenu.wnd", "main menu");
	state.mainMenuViewModel.emplace(state.model);
	state.mainMenuBindings.emplace(state.mainMenu.Document());
	state.mainMenuMotion.emplace(*state.mainMenuViewModel, *state.transitions);
	shell::BindMainMenuView(*state.mainMenuBindings, *state.mainMenuViewModel, &*state.mainMenuMotion);
	ReportMissing("main menu", *state.mainMenuBindings);

	// Options: Options.ini in the player's data folder (the original's defaults otherwise),
	// applied now and saved when the player accepts them.
	state.optionsFile = setup.userData / "Options.ini";
	state.optionsPreferences = engine::config::Preferences::Parse(ReadFileText(state.optionsFile));
	const auto volumes = game.DefaultVolumes();
	state.optionDefaults = {volumes[0], volumes[1], volumes[2], volumes[3], volumes[4], 50};
	state.userOptions = shell::ReadUserOptions(state.optionsPreferences, state.optionDefaults);
	state.ApplyOptions();
	load(state.optionsMenu, "Window/Menus/OptionsMenu.wnd", "options menu");
	// The detail box's levels (OptionsMenuInit) and the display modes offered (the original lists the
	// adapter's modes from 800 x 600 up; these are the common ones; the window's own is shown).
	std::vector<std::u16string> detailNames;
	for (const char *label : {"GUI:Low", "GUI:Medium", "GUI:High", "GUI:VeryHigh", "GUI:Custom"})
		detailNames.push_back(Localized(strings, label));
	if (detailNames[3].empty() || detailNames[3] == u"GUI:VeryHigh")
		detailNames[3] = u"Very High"; // FETCH_OR_SUBSTITUTE
	std::vector<std::pair<int, int>> resolutions{{800, 600}, {1024, 768}, {1280, 720}, {1280, 1024}, {1366, 768}, {1600, 900}, {1920, 1080},
		{2560, 1440}, {3840, 2160}};
	const std::pair<int, int> window{static_cast<int>(setup.width), static_cast<int>(setup.height)};
	// IPEnumeration: this machine's addresses for the LAN and online boxes.
	std::vector<std::string> addresses;
	for (const engine::net::LocalAddress &local : engine::net::LocalAddresses())
		addresses.push_back(engine::net::ToString(local.address));
	state.optionsViewModel.emplace(state.model, state.userOptions, state.optionDefaults, std::move(detailNames), resolutions, window, std::move(addresses));
	state.optionsBindings.emplace(state.optionsMenu.Document());
	shell::BindOptionsView(*state.optionsBindings, *state.optionsViewModel, state.model);
	ReportMissing("options menu", *state.optionsBindings);
	state.optionsOpenSubscription = state.model.optionsOpen.Subscribe([&state](bool open) {
		if (state.game != nullptr)
			state.game->SignalUiInteraction(open ? "ShellOptionsOpened" : "ShellOptionsClosed");
	});
	state.savedSubscription = state.model.optionsSaved.Subscribe([&state](std::uint32_t saves) {
		if (saves == 0)
			return;
		state.ApplyOptions();
		shell::WriteUserOptions(state.optionsPreferences, state.userOptions);
		std::error_code ignored;
		std::filesystem::create_directories(state.setup.userData, ignored);
		std::ofstream(state.optionsFile, std::ios::binary) << state.optionsPreferences.Write();
	});
	if (setup.openOptions)
		state.model.optionsOpen.Set(true);

	// Credits: CreditsMenu.wnd over no shell map, Credits.ini scrolling, the "Credits" music.
	load(state.creditsMenu, "Window/Menus/CreditsMenu.wnd", "credits menu");
	if (!state.creditsView.Load(setup.width, state.fontScale, languageFonts.credits))
		std::fprintf(stderr, "credits: fonts could not be made\n");
	state.creditsSettings = shell::ReadCredits(files.ReadText("Data/INI/Credits.ini").value_or(std::string{}),
		[&strings](std::string_view label) { return Localized(strings, label); });
	state.screenSubscription = state.model.requestedScreen.Subscribe([&state](shell::Screen screen) {
		if (screen == shell::Screen::Credits && !state.credits)
		{
			state.credits.emplace(state.model, state.creditsSettings, state.creditsView.Heights(), static_cast<int>(state.setup.height));
			state.creditFrames = 0.0f;
			state.game->PlayMenuMusic("Credits");
		}
		else if (screen != shell::Screen::Credits && state.credits)
		{
			state.credits.reset();
			state.game->RestoreMusic();
		}
		state.look = true;
	});
	// Message boxes (MessageBox.wnd), shared by every screen.
	load(state.messageBox, "Window/Menus/MessageBox.wnd", "message box");
	state.messageBindings.emplace(state.messageBox.Document());
	shell::BindMessageBoxView(*state.messageBindings, state.messages);
	ReportMissing("message box", *state.messageBindings);

	// Replays: the player's Replays folder (the original's getReplayDir), copies to the Desktop.
	load(state.replayMenu, "Window/Menus/ReplayMenu.wnd", "replay menu");
	const std::filesystem::path replays = setup.userData / "Replays";
	// A folder's files, and the start of one (a header is all a menu reads).
	const auto listFolder = [](std::filesystem::path folder) {
		return [folder] {
			std::vector<std::string> names;
			std::error_code error;
			for (const auto &entry : std::filesystem::directory_iterator(folder, error))
				if (entry.is_regular_file(error))
					names.push_back(entry.path().filename().string());
			return names;
		};
	};
	const auto readStart = [](std::filesystem::path folder) {
		return [folder](const std::string &name) -> std::optional<std::vector<std::byte>> {
			std::ifstream file(folder / name, std::ios::binary);
			if (!file)
				return std::nullopt;
			std::vector<std::byte> bytes(4096);
			file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			bytes.resize(static_cast<std::size_t>(file.gcount()));
			return bytes;
		};
	};
	shell::ReplayFiles replayFiles;
	replayFiles.list = listFolder(replays);
	replayFiles.read = readStart(replays);
	replayFiles.remove = [replays](const std::string &name) {
		std::error_code error;
		return std::filesystem::remove(replays / name, error);
	};
	replayFiles.copyToDesktop = [replays](const std::string &name) {
		const char *profile = std::getenv("USERPROFILE");
		if (profile == nullptr)
			return false;
		std::error_code error;
		return std::filesystem::copy_file(replays / name, std::filesystem::path(profile) / "Desktop" / name, std::filesystem::copy_options::overwrite_existing, error);
	};
	// ReplayMenu's Load: the host plays it back (RecorderClass::playbackFile).
	replayFiles.play = [&state, replays](const std::string &name) { state.replayRequest = replays / name; };
	shell::ReplayTexts replayTexts;
	const auto text = [&strings](const char *label, std::u16string &out) {
		if (const std::u16string found = Localized(strings, label); !found.empty() && found != std::u16string(label, label + std::char_traits<char>::length(label)))
			out = found;
	};
	text("GUI:LastReplay", replayTexts.lastReplay);
	text("GUI:NoFileSelected", replayTexts.noFileSelected);
	text("GUI:PleaseSelectAFile", replayTexts.pleaseSelectAFile);
	text("GUI:DeleteFile", replayTexts.deleteFile);
	text("GUI:AreYouSureDelete", replayTexts.areYouSureDelete);
	text("GUI:CopyReplay", replayTexts.copyReplay);
	text("GUI:AreYouSureCopy", replayTexts.areYouSureCopy);
	text("GUI:Error", replayTexts.error);
	state.replayViewModel.emplace(state.model, state.messages, std::move(replayFiles), std::move(replayTexts));
	state.replayBindings.emplace(state.replayMenu.Document());
	shell::BindReplayMenuView(*state.replayBindings, *state.replayViewModel);
	ReportMissing("replay menu", *state.replayBindings);
	state.replaySubscription = state.model.requestedScreen.Subscribe([&state](shell::Screen screen) {
		if (screen == shell::Screen::ReplayMenu)
			state.replayViewModel->Populate(); // ReplayMenuInit
	});

	// The load screen (SaveLoad.wnd, load only): the player's Save folder (GameState::getSaveDirectory).
	load(state.loadMenu, "Window/Menus/SaveLoad.wnd", "load screen");
	const std::filesystem::path saves = setup.userData / "Save";
	shell::SaveFiles saveFiles;
	saveFiles.list = listFolder(saves);
	saveFiles.read = readStart(saves);
	saveFiles.remove = [saves](const std::string &name) {
		std::error_code error;
		return std::filesystem::remove(saves / name, error);
	};
	// SaveLoadMenuSystem's Load: the host loads it (GameState::loadGame).
	saveFiles.load = [&state, saves](const std::string &name) { state.loadRequest = saves / name; };
	state.saveFolder = saves;
	saveFiles.mapName = [&strings](const std::string &label) {
		const std::u16string found = Localized(strings, label.c_str());
		return found == std::u16string(label.begin(), label.end()) ? std::u16string{} : found;
	};
	shell::SaveLoadTexts saveTexts;
	text("GUI:Error", saveTexts.error);
	state.saveFiles = saveFiles;
	text("GUI:NewSaveGame", saveTexts.newSave);
	state.saveTexts = saveTexts;
	state.loadViewModel.emplace(state.model, state.messages, std::move(saveFiles), std::move(saveTexts));
	// The menus over a game (their view models once the host answers for them: SetGameMenus).
	load(state.quitMenu, "Window/Menus/QuitMenu.wnd", "quit menu");
	load(state.quitNoSaveMenu, "Window/Menus/QuitNoSave.wnd", "quit menu (multiplayer)");
	load(state.popupSaveMenu, "Window/Menus/PopupSaveLoad.wnd", "save/load popup");
	// The score screen after a game.
	load(state.scoreMenu, "Window/Menus/ScoreScreen.wnd", "score screen");
	state.scoreViewModel.emplace([&strings](std::string_view label) { return Localized(strings, std::string(label).c_str()); });
	state.scoreBindings.emplace(state.scoreMenu.Document());
	{
		const ShellMenu &scoreImages = state.scoreMenu;
		shell::BindScoreScreenView(*state.scoreBindings, *state.scoreViewModel, [&scoreImages](std::string_view name) { return scoreImages.Image(name); });
	}
	ReportMissing("score screen", *state.scoreBindings);
	state.loadBindings.emplace(state.loadMenu.Document());
	shell::BindSaveLoadView(*state.loadBindings, *state.loadViewModel);
	ReportMissing("load screen", *state.loadBindings);
	state.loadSubscription = state.model.requestedScreen.Subscribe([&state](shell::Screen screen) {
		if (screen == shell::Screen::LoadGame)
			state.loadViewModel->Populate(); // SaveLoadMenuFullScreenInit
		// MainMenu's side buttons (setCampaign "USA", "GLA", "China") and difficulty (prepareCampaignGame).
		if (screen == shell::Screen::Campaign)
		{
			const shell::Side side = state.model.campaignSide.Get();
			const char *campaign = side == shell::Side::America ? "USA" : side == shell::Side::China ? "China" : side == shell::Side::Gla ? "GLA" : "TRAINING";
			state.campaignStart = FrontEnd::CampaignStart{campaign, static_cast<std::uint8_t>(state.model.campaignDifficulty.Get()), {}};
		}
	});

	// Skirmish setup (SkirmishGameOptionsMenu.wnd, SkirmishMapSelectMenu.wnd; Skirmish.ini, SkirmishStats.ini).
	load(state.skirmishMenu, "Window/Menus/SkirmishGameOptionsMenu.wnd", "skirmish menu");
	load(state.mapSelectMenu, "Window/Menus/SkirmishMapSelectMenu.wnd", "skirmish map list");
	state.setupCatalog = LoadSetupCatalog(loader, files, strings);
	state.skirmishPreferences = engine::config::Preferences::Parse(ReadFileText(setup.userData / "Skirmish.ini"));
	shell::SkirmishServices skirmishServices;
	skirmishServices.text = [&strings](std::string_view label) {
		const std::u16string found = Localized(strings, label);
		return found == std::u16string(label.begin(), label.end()) ? std::u16string{} : found;
	};
	skirmishServices.savePreferences = [&state] {
		std::error_code ignored;
		std::filesystem::create_directories(state.setup.userData, ignored);
		std::ofstream(state.setup.userData / "Skirmish.ini", std::ios::binary) << state.skirmishPreferences.Write();
	};
	skirmishServices.start = [&state](const session::setup::GameSetup &setup, int) { state.gameStart = setup; };
	skirmishServices.seed = [] {
		return static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	};
	skirmishServices.machineName = [] {
		const char *name = std::getenv("COMPUTERNAME");
		const std::string machine = name != nullptr ? name : "Player";
		return std::u16string(machine.begin(), machine.end());
	};
	state.skirmishViewModel.emplace(state.model, state.messages, state.setupCatalog, state.skirmishPreferences, std::move(skirmishServices));
	state.skirmishBindings.emplace(state.skirmishMenu.Document());
	state.mapSelectBindings.emplace(state.mapSelectMenu.Document());
	const ShellMenu &images = state.skirmishMenu;
	Engine::UI::WND::WNDFrame frame;
	frame.top = images.Image("FrameT"), frame.bottom = images.Image("FrameB"), frame.left = images.Image("FrameL"), frame.right = images.Image("FrameR");
	frame.upper_left = images.Image("FrameCornerUL"), frame.upper_right = images.Image("FrameCornerUR");
	frame.lower_left = images.Image("FrameCornerLL"), frame.lower_right = images.Image("FrameCornerLR");
	state.skirmishView.emplace(*state.skirmishBindings, state.skirmishMenu.Document(), *state.mapSelectBindings, state.mapSelectMenu.Document(),
		*state.skirmishViewModel, [&images](std::string_view name) { return images.Image(name); }, frame);
	ReportMissing("skirmish menu", *state.skirmishBindings);
	ReportMissing("skirmish map list", *state.mapSelectBindings);
	state.skirmishSubscription = state.model.requestedScreen.Subscribe([&state](shell::Screen screen) {
		if (screen != shell::Screen::Skirmish)
			return;
		// SkirmishGameOptionsMenuInit, with SkirmishBattleHonors' record.
		const engine::config::Preferences stats = engine::config::Preferences::Parse(ReadFileText(state.setup.userData / "SkirmishStats.ini"));
		state.skirmishViewModel->Open(stats);
	});

	// LAN (LanLobbyMenu.wnd with GameInfoWindow.wnd over its StaticTextGameInfo, NetworkDirectConnect.wnd,
	// LanGameOptionsMenu.wnd with LanMapSelectMenu.wnd over it; Network.ini).
	load(state.lanLobbyMenu, "Window/Menus/LanLobbyMenu.wnd", "LAN lobby");
	load(state.gameInfoMenu, "Window/Menus/GameInfoWindow.wnd", "LAN game info");
	load(state.directConnectMenu, "Window/Menus/NetworkDirectConnect.wnd", "direct connect");
	load(state.lanOptionsMenu, "Window/Menus/LanGameOptionsMenu.wnd", "LAN game options");
	load(state.lanMapMenu, "Window/Menus/LanMapSelectMenu.wnd", "LAN map list");
	if (const auto *slot = state.lanLobbyMenu.Document().Find_Window("LanLobbyMenu.wnd:StaticTextGameInfo"))
	{
		// GameInfoWindow: laid over the lobby's info box (both layouts at the lobby's resolution).
		const int from = state.lanLobbyMenu.AuthoredWidth(), to = state.gameInfoMenu.AuthoredWidth();
		state.gameInfoMenu.Document().Move_Window("GameInfoWindow.wnd:ParentGameInfo", slot->screen_region.left * to / from, slot->screen_region.top * to / from);
	}
	state.lanPreferences = engine::config::Preferences::Parse(ReadFileText(setup.userData / "Network.ini"));
	shell::LanServices lanServices;
	lanServices.start = [&state](const session::setup::GameSetup &setup, int slot, std::uint32_t host, bool hosting) {
		state.lanStart = FrontEnd::LanStart{setup, slot, host, hosting};
	};
	lanServices.text = [&strings](std::string_view label) {
		const std::u16string found = Localized(strings, label);
		return found == std::u16string(label.begin(), label.end()) ? std::u16string{} : found;
	};
	lanServices.savePreferences = [&state] {
		std::error_code ignored;
		std::filesystem::create_directories(state.setup.userData, ignored);
		std::ofstream(state.setup.userData / "Network.ini", std::ios::binary) << state.lanPreferences.Write();
	};
	lanServices.machineName = [] {
		const std::string machine = engine::net::MachineName();
		return std::u16string(machine.begin(), machine.end());
	};
	lanServices.mapName = [&state](const std::string &file) {
		const shell::SetupMap *map = state.setupCatalog.Find(file);
		return map != nullptr ? map->name : std::u16string{};
	};
	lanServices.addressText = [](std::uint32_t address) { return engine::net::ToString(address); };
	state.lanLobbyViewModel.emplace(state.model, state.messages, state.setupCatalog, state.lanPreferences, lanServices);
	state.directConnectViewModel.emplace(state.model, state.lanPreferences, lanServices);
	state.directConnectViewModel->PreferredMap = [&state] { return state.lanLobbyViewModel->PreferredMap(); };
	state.lanOptionsViewModel.emplace(state.model, state.messages, state.setupCatalog, state.lanPreferences, lanServices);
	state.lanOptionsViewModel->m_countdown = state.setupCatalog.startCountdown;
	state.lanOptionsViewModel->m_lineFor = [&state](const std::u16string &name, std::uint32_t ip, const std::u16string &text,
												   generalszh::network::lan::ChatType type) { return state.lanLobbyViewModel->Line(name, ip, text, type); };
	state.lanLobbyBindings.emplace(state.lanLobbyMenu.Document());
	state.gameInfoBindings.emplace(state.gameInfoMenu.Document());
	state.directConnectBindings.emplace(state.directConnectMenu.Document());
	state.lanOptionsBindings.emplace(state.lanOptionsMenu.Document());
	state.lanMapBindings.emplace(state.lanMapMenu.Document());
	state.lanLobbyView.emplace(*state.lanLobbyBindings, *state.lanLobbyViewModel);
	state.gameInfoView.emplace(*state.gameInfoBindings, *state.lanLobbyViewModel);
	shell::BindDirectConnectView(*state.directConnectBindings, *state.directConnectViewModel);
	const ShellMenu &lanImages = state.lanOptionsMenu;
	state.lanOptionsView.emplace(*state.lanOptionsBindings, state.lanOptionsMenu.Document(), *state.lanMapBindings, state.lanMapMenu.Document(),
		*state.lanOptionsViewModel, [&lanImages](std::string_view name) { return lanImages.Image(name); }, frame);
	for (const auto &[name, bindings] : {std::pair{"LAN lobby", &*state.lanLobbyBindings}, std::pair{"LAN game info", &*state.gameInfoBindings},
			 std::pair{"direct connect", &*state.directConnectBindings}, std::pair{"LAN game options", &*state.lanOptionsBindings},
			 std::pair{"LAN map list", &*state.lanMapBindings}})
		ReportMissing(name, *bindings);
	// The lobby's content: maps by file and CRC, the colours and factions a setup may name.
	state.lobbyServices.text = lanServices.text;
	state.lobbyServices.hasMap = [&state](const std::string &file, std::uint32_t crc) {
		const shell::SetupMap *map = state.setupCatalog.Find(file);
		return map != nullptr && (crc == 0 || map->crc == crc);
	};
	state.lobbyServices.wouldTransfer = [&state](const std::string &file) {
		const shell::SetupMap *map = state.setupCatalog.Find(file);
		return map == nullptr || !map->official;
	};
	state.lobbyServices.mapName = lanServices.mapName;
	state.lobbyServices.seed = [] {
		return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	};
	state.lobbyServices.limits = {static_cast<int>(state.setupCatalog.colors.size()), state.setupCatalog.playerTemplateCount};
	state.lobbyServices.startCountdown = state.setupCatalog.startCountdown;
	state.lanSubscription = state.model.requestedScreen.Subscribe([&state](shell::Screen screen) {
		const bool lanScreen = screen == shell::Screen::NetworkLobby || screen == shell::Screen::DirectConnect || screen == shell::Screen::LanGameOptions;
		if (!lanScreen)
		{
			// Out of LAN: the lobby and its socket go (LanLobbyMenu's Back deletes TheLAN).
			state.lanLobby.reset();
			state.lanSocket.reset();
			return;
		}
		// Back in the lobby from a game (LanLobbyMenuInit: TheLAN->reset()): its games and players heard afresh.
		if (screen == shell::Screen::NetworkLobby && state.lanLobby && state.lanLobby->CurrentGame() != nullptr &&
			state.lanLobby->CurrentGame()->inProgress)
		{
			state.lanLobby.reset();
			state.lanSocket.reset();
		}
		if (!state.lanLobby)
		{
			// The address Options.ini names (IPAddress), else the first; port 8086, broadcasts on.
			const auto addresses = engine::net::LocalAddresses();
			std::uint32_t address = addresses.empty() ? engine::net::LoopbackAddress : addresses.front().address;
			for (const engine::net::LocalAddress &local : addresses)
				if (engine::net::ToString(local.address) == state.userOptions.lanAddress)
					address = local.address;
			if (state.setup.lanLoopback)
				address = engine::net::LoopbackAddress;
			state.lanAddress = address;
			state.lanSocket = engine::net::UdpSocket::Open(generalszh::network::lan::LobbyPort, true, address);
			if (!state.lanSocket && !state.setup.lanLoopback)
				state.lanSocket = engine::net::UdpSocket::Open(generalszh::network::lan::LobbyPort, true, 0);
			if (!state.lanSocket)
			{
				state.messages.Show(shell::MessageBoxOk(Localized(*state.strings, "GUI:NetworkError"), Localized(*state.strings, "GUI:SocketError")));
				state.model.Pop();
				return;
			}
			generalszh::network::lan::Link link;
			engine::net::UdpSocket *socket = state.lanSocket.get();
			link.send = [socket](std::uint32_t to, std::span<const std::byte> datagram) {
				socket->SendTo({to, generalszh::network::lan::LobbyPort}, datagram);
			};
			link.receive = [socket]() -> std::optional<std::pair<std::uint32_t, std::vector<std::byte>>> {
				auto datagram = socket->Receive();
				if (!datagram)
					return std::nullopt;
				return std::pair{datagram->from.address, std::move(datagram->bytes)};
			};
			generalszh::network::lan::LobbyEvents events;
			events.playersChanged = [&state] { state.lanLobbyViewModel->PlayersChanged(); };
			events.gamesChanged = [&state] { state.lanLobbyViewModel->GamesChanged(); };
			events.gameCreated = [&state](generalszh::network::lan::Result result) { state.lanLobbyViewModel->GameCreated(result); };
			events.gameJoined = [&state](generalszh::network::lan::Result result) { state.lanLobbyViewModel->GameJoined(result); };
			events.slotsChanged = [&state] {
				if (state.Screen() == shell::Screen::LanGameOptions)
					state.lanOptionsViewModel->SlotsChanged();
			};
			events.leftGame = [&state] {
				if (state.Screen() == shell::Screen::LanGameOptions)
					state.lanOptionsViewModel->LeftGame();
			};
			events.chat = [&state](std::u16string name, std::uint32_t ip, std::u16string text, generalszh::network::lan::ChatType type) {
				if (state.lanLobby->InLobby())
					state.lanLobbyViewModel->Chat(name, ip, text, type);
				else
					state.lanOptionsViewModel->Chat(name, ip, text, type);
			};
			events.gameStarted = [&state] { state.lanOptionsViewModel->GameStarted(); };
			state.lanLobby = std::make_unique<generalszh::network::lan::LanLobby>(std::move(link), address, state.lobbyServices, std::move(events));
		}
		// Each screen's Init as it shows (the lobby's again when a screen over it goes).
		if (screen == shell::Screen::NetworkLobby)
			state.lanLobbyViewModel->Open(*state.lanLobby);
		else if (screen == shell::Screen::DirectConnect)
			state.directConnectViewModel->Open(*state.lanLobby);
		else
			state.lanOptionsViewModel->Open(*state.lanLobby);
	});

	// The Generals' Challenge (ChallengeMenu.wnd; ChallengeMode.ini with PlayerTemplate.ini's medallions).
	load(state.challengeMenu, "Window/Menus/ChallengeMenu.wnd", "challenge menu");
	{
		const engine::config::Document &challengeSet = loader.Load({"Data/INI/ChallengeMode"});
		const engine::config::Document &templateSet = loader.Load({"Data/INI/Default/PlayerTemplate", "Data/INI/PlayerTemplate"});
		engine::config::BindContext context{loader.DiagnosticsFor(challengeSet), engine::time::FixedStep{30}};
		std::vector<shell::ChallengeGeneral> generals;
		for (const content::GeneralPersona &persona : content::BindChallengeGenerals(challengeSet, &templateSet, context))
		{
			shell::ChallengeGeneral &general = generals.emplace_back();
			general.enabled = persona.startsEnabled;
			general.normal = persona.medallionNormal, general.hilite = persona.medallionHilite, general.selected = persona.medallionSelected;
			general.portrait = persona.bioPortraitSmall;
			const auto fetch = [&strings](const std::string &label) { return label.empty() ? std::u16string{} : Localized(strings, label); };
			general.name = fetch(persona.bioName), general.rank = fetch(persona.bioRank);
			general.branch = fetch(persona.bioBranch), general.strategy = fetch(persona.bioStrategy);
			general.previewSound = persona.previewSound;
			general.campaign = persona.campaign, general.playerTemplate = persona.playerTemplate;
		}
		shell::ChallengeServices services;
		services.sound = [&game](std::string_view sound) { game.PlayInterfaceSound(sound); };
		services.voice = [&game](std::string_view voice) { game.PlayInterfaceVoice(voice); };
		// setGeneralCampaign: the general's campaign, played as its PlayerTemplate.
		services.start = [&state](const shell::ChallengeGeneral &general, shell::Difficulty difficulty) {
			state.campaignStart = FrontEnd::CampaignStart{general.campaign, static_cast<std::uint8_t>(difficulty), general.playerTemplate};
		};
		state.challengeViewModel.emplace(state.model, std::move(generals), std::move(services));
	}
	state.challengeBindings.emplace(state.challengeMenu.Document());
	{
		const ShellMenu &challengeImages = state.challengeMenu;
		shell::BindChallengeView(*state.challengeBindings, state.challengeMenu.Document(), *state.challengeViewModel,
			[&challengeImages](std::string_view name) { return challengeImages.Image(name); },
			[&challengeImages](std::string_view name) { return challengeImages.ImageWidth(name); });
	}
	ReportMissing("challenge menu", *state.challengeBindings);
	state.challengeSubscription = state.model.requestedScreen.Subscribe([&state, open = false](shell::Screen screen) mutable {
		if (screen == shell::Screen::ChallengeMenu && !open)
		{
			state.challengeViewModel->Open(); // ChallengeMenuInit
			state.challengeFrames = 0.0f;
		}
		else if (screen != shell::Screen::ChallengeMenu && open)
			state.challengeViewModel->Close(); // ChallengeMenuShutdown
		open = screen == shell::Screen::ChallengeMenu;
	});

	if (setup.startScreen == "credits")
		state.model.Push(shell::Screen::Credits);
	else if (setup.startScreen == "lan")
		state.model.Push(shell::Screen::NetworkLobby);
	else if (setup.startScreen == "skirmish")
		state.model.Push(shell::Screen::Skirmish);
	else if (setup.startScreen == "load")
		state.model.Push(shell::Screen::LoadGame);
	else if (setup.startScreen == "replays")
		state.model.Push(shell::Screen::ReplayMenu);
	else if (setup.startScreen == "challenge")
		state.model.Push(shell::Screen::ChallengeMenu);
	// The first screen shows at once and plays in (the main menu's buttons flash in: MainMenuInit's first time).
	state.shownScreen = state.model.requestedScreen.Get();
	if (state.shownScreen == shell::Screen::MainMenu && !setup.menuAtOnce)
	{
		state.transitions->Reverse("FadeWholeScreen");
		state.mainMenuMotion->Hide();
		state.menuWaiting = true;
	}
	else if (state.shownScreen == shell::Screen::MainMenu)
		state.mainMenuMotion->Enter(true);
	else if (const std::string_view in = State::ScreenFade(state.shownScreen); !in.empty())
		state.transitions->SetGroup(in);
}

void FrontEnd::Handle(const engine::platform::PlatformEvent &event)
{
	State &state = *m_state;
	if (state.inGame && state.Active().first == nullptr)
		return; // the game's input
	using engine::platform::EventType;
	using engine::platform::KeyCode;
	using Engine::UI::WND::WNDInputEvent;
	using Engine::UI::WND::WNDPointer;
	if (!state.inGame && event.type == EventType::key_up && event.key == KeyCode::escape && state.credits)
	{
		state.credits->Leave(); // CreditsMenuInput: Escape (released) closes the credits
		return;
	}
	if (!state.inGame && event.type == EventType::key_up && event.key == KeyCode::escape && state.Screen() == shell::Screen::LoadGame && !state.messages.open.Get()
		&& !state.model.optionsOpen.Get())
	{
		state.loadViewModel->escape.Execute(); // SaveLoadMenuInput: Escape (released) backs out
		return;
	}
	if (!state.inGame && event.type == EventType::key_up && event.key == KeyCode::escape && state.Screen() == shell::Screen::Skirmish && !state.messages.open.Get()
		&& !state.model.optionsOpen.Get())
	{
		state.skirmishViewModel->escape.Execute(); // SkirmishGameOptionsMenuInput / SkirmishMapSelectMenuInput
		return;
	}
	if (!state.inGame && event.type == EventType::key_up && event.key == KeyCode::escape && !state.messages.open.Get() && !state.model.optionsOpen.Get())
	{
		// LanLobbyMenuInput / NetworkDirectConnectInput / LanGameOptionsMenuInput: Escape (released) backs out.
		if (state.Screen() == shell::Screen::NetworkLobby)
			return static_cast<void>(state.lanLobbyViewModel->escape.Execute());
		if (state.Screen() == shell::Screen::DirectConnect)
			return static_cast<void>(state.directConnectViewModel->escape.Execute());
		if (state.Screen() == shell::Screen::LanGameOptions)
			return static_cast<void>(state.lanOptionsViewModel->escape.Execute());
		if (state.Screen() == shell::Screen::ChallengeMenu)
			return static_cast<void>(state.challengeViewModel->back.Execute()); // ChallengeMenuInput
	}
	if (state.menuWaiting && !state.inGame)
	{
		bool stirred = event.type == EventType::key_down || event.type == EventType::text_input;
		if (event.type == EventType::mouse_moved)
		{
			if (!state.firstPointer)
				state.firstPointer = std::pair{event.position.x, event.position.y};
			stirred = std::abs(event.position.x - state.firstPointer->first) > 20.0f || std::abs(event.position.y - state.firstPointer->second) > 20.0f;
		}
		if (!stirred)
			return;
		state.menuWaiting = false;
		state.mainMenuMotion->Enter(true);
		state.look = true;
		return;
	}
	Engine::UI::WND::WNDDocument *layout = state.Layout();
	Engine::UI::WND::WNDBindings *bindings = state.Bindings();
	if (layout == nullptr || bindings == nullptr)
		return; // a screen without a layout of its own yet, or the credits (no mouse)
	// The mouse over the layout, in the resolution it was authored at.
	const ShellMenu &menu = *state.Active().first;
	const float x = event.position.x * static_cast<float>(menu.AuthoredWidth()) / static_cast<float>(state.setup.width);
	const float y = event.position.y * static_cast<float>(menu.AuthoredHeight()) / static_cast<float>(state.setup.height);
	std::optional<WNDInputEvent> input;
	switch (event.type)
	{
	case EventType::text_input:
	{
		std::u16string typed;
		for (const char *c = event.text; *c != 0; ++c)
			if (static_cast<unsigned char>(*c) < 0x80)
				typed.push_back(static_cast<char16_t>(*c));
		input = state.pointer.Type(*layout, typed);
		break;
	}
	case EventType::key_down:
		if (event.key == KeyCode::backspace)
			input = state.pointer.Key(*layout, WNDPointer::EditKey::Backspace);
		else if (event.key == KeyCode::enter || event.key == KeyCode::keypad_enter)
			input = state.pointer.Key(*layout, WNDPointer::EditKey::Enter);
		break;
	case EventType::mouse_moved:
		input = state.pointer.Move(*layout, x, y);
		state.lastPointer = {x, y};
		break;
	case EventType::mouse_button_down:
		if (event.code == 1)
			input = state.pointer.Press(*layout, x, y);
		state.lastPointer = {x, y};
		break;
	case EventType::mouse_button_up:
		if (event.code == 3)
			input = state.pointer.RightRelease(*layout, x, y);
		if (event.code == 1)
			input = state.pointer.Release(*layout, x, y,
				static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
		state.lastPointer = {x, y};
		break;
	case EventType::mouse_wheel:
		state.pointer.Wheel(*layout, state.lastPointer.first, state.lastPointer.second, event.y > 0.0f ? -1 : 1);
		break;
	default: break;
	}
	if (!input)
		return;
	if (input->kind == WNDInputEvent::Kind::Hovered)
	{
		state.Hovered(input->window, input->left);
		bindings->Apply(*input);
		return;
	}
	if (state.leaving)
		return;
	// The shell map's scripts hear some main menu buttons pressed (signalUIInteract ..._SELECTED).
	if (state.shownScreen == shell::Screen::MainMenu && input->kind == WNDInputEvent::Kind::Clicked)
		for (const auto &[window, hook] : {std::pair{"MainMenu.wnd:ButtonExit", "ShellMainMenuExitPushed"},
				 std::pair{"MainMenu.wnd:ButtonSkirmish", "ShellMainMenuSkirmishPushed"}, std::pair{"MainMenu.wnd:ButtonNetwork", "ShellMainMenuNetworkPushed"},
				 std::pair{"MainMenu.wnd:ButtonOptions", "ShellMainMenuOptionsPushed"}})
			if (input->window == window)
				state.game->SignalUiInteraction(hook);
	// GadgetPushButton / GadgetCheckBox play the click as they go down (combo boxes as they come up).
	if (input->kind == WNDInputEvent::Kind::Pressed || input->kind == WNDInputEvent::Kind::Toggled)
		state.game->PlayInterfaceSound("GUIClick");
	bindings->Apply(*input);
}

void FrontEnd::Step(float frameSeconds)
{
	State &state = *m_state;
	if (state.inGame)
	{
		// Scripted clicks on the menus over the game, one a step while one shows.
		if (state.gamePressed < state.setup.gamePresses.size())
			if (Engine::UI::WND::WNDBindings *bindings = state.Bindings())
			{
				Engine::UI::WND::WNDInputEvent click;
				click.kind = Engine::UI::WND::WNDInputEvent::Kind::Clicked;
				click.window = state.setup.gamePresses[state.gamePressed++];
				bindings->Apply(click);
			}
		// The menus over the game: rebuilt as they change or another takes the pointer.
		bool changed = false;
		for (auto *bindings : {&state.quitBindings, &state.quitNoSaveBindings, &state.popupSaveBindings})
			if (*bindings)
				changed = (*bindings)->TakeDirty() || changed;
		changed = state.optionsBindings->TakeDirty() || changed;
		changed = state.messageBindings->TakeDirty() || changed;
		const auto active = state.Active();
		if (changed || active != state.wasActive)
		{
			if (state.wasActive.first != nullptr)
				state.pointer.Reset(state.wasActive.first->Document());
			state.wasActive = active;
			state.look = true;
		}
		if (std::exchange(state.look, false) || state.pointer.TakeLook())
			for (ShellMenu *menu : {&state.quitMenu, &state.quitNoSaveMenu, &state.popupSaveMenu, &state.optionsMenu, &state.messageBox})
				if (!menu->Refresh())
					std::fprintf(stderr, "front end: a menu's render list could not be built\n");
		return;
	}
	// Screens not built yet go straight back to where they came from.
	const shell::Screen asked = state.model.requestedScreen.Get();
	if (asked != shell::Screen::MainMenu && asked != shell::Screen::Credits && asked != shell::Screen::ReplayMenu
		&& asked != shell::Screen::LoadGame && asked != shell::Screen::Skirmish && asked != shell::Screen::NetworkLobby
		&& asked != shell::Screen::DirectConnect && asked != shell::Screen::LanGameOptions && asked != shell::Screen::ChallengeMenu
		&& asked != shell::Screen::ScoreScreen && asked != shell::Screen::Campaign)
	{
		std::fprintf(stderr, "front end: that screen is not built yet\n");
		state.model.Pop();
	}
	// The credits scroll a step each original frame (30 a second, on real time).
	if (state.credits)
		for (state.creditFrames += frameSeconds * 30.0f; state.creditFrames >= 1.0f && state.credits; state.creditFrames -= 1.0f)
			state.credits->Step();
	// ChallengeMenuUpdate each original frame while the menu shows.
	if (state.shownScreen == shell::Screen::ChallengeMenu && !state.leaving)
		for (state.challengeFrames += frameSeconds * 30.0f; state.challengeFrames >= 1.0f; state.challengeFrames -= 1.0f)
			state.challengeViewModel->Update(state.transitions->IsFinished());
	// The window transitions: the original's 30-a-second frames, on real time.
	state.transitions->Update(static_cast<double>(frameSeconds) * 30.0);
	// MainMenuUpdate: transitions finished, the menu's buttons may start new ones.
	if (state.transitions->IsFinished())
		state.mainMenuViewModel->TransitionsFinished();
	state.mainMenuMotion->Step();
	// Another screen asked for: the one showing plays its way out first, then the new one shows and plays in.
	if (state.model.requestedScreen.Get() != state.shownScreen)
	{
		if (!state.leaving)
		{
			const std::string out = state.shownScreen == shell::Screen::MainMenu ? state.mainMenuMotion->Leave(state.model.requestedScreen.Get())
				: std::string(State::ScreenFade(state.shownScreen));
			if (!out.empty())
				state.transitions->Reverse(out);
			state.leaving = true;
		}
		if (state.transitions->IsFinished())
		{
			const shell::Screen was = state.shownScreen;
			state.shownScreen = state.model.requestedScreen.Get();
			// SHELL_SCRIPT_HOOK_SKIRMISH/LAN_OPENED and _CLOSED.
			if (was == shell::Screen::Skirmish)
				state.game->SignalUiInteraction("ShellSkirmishClosed");
			if (state.shownScreen == shell::Screen::Skirmish)
				state.game->SignalUiInteraction("ShellSkirmishOpened");
			if (was == shell::Screen::NetworkLobby && state.shownScreen == shell::Screen::MainMenu)
				state.game->SignalUiInteraction("ShellLANClosed");
			if (was == shell::Screen::MainMenu && state.shownScreen == shell::Screen::NetworkLobby)
				state.game->SignalUiInteraction("ShellLANOpened");
			state.leaving = false;
			if (state.shownScreen == shell::Screen::MainMenu)
			{
				state.mainMenuViewModel->Entered();
				state.mainMenuMotion->Enter(false);
			}
			else if (const std::string_view in = State::ScreenFade(state.shownScreen); !in.empty())
			{
				state.transitions->Remove("MainMenuDefaultMenuLogoFade");
				state.transitions->SetGroup(in);
			}
			state.look = true;
		}
	}
	if (state.transitions->Active())
		state.look = true; // windows show and hide as the transitions play
	// The LAN lobby's traffic (LANAPI::update, throttled inside), on real time.
	if (state.lanLobby)
		state.lanLobby->Update(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
	// Scripted clicks, one a step, on whatever layout has the pointer.
	if (state.pressed < state.setup.presses.size())
		if (Engine::UI::WND::WNDBindings *bindings = state.Bindings())
		{
			// "hover:<window>" points at a window (GBM_MOUSE_ENTERING) instead.
			Engine::UI::WND::WNDInputEvent click;
			click.kind = Engine::UI::WND::WNDInputEvent::Kind::Clicked;
			click.window = state.setup.presses[state.pressed++];
			if (click.window.starts_with("hover:"))
			{
				click.kind = Engine::UI::WND::WNDInputEvent::Kind::Hovered;
				click.window.erase(0, 6);
			}
			bindings->Apply(click);
		}
	bool changed = false;
	for (Engine::UI::WND::WNDBindings *bindings : {&*state.mainMenuBindings, &*state.optionsBindings, &*state.replayBindings, &*state.loadBindings, &*state.skirmishBindings, &*state.mapSelectBindings, &*state.lanLobbyBindings, &*state.gameInfoBindings,
			 &*state.directConnectBindings, &*state.lanOptionsBindings, &*state.lanMapBindings, &*state.challengeBindings, &*state.messageBindings, &*state.scoreBindings})
		changed = bindings->TakeDirty() || changed;
	const auto active = state.Active();
	if (changed || active != state.wasActive)
	{
		// Panels changed under the pointer, or another layout took it.
		if (state.wasActive.first != nullptr)
			state.pointer.Reset(state.wasActive.first->Document());
		state.wasActive = active;
		state.look = true;
	}
	if (std::exchange(state.look, false) || state.pointer.TakeLook())
		for (ShellMenu *menu : {&state.mainMenu, &state.optionsMenu, &state.creditsMenu, &state.replayMenu, &state.loadMenu, &state.skirmishMenu, &state.mapSelectMenu, &state.lanLobbyMenu, &state.gameInfoMenu,
			 &state.directConnectMenu, &state.lanOptionsMenu, &state.lanMapMenu, &state.challengeMenu, &state.messageBox, &state.scoreMenu})
			if (!menu->Refresh())
				std::fprintf(stderr, "front end: a menu's render list could not be built\n");
}

bool FrontEnd::ShellMapShown() const noexcept { return !m_state->credits; }

void FrontEnd::Draw(Graphics::Renderer2D &renderer)
{
	State &state = *m_state;
	if (state.inGame)
	{
		if (state.quitViewModel && state.quitViewModel->open.Get())
			(state.quitViewModel->saveLoadShown.Get() ? state.quitMenu : state.quitNoSaveMenu).Draw(renderer);
		if (state.popupSaveOpen)
			state.popupSaveMenu.Draw(renderer);
		if (state.model.optionsOpen.Get())
			state.optionsMenu.Draw(renderer);
		if (state.messages.open.Get())
			state.messageBox.Draw(renderer);
		return;
	}
	if (state.setup.startScreen == "none")
		return; // captures of the bare shell map
	if (state.credits)
	{
		state.creditsMenu.Draw(renderer);
		state.creditsView.Draw(*state.credits, renderer);
	}
	else if (state.Screen() == shell::Screen::MainMenu)
		state.mainMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::ReplayMenu)
		state.replayMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::LoadGame)
		state.loadMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::ScoreScreen)
		state.scoreMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::ChallengeMenu)
		state.challengeMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::NetworkLobby)
	{
		state.lanLobbyMenu.Draw(renderer);
		state.gameInfoMenu.Draw(renderer);
	}
	else if (state.Screen() == shell::Screen::DirectConnect)
		state.directConnectMenu.Draw(renderer);
	else if (state.Screen() == shell::Screen::LanGameOptions)
	{
		state.lanOptionsMenu.Draw(renderer);
		if (state.lanOptionsViewModel->mapSelectOpen.Get())
			state.lanMapMenu.Draw(renderer);
	}
	else if (state.Screen() == shell::Screen::Skirmish)
	{
		state.skirmishMenu.Draw(renderer);
		if (state.skirmishViewModel->mapSelectOpen.Get())
			state.mapSelectMenu.Draw(renderer);
	}
	if (state.model.optionsOpen.Get())
		state.optionsMenu.Draw(renderer);
	if (state.messages.open.Get())
		if (!state.messageBox.Draw(renderer))
			std::fprintf(stderr, "front end: the message box could not be drawn\n");
	// The transitions' flashes and fades over every window (GameWindowManager::winRepaint's last pass).
	state.overlays.Clear();
	if (state.transitions->Draw(state.overlays))
		state.overlayRenderer.Render_Draw_List(state.overlays, renderer);
}

bool FrontEnd::QuitRequested() const noexcept { return m_state->model.quitRequested.Get(); }

std::optional<session::setup::GameSetup> FrontEnd::TakeGameStart()
{
	auto start = std::move(m_state->gameStart);
	m_state->gameStart.reset();
	return start;
}

std::optional<FrontEnd::LanStart> FrontEnd::TakeLanStart()
{
	auto start = std::move(m_state->lanStart);
	m_state->lanStart.reset();
	return start;
}

std::optional<FrontEnd::CampaignStart> FrontEnd::TakeCampaignStart()
{
	auto start = std::move(m_state->campaignStart);
	m_state->campaignStart.reset();
	// The campaign screen is no screen of its own: the game over, the main menu shows again.
	if (start && m_state->model.requestedScreen.Get() == shell::Screen::Campaign)
		m_state->model.Pop();
	return start;
}

std::optional<std::filesystem::path> FrontEnd::TakeReplayRequest()
{
	auto request = std::move(m_state->replayRequest);
	m_state->replayRequest.reset();
	return request;
}

std::optional<std::filesystem::path> FrontEnd::TakeLoadRequest()
{
	auto request = std::move(m_state->loadRequest);
	m_state->loadRequest.reset();
	// The game over, the main menu shows again (not the load screen).
	if (request && m_state->model.requestedScreen.Get() == shell::Screen::LoadGame)
		m_state->model.Pop();
	return request;
}

std::filesystem::path FrontEnd::SaveFolder() const { return m_state->saveFolder; }

void FrontEnd::SetGameMenus(GameMenus menus)
{
	State &state = *m_state;
	state.gameMenus = std::move(menus);
	const auto text = [&state](const char *label) { return Localized(*state.strings, label); };
	shell::QuitMenuServices quit = state.gameMenus.quit;
	quit.openOptions = [&state] { state.model.optionsOpen.Set(true); };
	quit.openSaveLoad = [&state] {
		state.popupSaveViewModel->Populate(); // SaveLoadMenuInit
		state.popupSaveOpen = true;
	};
	shell::QuitMenuTexts texts;
	texts.quitTitle = text("GUI:QuitPopupTitle"), texts.quitMessage = text("GUI:QuitPopupMessage");
	texts.restartTitle = text("GUI:RestartConfirmationTitle"), texts.restartMessage = text("GUI:RestartConfirmation");
	texts.surrenderTitle = text("GUI:SurrenderConfirmationTitle"), texts.surrenderMessage = text("GUI:SurrenderConfirmation");
	texts.restartMission = text("GUI:RestartMission"), texts.exitMission = text("GUI:ExitMission"), texts.surrender = text("GUI:Surrender");
	texts.restartGame = text("GUI:RestartGame"), texts.exitGame = text("GUI:Exit");
	state.quitViewModel.emplace(state.messages, std::move(quit), std::move(texts));
	state.quitBindings.emplace(state.quitMenu.Document());
	shell::BindQuitMenuView(*state.quitBindings, *state.quitViewModel, "QuitMenu.wnd:");
	ReportMissing("quit menu", *state.quitBindings);
	state.quitNoSaveBindings.emplace(state.quitNoSaveMenu.Document());
	shell::BindQuitMenuView(*state.quitNoSaveBindings, *state.quitViewModel, "QuitNoSave.wnd:");
	ReportMissing("quit menu (multiplayer)", *state.quitNoSaveBindings);
	shell::SaveFiles files = state.saveFiles;
	files.save = [&state](const std::optional<std::string> &file, const std::u16string &description) {
		if (state.gameMenus.save)
			state.gameMenus.save(file, description);
	};
	files.defaultDescription = [&state] { return state.gameMenus.defaultDescription ? state.gameMenus.defaultDescription() : std::u16string{}; };
	files.close = [&state] { state.popupSaveOpen = false; };
	state.popupSaveViewModel.emplace(state.model, state.messages, std::move(files), state.saveTexts, shell::SaveLoadMode::SaveAndLoad);
	state.popupSaveBindings.emplace(state.popupSaveMenu.Document());
	shell::BindPopupSaveLoadView(*state.popupSaveBindings, *state.popupSaveViewModel);
	ReportMissing("save/load popup", *state.popupSaveBindings);
}

void FrontEnd::ToggleQuitMenu()
{
	State &state = *m_state;
	if (!state.inGame || !state.quitViewModel)
		return;
	if (state.model.optionsOpen.Get())
	{
		state.model.optionsOpen.Set(false); // the options menu's Back
		return;
	}
	if (state.popupSaveOpen)
	{
		state.popupSaveOpen = false; // PopupSaveLoad's Back
		return;
	}
	if (state.quitViewModel->open.Get())
		state.quitViewModel->Close();
	else
		state.quitViewModel->Open(state.gameMenus.mode ? state.gameMenus.mode() : shell::QuitMenuMode::SinglePlayer,
			!state.gameMenus.inputEnabled || state.gameMenus.inputEnabled(), state.gameMenus.beaten && state.gameMenus.beaten());
}

void FrontEnd::ShowScoreScreen(const shell::ScoreScreenSetup &setup)
{
	State &state = *m_state;
	state.scoreViewModel->Show(setup);
	state.model.Push(shell::Screen::ScoreScreen);
	// clearGameData: pushed with showShell(FALSE): it shows at once, no transition played out first.
	state.shownScreen = shell::Screen::ScoreScreen;
	state.leaving = false;
	state.look = true;
}

shell::ScoreChoice FrontEnd::TakeScoreChoice()
{
	State &state = *m_state;
	const shell::ScoreChoice choice = state.scoreViewModel->TakeChoice();
	if (choice != shell::ScoreChoice::None && state.model.requestedScreen.Get() == shell::Screen::ScoreScreen)
		state.model.Pop(); // TheShell->pop(): the main menu again
	return choice;
}

bool FrontEnd::GameMenuShown() const noexcept { return m_state->inGame && m_state->Active().first != nullptr; }

void FrontEnd::EnterGame() { m_state->inGame = true; }

void FrontEnd::LeaveGame()
{
	State &state = *m_state;
	state.inGame = false;
	if (state.quitViewModel)
		state.quitViewModel->Close();
	state.popupSaveOpen = false;
	state.model.optionsOpen.Set(false);
}

bool FrontEnd::InGame() const noexcept { return m_state->inGame; }
const shell::SetupCatalog &FrontEnd::SetupCatalog() const noexcept { return m_state->setupCatalog; }
}
