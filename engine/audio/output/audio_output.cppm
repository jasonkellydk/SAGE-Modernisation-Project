export module engine.audio.output.audio_output;
import std;

export import engine.audio.mixing.mixer;

// Where mixed sound goes: an adapter over a platform audio API that pulls
// blocks from a mixer on its own audio thread for as long as it lives. The
// mixer must outlive it. Adapters live in engine/audio/adapters.
export namespace engine::audio
{
class AudioOutput
{
public:
	// Every adapter runs the mixer at this rate.
	static constexpr std::uint32_t SampleRate = 48000;

	virtual ~AudioOutput() = default;
	virtual std::string_view Name() const noexcept = 0;
};
}
