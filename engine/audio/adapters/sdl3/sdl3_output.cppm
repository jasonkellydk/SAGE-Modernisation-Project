export module engine.audio.adapters.sdl3.sdl3_output;
import std;

export import engine.audio.output.audio_output;

// SDL3 adapter: the portable output (the fallback on Windows).
export namespace engine::audio
{
// The default playback device; null if SDL has none.
std::unique_ptr<AudioOutput> OpenSdl3Output(Mixer &mixer);
}
