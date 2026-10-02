export module games.generalszh.hosts.game.movie_player;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Video.FFmpeg.Player;
import engine.audio.adapters.video.movie_sound;
export import games.generalszh.content.global.videos;
export import engine.audio.mixing.mixer;

// The display's movie (the original's Display::playMovie / isMoviePlaying / stopMovie over its video player): one
// movie at a time, found as Open_Movie_File finds it (the language's Movies folder of the install, then the game's),
// played on real time, its picture stretched over the whole screen, its sound on the speech bus (the original's
// GameVideoAudioSink: a stream on AudioBus::Speech), converted to the mixer's stereo at its rate.
export namespace generalszh::host
{
using engine::audio::MovieSound;

class MoviePlayer
{
public:
	// `mixer`: where its sound plays (none: silent).
	explicit MoviePlayer(engine::audio::Mixer *mixer = nullptr)
	{
		if (mixer != nullptr)
		{
			m_sound = std::make_unique<MovieSound>(*mixer);
			m_player.Set_Audio_Sink(m_sound.get());
		}
	}

	// Starts `movie` (a Video.ini name, or a file name without one); false when it cannot be found or opened.
	bool Play(const content::VideoCatalog &videos, const std::filesystem::path &install, std::string_view language, std::string_view movie)
	{
		Stop();
		if (movie.empty())
			return false;
		for (const std::string &path : videos.Paths(movie, language))
		{
			const std::filesystem::path file = install / path;
			std::error_code error;
			if (!std::filesystem::exists(file, error))
				continue;
			if (!m_player.Open(file.string()))
				continue;
			m_player.Set_Mode(Engine::Video::PlaybackMode::Once);
			m_player.Play();
			m_frames = m_player.Info().frame_count;
			m_open = true;
			return true;
		}
		return false;
	}

	void Stop() noexcept
	{
		if (m_open)
			m_player.Close();
		if (m_sound)
			m_sound->Stop();
		m_open = false;
		m_frame = 0;
		m_frames = 0;
	}

	// Display::isMoviePlaying.
	bool Playing() const noexcept { return m_open && m_player.State() == Engine::Video::PlaybackState::Playing; }

	// The movie on by `seconds` of real time.
	void Update(double seconds)
	{
		if (!m_open)
			return;
		m_player.Update(seconds);
		if (const Engine::Video::DecodedVideoFrame *frame = m_player.Current_Frame(); frame != nullptr && m_player.Is_Frame_Ready())
		{
			m_frame = frame->frame_index;
			m_width = frame->width;
			m_height = frame->height;
			m_pitch = frame->row_pitch;
			m_pixels.assign(frame->pixels.begin(), frame->pixels.end());
			++m_revision;
			m_player.Clear_Frame_Ready();
		}
	}

	// Sound frames handed to the mixer so far (checks).
	std::uint64_t SoundQueued() const noexcept { return m_sound ? m_sound->Queued() : 0; }

	// The frame shown and how many the movie has (1 when unknown).
	std::uint64_t Frame() const noexcept { return m_frame; }
	std::uint64_t FrameCount() const noexcept { return m_frames == 0 ? 1 : m_frames; }

	// Its last frame as an image for this frame's draws (a window's video buffer); none before the first.
	std::optional<Engine::UI::WND::ImageRef> FrameImage(Graphics::Renderer2D &renderer)
	{
		if (m_pixels.empty() || m_width == 0 || m_height == 0)
			return std::nullopt;
		Engine::UI::WND::ImageRef picture;
		picture.generated = renderer.Register_Texture({Graphics::TextureHandle(0x7AD1001u, 1), m_width, m_height, m_pitch, m_revision,
			std::span<const std::byte>(m_pixels)});
		return picture;
	}

	// Its last frame over the whole `width` x `height` screen.
	void Draw(Graphics::Renderer2D &renderer, float width, float height)
	{
		const auto image = FrameImage(renderer);
		if (!image)
			return;
		const Engine::UI::WND::ImageRef &picture = *image;
		m_list.Clear();
		m_list.Add_Image(picture, {0.0f, 0.0f, width, height});
		m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	std::unique_ptr<MovieSound> m_sound; // before the player, which feeds it
	Engine::Video::FFmpegPlayer m_player;
	bool m_open{false};
	std::uint64_t m_frame{0}, m_frames{0};
	std::uint32_t m_width{0}, m_height{0}, m_pitch{0};
	std::uint32_t m_revision{0};
	std::vector<std::byte> m_pixels;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
