export module games.renegade.presentation.menu.movie_audio;
export import engine.audio.adapters.video.movie_sound;

export namespace renegade::presentation {
// movie.cpp suppresses WWAudio while BINKMovie.cpp opens its independent
// output without applying WWAudio's cinematic volume or enabled flag.
// The modern host routes FFmpeg PCM through the shared mixer instead.
inline constexpr engine::audio::Bus MovieAudioBus=engine::audio::Bus::Media;
}
