export module engine.audio.adapters.xaudio2.xaudio2_output;
import std;

export import engine.audio.output.audio_output;

// XAudio2 adapter: the preferred output on Windows.
export namespace engine::audio
{
// The default endpoint; null if XAudio2 is unavailable.
std::unique_ptr<AudioOutput> OpenXAudio2Output(Mixer &mixer);
}
