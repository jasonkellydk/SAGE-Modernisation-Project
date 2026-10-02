export module engine.core.serialization.byte_stream;
import std;

// Deterministic binary encoding: fixed-width little-endian integers, and
// length-prefixed strings and blobs. The same bytes on every platform, so a
// message or checkpoint encodes identically for loopback, LAN and replays.
export namespace engine::core::serialization
{
class ByteWriter
{
public:
	void U8(std::uint8_t value) { m_bytes.push_back(static_cast<std::byte>(value)); }
	void U32(std::uint32_t value) { Little(value, 4); }
	void U64(std::uint64_t value) { Little(value, 8); }
	void I64(std::int64_t value) { Little(static_cast<std::uint64_t>(value), 8); }
	void Flag(bool value) { U8(value ? 1 : 0); }

	void Text(std::string_view text)
	{
		U32(static_cast<std::uint32_t>(text.size()));
		for (const char c : text)
			m_bytes.push_back(static_cast<std::byte>(c));
	}

	void Blob(std::span<const std::byte> bytes)
	{
		U32(static_cast<std::uint32_t>(bytes.size()));
		m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
	}

	// Raw bytes, no length prefix (the reader knows the size).
	void Raw(std::span<const std::byte> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }

	const std::vector<std::byte> &Bytes() const noexcept { return m_bytes; }
	std::vector<std::byte> Take() noexcept { return std::move(m_bytes); }

private:
	void Little(std::uint64_t value, int width)
	{
		for (int index = 0; index < width; ++index)
			m_bytes.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFu));
	}

	std::vector<std::byte> m_bytes;
};

// Reads what ByteWriter wrote; any read past the end fails the reader (all
// later reads return empty) instead of reading garbage.
class ByteReader
{
public:
	explicit ByteReader(std::span<const std::byte> bytes) noexcept : m_bytes(bytes) {}

	std::optional<std::uint8_t> U8() { return Little<std::uint8_t>(1); }
	std::optional<std::uint32_t> U32() { return Little<std::uint32_t>(4); }
	std::optional<std::uint64_t> U64() { return Little<std::uint64_t>(8); }

	std::optional<std::int64_t> I64()
	{
		const auto value = Little<std::uint64_t>(8);
		return value ? std::optional(static_cast<std::int64_t>(*value)) : std::nullopt;
	}

	std::optional<bool> Flag()
	{
		const auto value = U8();
		return value ? std::optional(*value != 0) : std::nullopt;
	}

	std::optional<std::string> Text()
	{
		const auto size = U32();
		if (!size || !Has(*size))
			return Fail<std::string>();
		std::string text(reinterpret_cast<const char *>(m_bytes.data() + m_offset), *size);
		m_offset += *size;
		return text;
	}

	std::optional<std::vector<std::byte>> Blob()
	{
		const auto size = U32();
		if (!size || !Has(*size))
			return Fail<std::vector<std::byte>>();
		std::vector<std::byte> blob(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
			m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + *size));
		m_offset += *size;
		return blob;
	}

	// Exactly `destination.size()` raw bytes (what Raw wrote).
	bool Raw(std::span<std::byte> destination)
	{
		if (!Has(destination.size()))
			return Fail<bool>().has_value();
		std::memcpy(destination.data(), m_bytes.data() + m_offset, destination.size());
		m_offset += destination.size();
		return true;
	}

	bool Failed() const noexcept { return m_failed; }
	bool AtEnd() const noexcept { return !m_failed && m_offset == m_bytes.size(); }

private:
	bool Has(std::size_t count) const noexcept { return !m_failed && m_bytes.size() - m_offset >= count; }

	template<typename T>
	std::optional<T> Fail()
	{
		m_failed = true;
		return std::nullopt;
	}

	template<typename T>
	std::optional<T> Little(int width)
	{
		if (!Has(static_cast<std::size_t>(width)))
			return Fail<T>();
		std::uint64_t value = 0;
		for (int index = 0; index < width; ++index)
			value |= static_cast<std::uint64_t>(m_bytes[m_offset + static_cast<std::size_t>(index)]) << (8 * index);
		m_offset += static_cast<std::size_t>(width);
		return static_cast<T>(value);
	}

	std::span<const std::byte> m_bytes;
	std::size_t m_offset{0};
	bool m_failed{false};
};
}
