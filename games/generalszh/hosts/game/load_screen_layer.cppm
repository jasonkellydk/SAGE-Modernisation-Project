module;
#include <cstdio>

export module games.generalszh.hosts.game.load_screen_layer;
import std;

export import games.generalszh.shell.load_screen.load_screen_kind;
export import games.generalszh.shell.load_screen.load_screen_view;
import games.generalszh.hosts.game.shell_menu;
import games.generalszh.content.loading.content_loader;
import games.generalszh.content.global.player_templates;
import games.generalszh.content.global.challenge_generals;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;
import engine.config.binding.values;
import engine.time.simulation_time;
import Engine.UI.WND;
import Engine.UI.WND.Document;
import Graphics.Frame.Runtime;
import Graphics.Renderer2D;
import games.generalszh.hosts.game.frame_draws;
import games.generalszh.hosts.game.movie_player;

// The load screen a game loads behind (GameLogic::m_loadScreen): its layout loaded and bound to its view model as the
// load begins (LoadScreen::init), and drawn on its own each time the load reports its progress (LoadScreen::update:
// TheDisplay->draw() while the game loop waits), until the game starts (deleteLoadScreen). Screen state lives in the
// view models (MVVM); this layer holds the layout and draws it.
export namespace generalszh::host
{
// PlayerTemplate.ini with ChallengeMode.ini's generals, as the multiplayer load screen shows the factions.
inline std::vector<shell::LoadScreenFaction> LoadScreenFactions(content::ContentLoader &loader, const content::PlayerTemplates &templates,
	const engine::localization::StringTable &strings)
{
	const engine::config::Document &challengeSet = loader.Load({"Data/INI/ChallengeMode"});
	const engine::config::Document &templateSet = loader.Load({"Data/INI/Default/PlayerTemplate", "Data/INI/PlayerTemplate"});
	engine::config::BindContext context{loader.DiagnosticsFor(challengeSet), engine::time::FixedStep{30}};
	const content::ChallengeGenerals generals = content::BindChallengeGenerals(challengeSet, &templateSet, context);
	std::vector<shell::LoadScreenFaction> factions;
	for (const content::PlayerTemplateInfo &info : templates.templates)
	{
		shell::LoadScreenFaction &faction = factions.emplace_back();
		faction.name = info.name;
		faction.displayName = Localized(strings, info.displayName);
		faction.features = info.features;
		faction.loadScreenMusic = info.loadScreenMusic;
		// ChallengeGenerals::getGeneralByTemplateName.
		for (const content::GeneralPersona &persona : generals)
			if (!persona.playerTemplate.empty() && persona.playerTemplate == info.name)
			{
				faction.general = true;
				faction.generalPortrait = persona.bioPortraitLarge;
				faction.generalName = persona.bioName;
				break;
			}
	}
	return factions;
}

// How a load screen sounds (LoadScreen.cpp's TheAudio calls), played at once by the host's sound player (the load holds
// the game loop, and the match it starts replaces the world whose systems would play them).
struct LoadScreenAudio
{
	std::function<std::uint64_t(std::string_view)> play; // addAudioEvent; 0: not playing
	std::function<void(std::uint64_t, bool)> stop;       // removeAudioEvent (true: faded as AHSV_StopTheMusicFade)
	std::function<void()> fadeMusic;                     // removeAudioEvent(AHSV_StopTheMusicFade) on the music playing
	std::function<void()> update;                        // TheAudio->update
};

class LoadScreenLayer
{
public:
	void SetAudio(LoadScreenAudio audio) { m_audio = std::move(audio); }

