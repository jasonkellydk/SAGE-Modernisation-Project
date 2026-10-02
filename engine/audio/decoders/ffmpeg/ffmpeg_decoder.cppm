export module engine.audio.decoders.ffmpeg.ffmpeg_decoder;
import std;

export import engine.audio.mixing.pcm;
export import engine.audio.playback.sound_player;

// FFmpeg decoder: audio files (WAV in any codec the game uses, including IMA ADPCM; MP3;
// anything FFmpeg reads) decoded from memory to interleaved stereo float at
// the mixer's rate. Short sounds decode whole; music and speech stream.
export namespace engine::audio
{
class AudioDecoder
{
public:
	// Null if the data is not a decodable audio file.
	static std::unique_ptr<AudioDecoder> Open(std::vector<std::byte> file, std::uint32_t outputRate);
	~AudioDecoder();
	AudioDecoder(const AudioDecoder &) = delete;
	AudioDecoder &operator=(const AudioDecoder &) = delete;

	// Up to `out.size() / 2` frames; 0 once the file is done.
	std::size_t Read(std::span<float> out);
	bool Ended() const noexcept;

private:
	struct State;
	explicit AudioDecoder(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};

// The whole file, decoded.
std::optional<PcmBuffer> DecodeAll(std::vector<std::byte> file, std::uint32_t outputRate);

// A stream kept `seconds` ahead by its decoder.
class DecoderFeed final : public StreamFeed
{
public:
	DecoderFeed(std::unique_ptr<AudioDecoder> decoder, std::uint32_t sampleRate, float seconds = 1.0f);
	std::shared_ptr<PcmStream> Output() override { return m_output; }
	void Pump() override;

private:
	std::unique_ptr<AudioDecoder> m_decoder;
	std::shared_ptr<PcmStream> m_output;
	std::vector<float> m_scratch;
	std::size_t m_pending{0}; // decoded frames in m_scratch not yet written
	std::size_t m_offset{0};
};
}
