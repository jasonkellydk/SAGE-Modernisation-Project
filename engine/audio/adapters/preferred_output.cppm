export module engine.audio.adapters.preferred_output;
import std;

export import engine.audio.output.audio_output;
#if defined(_WIN32)
import engine.audio.adapters.xaudio2.xaudio2_output;
#endif
import engine.audio.adapters.sdl3.sdl3_output;

// The platform's best output: XAudio2 on Windows, SDL3 elsewhere (and as
// Windows' fallback). Null when there is no audio at all (the game runs silent).
export namespace engine::audio
{
std::unique_ptr<AudioOutput> OpenPreferredOutput(Mixer &mixer)
{
#if defined(_WIN32)
	if (auto output = OpenXAudio2Output(mixer))
		return output;
#endif
	return OpenSdl3Output(mixer);
}
}