	// LoadScreen::init: the screen `kind` shows, its view model set up by `init` (once bound, so the view shows it).
	bool Show(shell::LoadScreenKind kind, const engine::filesystem::VirtualFileSystem &files, const engine::localization::StringTable &strings,
		std::uint32_t width, std::uint32_t height, float fontScale, const std::function<void(LoadScreenLayer &)> &init)
	{
		Close();
		m_movieFrame = 0;
		const char *layout = kind == shell::LoadScreenKind::ShellGame ? "Window/Menus/ShellGameLoadScreen.wnd"
			: kind == shell::LoadScreenKind::SinglePlayer             ? "Window/Menus/SinglePlayerLoadScreen.wnd"
			: kind == shell::LoadScreenKind::Challenge                ? "Window/Menus/ChallengeLoadScreen.wnd"
			: kind == shell::LoadScreenKind::MultiPlayer              ? "Window/Menus/MultiplayerLoadScreen.wnd"
																	  : nullptr;
		if (layout == nullptr)
			return false;
		auto menu = std::make_unique<ShellMenu>();
		std::string error;
		if (!menu->Load(files, layout, strings, width, height, error, fontScale))
		{
			std::fprintf(stderr, "load screen: %s\n", error.c_str());
			return false;
		}
		m_menu = std::move(menu);
		m_kind = kind;
		m_bindings.emplace(m_menu->Document());
		ShellMenu &shown = *m_menu;
		const shell::LoadScreenImageResolve resolve = [&shown](std::string_view name) { return shown.Image(name); };
		const shell::LoadScreenImageSize size = [&shown](std::string_view name) { return shown.ImageSize(name); };
		if (kind == shell::LoadScreenKind::ShellGame)
			shell::BindShellGameLoadScreenView(*m_bindings, shellGame, resolve, size);
		else if (kind == shell::LoadScreenKind::MultiPlayer)
			m_multiplayerView.emplace(*m_bindings, m_menu->Document(), multiplayer, resolve, size);
		else
			m_missionView.emplace(*m_bindings, mission, kind == shell::LoadScreenKind::Challenge, resolve, size);
		if (init)
			init(*this);
		// MultiPlayerLoadScreen::init: the local player's faction's LoadScreenMusic, after the music playing fades.
		if (shell::LoadScreenPlaysMusic(kind) && !multiplayer.music.Get().empty() && m_audio.play)
		{
			if (m_audio.fadeMusic)
				m_audio.fadeMusic();
			m_music = m_audio.play(multiplayer.music.Get());
		}
		Present();
		return true;
	}

	// LoadScreen::update: the load's percent on the screen, drawn at once.
	void Update(int percent)
	{
		if (!m_menu)
			return;
		// The original's sound runs on its own (Miles); here the host's player is sequenced at each step of the load.
		if (m_audio.update)
			m_audio.update();
		if (m_kind == shell::LoadScreenKind::ShellGame)
			shellGame.Update(percent);
		else if (m_kind == shell::LoadScreenKind::MultiPlayer)
			multiplayer.Update(percent);
		else
			mission.Update(percent);
		// A host run's look at each screen (--load-screen-shots): the frame at the assets' preload.
		const bool shot = m_capture != nullptr && percent == shell::load_progress::PostPreloadAssets;
		std::filesystem::path previous;
		if (shot)
		{
			previous = std::exchange(m_capture->captureFile, m_shots / std::format("load_screen_{}_{}.png", static_cast<int>(m_kind), m_shotCount++));
			m_capture->captureNextFrame = true;
		}
		Present();
		if (shot)
		{
			std::println("load screen: {}{}", m_capture->captureFile.string(), m_capture->captured ? "" : " (not captured)");
			m_capture->captureFile = std::move(previous);
			m_capture->captureNextFrame = false;
			m_capture->captured = false;
		}
	}

