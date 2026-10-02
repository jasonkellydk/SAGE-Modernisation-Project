module;
#include <zlib.h>

export module engine.compression.zlib.zlib_decoder;
import std;

export namespace engine::compression::zlib
{
// Inflates a zlib stream whose decoded size is known up front.
inline std::expected<std::vector<std::byte>, std::string> Decode(std::span<const std::byte> data, std::size_t decodedSize)
{
	std::vector<std::byte> out(decodedSize);
	uLongf length = static_cast<uLongf>(decodedSize);
	const int status = ::uncompress(reinterpret_cast<Bytef *>(out.data()), &length,
		reinterpret_cast<const Bytef *>(data.data()), static_cast<uLong>(data.size()));
	if (status != Z_OK)
		return std::unexpected("zlib stream is corrupt (status " + std::to_string(status) + ")");
	if (length != decodedSize)
		return std::unexpected("zlib stream decoded to " + std::to_string(length) + " bytes, expected " + std::to_string(decodedSize));
	return out;
}
}
