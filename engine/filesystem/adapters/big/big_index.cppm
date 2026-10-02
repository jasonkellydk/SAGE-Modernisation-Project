export module engine.filesystem.adapters.big.big_index;
import std;

export import engine.filesystem.core.path;

// The .big archive layout used by Generals / Zero Hour:
//   0x00  "BIGF"
//   0x04  u32 LE  archive size
//   0x08  u32 BE  file count
//   0x0C  u32 BE  offset of the first file's data (end of the index)
//   0x10  per file: u32 BE offset, u32 BE size, zero-terminated path
export namespace engine::filesystem::big
{
constexpr std::size_t HeaderSize = 16;

struct Header
{
	std::uint32_t archiveSize{0};
	std::uint32_t fileCount{0};
	std::uint32_t indexEnd{0};
};

struct Entry
{
	std::string path; // normalized
	std::uint32_t offset{0};
	std::uint32_t size{0};
};

inline std::uint32_t ReadBigEndian(std::span<const std::byte> bytes, std::size_t at) noexcept
{
	return (std::to_integer<std::uint32_t>(bytes[at]) << 24) | (std::to_integer<std::uint32_t>(bytes[at + 1]) << 16) |
		(std::to_integer<std::uint32_t>(bytes[at + 2]) << 8) | std::to_integer<std::uint32_t>(bytes[at + 3]);
}

inline std::uint32_t ReadLittleEndian(std::span<const std::byte> bytes, std::size_t at) noexcept
{
	return std::to_integer<std::uint32_t>(bytes[at]) | (std::to_integer<std::uint32_t>(bytes[at + 1]) << 8) |
		(std::to_integer<std::uint32_t>(bytes[at + 2]) << 16) | (std::to_integer<std::uint32_t>(bytes[at + 3]) << 24);
}

inline std::expected<Header, std::string> ParseHeader(std::span<const std::byte> bytes)
{
	if (bytes.size() < HeaderSize)
		return std::unexpected("truncated header");
	if (bytes[0] != std::byte{'B'} || bytes[1] != std::byte{'I'} || bytes[2] != std::byte{'G'} || bytes[3] != std::byte{'F'})
		return std::unexpected("not a BIGF archive");
	Header header{ReadLittleEndian(bytes, 4), ReadBigEndian(bytes, 8), ReadBigEndian(bytes, 12)};
	if (header.indexEnd < HeaderSize)
		return std::unexpected("index end lies inside the header");
	return header;
}

// Parses the index from the archive's first `header.indexEnd` bytes. Every
// entry must lie inside the real file (`fileSize`); the header's own size
// field is not trusted, as some tools write it wrong.
inline std::expected<std::vector<Entry>, std::string> ParseIndex(std::span<const std::byte> bytes, std::uint64_t fileSize)
{
	const auto header = ParseHeader(bytes);
	if (!header)
		return std::unexpected(header.error());
	if (bytes.size() < header->indexEnd)
		return std::unexpected("truncated index");
	std::vector<Entry> entries;
	entries.reserve(header->fileCount);
	std::size_t at = HeaderSize;
	for (std::uint32_t file = 0; file < header->fileCount; ++file)
	{
		if (at + 8 > header->indexEnd)
			return std::unexpected("index entry " + std::to_string(file) + " runs past the index");
		Entry entry;
		entry.offset = ReadBigEndian(bytes, at);
		entry.size = ReadBigEndian(bytes, at + 4);
		at += 8;
		std::string raw;
		while (at < header->indexEnd && bytes[at] != std::byte{0})
			raw += static_cast<char>(std::to_integer<unsigned char>(bytes[at++]));
		if (at >= header->indexEnd)
			return std::unexpected("unterminated path in index entry " + std::to_string(file));
		++at;
		if (static_cast<std::uint64_t>(entry.offset) + entry.size > fileSize)
			return std::unexpected("'" + raw + "' lies outside the archive");
		entry.path = NormalizePath(raw);
		entries.push_back(std::move(entry));
	}
	return entries;
}
}