	// A mission's movie playing in the screen's background window (SinglePlayerLoadScreen / ChallengeLoadScreen::init's
	// loop): each frame decoded since the last (`frameIndex`: the frame shown, 0 first) steps the bar; `playing` false:
	// the movie is over, the background shows (setVideoBuffer(NULL)).
	void MovieProgress(std::uint64_t frameIndex, bool playing)
	{
		if (!m_menu || (m_kind != shell::LoadScreenKind::SinglePlayer && m_kind != shell::LoadScreenKind::Challenge))
			return;
		for (; m_movieFrame <= frameIndex; ++m_movieFrame)
			mission.MovieFrame(static_cast<int>(m_movieFrame + 1)); // the stream's index after frameNext
		if (m_kind == shell::LoadScreenKind::Challenge)
			UpdateWindowMovies(playing);
		if (!playing)
		{
			mission.EndMovie();
			// SinglePlayerLoadScreen / ChallengeLoadScreen::init's end (after the challenge's taunt): the ambient loop.
			if (!m_ambientStarted && !shell::LoadScreenAmbientFor(m_kind).empty() && m_audio.play)
			{
				m_ambientStarted = true;
				m_ambient = m_audio.play(shell::LoadScreenAmbientFor(m_kind));
			}
		}
	}

	// Where the challenge screen's window movies (its portraits and versus overlay) are found (Video.ini, the install).
	void SetWindowMovieSource(const content::VideoCatalog &videos, std::filesystem::path install)
	{
		m_videos = &videos;
		m_install = std::move(install);
	}

	// Where the background window's movie frame comes from (the display's movie) while the screen shows one.
	void SetVideoSource(std::function<std::optional<Engine::UI::WND::ImageRef>(Graphics::Renderer2D &)> source) { m_videoSource = std::move(source); }

	// The screen within the host's own frame (a movie playing in it).
	void Draw(Graphics::Renderer2D &renderer)
	{
		if (!m_menu)
			return;
		SetVideo(renderer);
		if (m_menu->Refresh())
			m_menu->Draw(renderer);
	}

	// Each screen's frame written to `directory` as its load preloads its assets.
	void CaptureTo(FrameCapture &capture, std::filesystem::path directory)
	{
		m_capture = &capture;
		m_shots = std::move(directory);
	}

	// GameLogic::deleteLoadScreen.
	void Close()
	{
		for (WindowMovie &movie : m_windowMovies)
		{
			movie.player.reset();
			movie.started.clear();
		}
		m_windowClock.reset();
		// The screen's destructor: the ambient loop removed; the multiplayer screen's music faded
		// (removeAudioEvent(AHSV_StopTheMusicFade)).
		if (m_audio.stop)
		{
			if (m_ambient != 0)
				m_audio.stop(m_ambient, false);
			if (m_music != 0)
				m_audio.stop(m_music, true);
		}
		m_ambient = 0;
		m_music = 0;
		m_ambientStarted = false;
		m_missionView.reset();
		m_multiplayerView.reset();
		m_bindings.reset();
		m_menu.reset();
		m_videoSource = nullptr;
		m_kind = shell::LoadScreenKind::None;
	}

	bool Shown() const noexcept { return m_menu != nullptr; }
	// A mapped image of that name exists (TheMappedImageCollection->findImageByName).
	bool ImageKnown(std::string_view name) const { return m_menu && m_menu->ImageSize(name).has_value(); }
	shell::LoadScreenKind Kind() const noexcept { return m_kind; }

	// One frame of the screen alone (W3DDisplay::draw with m_loadScreenRender: the windows, no world).
	void Present()
	{
		if (!m_menu || !Graphics::Graphics_Begin_Frame())
			return;
		SetVideo(Graphics::Get_Renderer2D());
		if (m_menu->Refresh())
			m_menu->Draw(Graphics::Get_Renderer2D());
		if (!(Graphics::Graphics_Execute_Queued_Draws() && Graphics::Graphics_End_Frame() && Graphics::Graphics_Present()))
			Graphics::Graphics_Abort_Frame();
	}

