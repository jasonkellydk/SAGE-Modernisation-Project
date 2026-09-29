module;
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

module engine.audio.adapters.sdl3.sdl3_output;

// An audio stream on the default playback device whose callback asks the
// mixer for exactly the bytes the device wants.
namespace engine::audio
{
namespace
{
class Sdl3Output final : public AudioOutput
{
public:
	explicit Sdl3Output(Mixer &mixer) : m_mixer(mixer) {}

	~Sdl3Output() override
	{
		if (m_stream != nullptr)
			SDL_DestroyAudioStream(m_stream);
		if (m_initialized)
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}

	bool Open()
	{
		m_initialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
		if (!m_initialized)
			return false;
		const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(SampleRate)};
		m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Pull, this);
		return m_stream != nullptr && SDL_ResumeAudioStreamDevice(m_stream);
	}

	std::string_view Name() const noexcept override { return "SDL3"; }

private:
	static void SDLCALL Pull(void *userdata, SDL_AudioStream *stream, int additional, int)
	{
		auto &self = *static_cast<Sdl3Output *>(userdata);
		if (additional <= 0)
			return;
		const std::size_t floats = static_cast<std::size_t>(additional) / sizeof(float);
		self.m_block.resize(floats - floats % 2);
		self.m_mixer.Mix(self.m_block);
		SDL_PutAudioStreamData(stream, self.m_block.data(), static_cast<int>(self.m_block.size() * sizeof(float)));
	}

	Mixer &m_mixer;
	SDL_AudioStream *m_stream{nullptr};
	bool m_initialized{false};
	std::vector<float> m_block;
};
}

std::unique_ptr<AudioOutput> OpenSdl3Output(Mixer &mixer)
{
	auto output = std::make_unique<Sdl3Output>(mixer);
	if (!output->Open())
		return nullptr;
	return output;
}
}
