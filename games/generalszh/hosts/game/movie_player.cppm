export module games.generalszh.hosts.game.movie_player;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Video.FFmpeg.Player;
export import games.generalszh.content.global.videos;
export import engine.audio.mixing.mixer;

// The display's movie (the original's Display::playMovie / isMoviePlaying / stopMovie over its video player): one
// movie at a time, found as Open_Movie_File finds it (the language's Movies folder of the install, then the game's),
// played on real time, its picture stretched over the whole screen, its sound on the speech bus (the original's
// GameVideoAudioSink: a stream on AudioBus::Speech), converted to the mixer's stereo at its rate.
export namespace generalszh::host
{
// The movie's sound into the mixer: 16-bit chunks of any rate and channel count, as interleaved stereo float at the
// mixer's rate (linear), drained by one stream voice.
class MovieSound final : public Engine::Video::AudioSink
{
public:
	explicit MovieSound(engine::audio::Mixer &mixer) : m_mixer(mixer) {}
	~MovieSound() noexcept override { Stop(); }

	bool Submit(const Engine::Video::DecodedAudioChunk &chunk) override
	{
		if (!chunk.Is_Valid())
			return false;
		if (!m_stream)
			m_stream = std::make_shared<engine::audio::PcmStream>(static_cast<std::size_t>(m_mixer.SampleRate()) * 8);
		const std::size_t channels = chunk.channels;
		const std::size_t frames = chunk.samples.size() / (channels * 2);
		const auto sample = [&](std::size_t frame, std::size_t channel) {
			const std::size_t at = (frame * channels + std::min(channel, channels - 1)) * 2;
			const auto low = std::to_integer<std::uint16_t>(chunk.samples[at]);
			const auto high = std::to_integer<std::uint16_t>(chunk.samples[at + 1]);
			return static_cast<float>(static_cast<std::int16_t>(static_cast<std::uint16_t>(low | (high << 8)))) / 32768.0f;
		};
		const double step = static_cast<double>(chunk.sample_rate) / static_cast<double>(m_mixer.SampleRate());
		m_out.clear();
		for (; m_phase < static_cast<double>(frames); m_phase += step)
		{
			const auto index = static_cast<std::size_t>(m_phase);
			const float t = static_cast<float>(m_phase - static_cast<double>(index));
			for (std::size_t channel = 0; channel < 2; ++channel)
			{
				// Between the last frame of the chunk before and this chunk's first, the one kept from before.
				const float a = index == 0 && m_phase < 0.0 ? m_last[channel] : sample(index, channel);
				const float b = index + 1 < frames ? sample(index + 1, channel) : a;
				m_out.push_back(a + (b - a) * t);
			}
		}
		m_phase -= static_cast<double>(frames);
		if (frames > 0)
			m_last = {sample(frames - 1, 0), sample(frames - 1, 1)};
		m_queued += m_stream->Write(m_out);
		if (m_started && m_voice == 0)
			Begin();
		return true;
	}

	void Start() noexcept override
	{
		m_started = true;
		if (m_stream && m_voice == 0)
			Begin();
	}
	void Pause() noexcept override
	{
		m_started = false;
		if (m_voice != 0)
			m_mixer.SetGain(m_voice, 0.0f);
	}
	void Resume() noexcept override
	{
		m_started = true;
		if (m_voice != 0)
			m_mixer.SetGain(m_voice, 1.0f);
		else if (m_stream)
			Begin();
	}
	void Stop() noexcept override
	{
		m_started = false;
		if (m_voice != 0)
			m_mixer.Stop(m_voice);
		m_voice = 0;
		m_stream.reset();
		m_phase = 0.0;
		m_last = {};
	}
	void Reset() noexcept override { Stop(); }

	// Stereo frames handed to the mixer so far (checks).
	std::uint64_t Queued() const noexcept { return m_queued; }

private:
	void Begin()
	{
		engine::audio::VoiceStart start;
		start.stream = m_stream;
		start.bus = engine::audio::Bus::Speech;
		m_voice = m_mixer.Play(std::move(start));
	}

	engine::audio::Mixer &m_mixer;
	std::shared_ptr<engine::audio::PcmStream> m_stream;
	engine::audio::VoiceId m_voice{0};
	bool m_started{false};
	double m_phase{0.0};
	std::array<float, 2> m_last{};
	std::vector<float> m_out;
	std::uint64_t m_queued{0};
};

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

	// Its last frame over the whole `width` x `height` screen.
	void Draw(Graphics::Renderer2D &renderer, float width, float height)
	{
		if (m_pixels.empty() || m_width == 0 || m_height == 0)
			return;
		Engine::UI::WND::ImageRef picture;
		picture.generated = renderer.Register_Texture({Graphics::TextureHandle(0x7AD1001u, 1), m_width, m_height, m_pitch, m_revision,
			std::span<const std::byte>(m_pixels)});
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