	shell::ShellGameLoadScreenViewModel shellGame;
	shell::MissionLoadScreenViewModel mission;
	shell::MultiplayerLoadScreenViewModel multiplayer;
	// ShellGameLoadScreen::init's static firstLoad: the title screen shows once.
	bool shellLoadedOnce{false};

private:
	// The background window's video (this frame's registration of the movie's frame), while the screen shows it.
	void SetVideo(Graphics::Renderer2D &renderer)
	{
		std::optional<Engine::UI::WND::ImageRef> video;
		if (m_videoSource && (m_kind == shell::LoadScreenKind::SinglePlayer || m_kind == shell::LoadScreenKind::Challenge) && mission.movieShown.Get())
			video = m_videoSource(renderer);
		const char *parent = m_kind == shell::LoadScreenKind::Challenge ? "ChallengeLoadScreen.wnd:ParentChallengeLoadScreen"
																		: "SinglePlayerLoadScreen.wnd:ParentSinglePlayerLoadScreen";
		if (Engine::UI::WND::WNDWindow *window = m_menu ? m_menu->Document().Find_Window(parent) : nullptr)
			window->video = video;
		if (m_kind != shell::LoadScreenKind::Challenge || !m_menu)
			return;
		for (WindowMovie &movie : m_windowMovies)
			if (Engine::UI::WND::WNDWindow *window = m_menu->Document().Find_Window(movie.window))
				window->video = movie.player ? movie.player->FrameImage(renderer) : std::nullopt;
	}

	// ChallengeLoadScreen's WindowVideoManager: each window's movie starts when the screen asks for it
	// (WINDOW_PLAY_MOVIE_SHOW_LAST_FRAME: once, its last frame kept), and is updated, on real time, only while the
	// mission's movie plays (m_wndVideoManager->update in init's loop), so it stays as it was over the load.
	void UpdateWindowMovies(bool playing)
	{
		const auto now = std::chrono::steady_clock::now();
		const double seconds = m_windowClock ? std::chrono::duration<double>(now - *m_windowClock).count() : 0.0;
		m_windowClock = now;
		const std::array<const engine::gui::mvvm::Observable<std::string> *, 3> asked{&mission.portraitMovieLeft, &mission.portraitMovieRight,
			&mission.versusMovie};
		for (std::size_t index = 0; index < m_windowMovies.size(); ++index)
		{
			WindowMovie &movie = m_windowMovies[index];
			const std::string &name = asked[index]->Get();
			if (!name.empty() && name != movie.started && m_videos != nullptr)
			{
				movie.started = name;
				movie.player = std::make_unique<MoviePlayer>(nullptr, movie.texture);
				if (!movie.player->Play(*m_videos, m_install, "English", name))
					std::fprintf(stderr, "load screen: movie '%s' could not be played\n", name.c_str());
			}
			if (playing && movie.player)
				movie.player->Update(seconds);
		}
	}

	std::unique_ptr<ShellMenu> m_menu;
	std::optional<Engine::UI::WND::WNDBindings> m_bindings;
	std::optional<shell::MissionLoadScreenView> m_missionView;
	std::optional<shell::MultiplayerLoadScreenView> m_multiplayerView;
	shell::LoadScreenKind m_kind{shell::LoadScreenKind::None};
	FrameCapture *m_capture{nullptr};
	std::filesystem::path m_shots;
	int m_shotCount{0};
	std::uint64_t m_movieFrame{0}; // the next movie frame to step the bar for
	std::function<std::optional<Engine::UI::WND::ImageRef>(Graphics::Renderer2D &)> m_videoSource;
	// The challenge screen's window movies: PortraitMovieLeft, PortraitMovieRight, OverlayVs.
	struct WindowMovie
	{
		const char *window;
		std::uint32_t texture;
		std::string started;
		std::unique_ptr<MoviePlayer> player;
	};
	std::array<WindowMovie, 3> m_windowMovies{{{"ChallengeLoadScreen.wnd:PortraitMovieLeft", 0x7AD1002u, {}, {}},
		{"ChallengeLoadScreen.wnd:PortraitMovieRight", 0x7AD1003u, {}, {}}, {"ChallengeLoadScreen.wnd:OverlayVs", 0x7AD1004u, {}, {}}}};
	std::optional<std::chrono::steady_clock::time_point> m_windowClock;
	const content::VideoCatalog *m_videos{nullptr};
	std::filesystem::path m_install;
	LoadScreenAudio m_audio;
	std::uint64_t m_ambient{0}, m_music{0};
	bool m_ambientStarted{false};
};
}
