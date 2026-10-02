export module engine.audio.definitions.wave_length;
import std;

// How long a RIFF WAVE file plays, from its header (the original's AIL_WAV_info: sample frames and rate): PCM frames
// are the data's bytes over the block size; compressed ones (IMA ADPCM) take the 'fact' chunk's frame count, else
// whole blocks times the frames each block holds. None when the file is not a wave file it can read.
export namespace engine::audio
{
struct WaveLength
{
	std::uint64_t frames{0};
	std::uint32_t rate{0};

	// Whole microseconds (truncated).
	std::uint64_t Microseconds() const noexcept { return rate == 0 ? 0 : frames * 1'000'000 / rate; }
};

inline std::optional<WaveLength> ReadWaveLength(std::span<const std::byte> file)
{
	const auto u16 = [&](std::size_t at) {
		return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(file[at])) | static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(file[at + 1])) << 8;
	};
	const auto u32 = [&](std::size_t at) { return u16(at) | u16(at + 2) << 16; };
	const auto tag = [&](std::size_t at, std::string_view name) {
		for (std::size_t index = 0; index < 4; ++index)
			if (std::to_integer<char>(file[at + index]) != name[index])
				return false;
		return true;
	};
	if (file.size() < 12 || !tag(0, "RIFF") || !tag(8, "WAVE"))
		return std::nullopt;
	std::uint32_t format = 0, rate = 0, blockAlign = 0, samplesPerBlock = 0;
	std::optional<std::uint64_t> factFrames, dataBytes;
	for (std::size_t at = 12; at + 8 <= file.size();)
	{
		const std::uint32_t size = u32(at + 4);
		const std::size_t body = at + 8;
		if (tag(at, "fmt ") && body + 16 <= file.size())
		{
			format = u16(body);
			rate = u32(body + 4);
			blockAlign = u16(body + 12);
			// WAVEFORMATEX's cbSize, then IMA ADPCM's frames per block.
			if (size >= 20 && body + 20 <= file.size())
				samplesPerBlock = u16(body + 18);
		}
		else if (tag(at, "fact") && body + 4 <= file.size())
			factFrames = u32(body);
		else if (tag(at, "data"))
			dataBytes = std::min<std::uint64_t>(size, file.size() - body);
		at = body + size + (size & 1u);
	}
	if (rate == 0 || blockAlign == 0 || !dataBytes)
		return std::nullopt;
	if (format == 1) // PCM
		return WaveLength{*dataBytes / blockAlign, rate};
	if (factFrames)
		return WaveLength{*factFrames, rate};
	if (samplesPerBlock != 0)
		return WaveLength{*dataBytes / blockAlign * samplesPerBlock, rate};
	return std::nullopt;
}
}
