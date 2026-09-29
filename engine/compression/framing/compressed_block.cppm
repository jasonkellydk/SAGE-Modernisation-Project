export module engine.compression.framing.compressed_block;
import std;

import engine.compression.refpack.refpack_decoder;
import engine.compression.zlib.zlib_decoder;

// The 8-byte compression wrapper EA titles of this era put in front of data
// (maps, saves): a 4-byte tag, a u32 LE decoded size, then the payload.
//   "EAR\0"            RefPack
//   "ZL1\0".."ZL9\0"   zlib (the digit is the level used to compress)
//   "NOX\0"            LZH (not supported: no shipped content uses it)
// Data without a known tag is returned unchanged.
export namespace engine::compression
{
enum class BlockCodec
{
	None,
	RefPack,
	Zlib,
	Lzh
};

inline BlockCodec DetectBlockCodec(std::span<const std::byte> data) noexcept
{
	if (data.size() < 8)
		return BlockCodec::None;
	const char *tag = reinterpret_cast<const char *>(data.data());
	if (std::memcmp(tag, "EAR\0", 4) == 0)
		return BlockCodec::RefPack;
	if (tag[0] == 'Z' && tag[1] == 'L' && tag[2] >= '1' && tag[2] <= '9' && tag[3] == '\0')
		return BlockCodec::Zlib;
	if (std::memcmp(tag, "NOX\0", 4) == 0)
		return BlockCodec::Lzh;
	return BlockCodec::None;
}

inline std::expected<std::vector<std::byte>, std::string> UnwrapBlock(std::span<const std::byte> data)
{
	const BlockCodec codec = DetectBlockCodec(data);
	if (codec == BlockCodec::None)
		return std::vector<std::byte>(data.begin(), data.end());
	const std::size_t size = std::to_integer<std::size_t>(data[4]) | (std::to_integer<std::size_t>(data[5]) << 8) |
		(std::to_integer<std::size_t>(data[6]) << 16) | (std::to_integer<std::size_t>(data[7]) << 24);
	const auto payload = data.subspan(8);
	switch (codec)
	{
	case BlockCodec::RefPack:
	{
		auto decoded = refpack::Decode(payload);
		if (decoded && decoded->size() != size)
			return std::unexpected("RefPack block size does not match its wrapper");
		return decoded;
	}
	case BlockCodec::Zlib:
		return zlib::Decode(payload, size);
	default:
		return std::unexpected("LZH (NOX) compressed data is not supported");
	}
}
}
