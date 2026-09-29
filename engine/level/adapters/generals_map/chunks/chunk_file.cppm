export module engine.level.adapters.generals_map.chunks.chunk_file;
import std;

export import engine.level.model.properties;
import engine.compression.framing.compressed_block;

// The SAGE "chunky" container used by Generals maps:
//   [8-byte compression wrapper: "EAR\0" RefPack or "ZLn\0" zlib]   optional
//   "CkMp" i32 count { u8 length, name, u32 id }    name table
//   chunks: u32 id, u16 version, i32 size, data     (little-endian)
// Chunks nest: a parent's data contains child chunks wherever the format says.
export namespace engine::level::generals_map
{
namespace math = Engine::Math;

struct FormatError : std::runtime_error
{
	using std::runtime_error::runtime_error;
};

class ChunkFile;

struct ChunkHeader
{
	std::string_view name;
	std::uint16_t version{0};
	std::uint32_t size{0};
};

// Bounds-checked reader over one chunk's data (or the file body).
class ChunkCursor
{
public:
	ChunkCursor(const ChunkFile &file, std::span<const std::byte> data) noexcept : m_file(&file), m_data(data) {}

	bool AtEnd() const noexcept { return m_at >= m_data.size(); }
	std::size_t Remaining() const noexcept { return m_data.size() - m_at; }

	// Reads the next chunk header and returns a cursor over its data; this
	// cursor moves past the whole chunk.
	ChunkCursor OpenChunk(ChunkHeader &header);

	std::span<const std::byte> ReadBytes(std::size_t count)
	{
		Require(count);
		const auto bytes = m_data.subspan(m_at, count);
		m_at += count;
		return bytes;
	}

	std::uint8_t ReadU8() { return std::to_integer<std::uint8_t>(ReadBytes(1)[0]); }

	std::uint16_t ReadU16()
	{
		const auto b = ReadBytes(2);
		return static_cast<std::uint16_t>(std::to_integer<unsigned>(b[0]) | (std::to_integer<unsigned>(b[1]) << 8));
	}

	std::uint32_t ReadU32()
	{
		const auto b = ReadBytes(4);
		return std::to_integer<std::uint32_t>(b[0]) | (std::to_integer<std::uint32_t>(b[1]) << 8) |
			(std::to_integer<std::uint32_t>(b[2]) << 16) | (std::to_integer<std::uint32_t>(b[3]) << 24);
	}

	std::int32_t ReadI32() { return static_cast<std::int32_t>(ReadU32()); }

	// IEEE binary32 field, converted exactly (see Fixed::FromBinary32Bits).
	math::Fixed ReadFixed() { return math::Fixed::FromBinary32Bits(ReadU32()); }

	std::string ReadString()
	{
		const std::uint16_t length = ReadU16();
		const auto bytes = ReadBytes(length);
		return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	}

	// UTF-16LE with a u16 length in code units, returned as UTF-8.
	std::string ReadWideString()
	{
		const std::uint16_t length = ReadU16();
		std::string out;
		for (std::uint16_t index = 0; index < length; ++index)
		{
			const unsigned unit = ReadU16();
			if (unit < 0x80)
				out += static_cast<char>(unit);
			else if (unit < 0x800)
			{
				out += static_cast<char>(0xC0 | (unit >> 6));
				out += static_cast<char>(0x80 | (unit & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xE0 | (unit >> 12));
				out += static_cast<char>(0x80 | ((unit >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (unit & 0x3F));
			}
		}
		return out;
	}

	// Typed dictionary: u16 count { i32 (nameId << 8 | type), value }.
	Properties ReadProperties();

	std::string_view NameOf(std::uint32_t id) const;

private:
	void Require(std::size_t count) const
	{
		if (count > Remaining())
			throw FormatError("read of " + std::to_string(count) + " bytes past the end of a chunk");
	}

	const ChunkFile *m_file;
	std::span<const std::byte> m_data;
	std::size_t m_at{0};
};

class ChunkFile
{
public:
	// Accepts the raw file bytes, compressed or not.
	static std::expected<ChunkFile, std::string> Parse(std::span<const std::byte> bytes)
	{
		ChunkFile file;
		auto unwrapped = compression::UnwrapBlock(bytes);
		if (!unwrapped)
			return std::unexpected("compressed map: " + unwrapped.error());
		file.m_bytes = std::move(*unwrapped);
		try
		{
			ChunkCursor cursor(file, file.m_bytes);
			const auto tag = cursor.ReadBytes(4);
			if (std::memcmp(tag.data(), "CkMp", 4) != 0)
				return std::unexpected("not a chunk file (missing CkMp name table)");
			const std::int32_t count = cursor.ReadI32();
			if (count < 0)
				return std::unexpected("negative name table size");
			for (std::int32_t index = 0; index < count; ++index)
			{
				const std::uint8_t length = cursor.ReadU8();
				const auto name = cursor.ReadBytes(length);
				const std::uint32_t id = cursor.ReadU32();
				file.m_names[id] = std::string(reinterpret_cast<const char *>(name.data()), name.size());
			}
			file.m_bodyOffset = file.m_bytes.size() - cursor.Remaining();
		}
		catch (const FormatError &error)
		{
			return std::unexpected(std::string("name table: ") + error.what());
		}
		return file;
	}

	ChunkFile(ChunkFile &&) noexcept = default;
	ChunkFile &operator=(ChunkFile &&) noexcept = default;
	ChunkFile(const ChunkFile &) = delete;
	ChunkFile &operator=(const ChunkFile &) = delete;

	// Cursor over the top-level chunks. The file must outlive it.
	ChunkCursor Body() const noexcept { return ChunkCursor(*this, std::span(m_bytes).subspan(m_bodyOffset)); }

	std::string_view NameOf(std::uint32_t id) const
	{
		const auto found = m_names.find(id);
		if (found == m_names.end())
			throw FormatError("unknown name id " + std::to_string(id));
		return found->second;
	}

	std::size_t DecodedSize() const noexcept { return m_bytes.size(); }

private:
	ChunkFile() = default;

	std::vector<std::byte> m_bytes;
	std::map<std::uint32_t, std::string> m_names;
	std::size_t m_bodyOffset{0};
};

inline std::string_view ChunkCursor::NameOf(std::uint32_t id) const { return m_file->NameOf(id); }

inline ChunkCursor ChunkCursor::OpenChunk(ChunkHeader &header)
{
	header.name = NameOf(ReadU32());
	header.version = ReadU16();
	const std::int32_t size = ReadI32();
	if (size < 0 || static_cast<std::size_t>(size) > Remaining())
		throw FormatError("chunk '" + std::string(header.name) + "' runs past its parent");
	header.size = static_cast<std::uint32_t>(size);
	return ChunkCursor(*m_file, ReadBytes(header.size));
}

inline Properties ChunkCursor::ReadProperties()
{
	Properties properties;
	const std::uint16_t count = ReadU16();
	for (std::uint16_t index = 0; index < count; ++index)
	{
		const std::uint32_t keyAndType = ReadU32();
		std::string key(NameOf(keyAndType >> 8));
		switch (keyAndType & 0xFF)
		{
		case 0: properties.Set(std::move(key), ReadU8() != 0); break;
		case 1: properties.Set(std::move(key), static_cast<std::int64_t>(ReadI32())); break;
		case 2: properties.Set(std::move(key), ReadFixed()); break;
		case 3: properties.Set(std::move(key), ReadString()); break;
		case 4: properties.Set(std::move(key), ReadWideString()); break;
		default: throw FormatError("unknown property type " + std::to_string(keyAndType & 0xFF));
		}
	}
	return properties;
}
}
