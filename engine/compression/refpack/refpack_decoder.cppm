export module engine.compression.refpack.refpack_decoder;
import std;

// EA RefPack (the LZ77 variant used across EA formats of the era). Stream
// layout: 2-byte type (0x10FB/0x11FB/0x90FB/0x91FB, big-endian), then the
// decoded size (3 bytes, or 4 when bit 0x8000 is set; 0x100 prefixes an
// extra size field that is skipped), then commands until the end command.
// Unlike the original decoder, every read and back-reference is bounds
// checked, so corrupt input produces an error instead of memory corruption.
export namespace engine::compression::refpack
{
inline bool IsRefPack(std::span<const std::byte> data) noexcept
{
	if (data.size() < 2)
		return false;
	const unsigned type = (std::to_integer<unsigned>(data[0]) << 8) | std::to_integer<unsigned>(data[1]);
	return type == 0x10FB || type == 0x11FB || type == 0x90FB || type == 0x91FB;
}

inline std::expected<std::vector<std::byte>, std::string> Decode(std::span<const std::byte> data)
{
	if (!IsRefPack(data))
		return std::unexpected("not a RefPack stream");
	std::size_t at = 0;
	const auto next = [&](std::uint32_t &value) {
		if (at >= data.size())
			return false;
		value = std::to_integer<std::uint32_t>(data[at++]);
		return true;
	};
	std::uint32_t high = 0;
	std::uint32_t low = 0;
	next(high);
	next(low);
	const std::uint32_t type = (high << 8) | low;
	const std::size_t sizeBytes = (type & 0x8000) ? 4 : 3;
	if (type & 0x100)
		at += sizeBytes;
	std::uint64_t decodedSize = 0;
	for (std::size_t index = 0; index < sizeBytes; ++index)
	{
		std::uint32_t byte = 0;
		if (!next(byte))
			return std::unexpected("truncated RefPack header");
		decodedSize = (decodedSize << 8) | byte;
	}
	if (decodedSize > (std::uint64_t{1} << 31))
		return std::unexpected("RefPack decoded size is implausible");

	std::vector<std::byte> out;
	out.reserve(static_cast<std::size_t>(decodedSize));
	const auto literal = [&](std::uint32_t count) {
		if (count > data.size() - at || out.size() + count > decodedSize)
			return false;
		out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + count));
		at += count;
		return true;
	};
	const auto copy = [&](std::uint32_t distance, std::uint32_t count) {
		// distance counts back from the last written byte (0 = previous byte).
		if (distance >= out.size() || out.size() + count > decodedSize)
			return false;
		const std::size_t from = out.size() - 1 - distance;
		for (std::uint32_t index = 0; index < count; ++index)
			out.push_back(out[from + index]);
		return true;
	};

	for (;;)
	{
		std::uint32_t first = 0;
		std::uint32_t second = 0;
		std::uint32_t third = 0;
		std::uint32_t fourth = 0;
		if (!next(first))
			return std::unexpected("RefPack stream ends without an end command");
		bool ok = true;
		if (!(first & 0x80))
		{
			ok = next(second) && literal(first & 3) &&
				copy(((first & 0x60) << 3) + second, ((first & 0x1C) >> 2) + 3);
		}
		else if (!(first & 0x40))
		{
			ok = next(second) && next(third) && literal(second >> 6) &&
				copy(((second & 0x3F) << 8) + third, (first & 0x3F) + 4);
		}
		else if (!(first & 0x20))
		{
			ok = next(second) && next(third) && next(fourth) && literal(first & 3) &&
				copy((((first & 0x10) >> 4) << 16) + (second << 8) + third, (((first & 0x0C) >> 2) << 8) + fourth + 5);
		}
		else
		{
			const std::uint32_t run = ((first & 0x1F) << 2) + 4;
			if (run <= 112)
				ok = literal(run);
			else
			{
				if (!literal(first & 3))
					return std::unexpected("corrupt RefPack end command");
				break;
			}
		}
		if (!ok)
			return std::unexpected("corrupt RefPack command at byte " + std::to_string(at));
	}
	if (out.size() != decodedSize)
		return std::unexpected("RefPack stream decoded to " + std::to_string(out.size()) + " bytes, header says " + std::to_string(decodedSize));
	return out;
}
}
