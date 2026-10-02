export module engine.audio.adapters.video.movie_sound;
import std;
import Video.Playback;
export import engine.audio.mixing.mixer;

export namespace engine::audio
{
// The movie's sound into the mixer: 16-bit chunks of any rate and channel count, as interleaved stereo float at the
// mixer's rate (linear), drained by one stream voice.
class MovieSound final : public Engine::Video::AudioSink
{
public:
	explicit MovieSound(engine::audio::Mixer &mixer,Bus bus=Bus::Speech) : m_mixer(mixer),m_bus(bus) {}
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
		start.bus = m_bus;
		m_voice = m_mixer.Play(std::move(start));
	}

	engine::audio::Mixer &m_mixer;
	Bus m_bus;
	std::shared_ptr<engine::audio::PcmStream> m_stream;
	engine::audio::VoiceId m_voice{0};
	bool m_started{false};
	double m_phase{0.0};
	std::array<float, 2> m_last{};
	std::vector<float> m_out;
	std::uint64_t m_queued{0};
};

}
