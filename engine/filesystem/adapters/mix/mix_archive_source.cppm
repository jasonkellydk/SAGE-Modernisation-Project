export module engine.filesystem.adapters.mix.mix_archive_source;
import std;
export import engine.filesystem.core.virtual_file_system;

export namespace engine::filesystem::mix
{
// MIX1's key is the standard CRC32 of the ASCII upper-case filename.
inline std::uint32_t FilenameKey(std::string_view name) noexcept
{
	std::uint32_t crc = 0xFFFFFFFFu;
	for (unsigned char c : name)
	{
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		crc ^= c;
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
	}
	return crc ^ 0xFFFFFFFFu;
}
}

export namespace engine::filesystem
{
// Reusable Westwood MIX1 container adapter. Metadata only is retained;
// parallel reads use independent streams, as the BIG adapter does.
class MixArchiveSource final : public FileSource
{
public:
	static std::expected<std::unique_ptr<MixArchiveSource>, std::string> Open(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary);
		std::error_code error;
		const auto size = std::filesystem::file_size(path, error);
		if (!stream || error) return std::unexpected("cannot open MIX1: " + path.generic_string());
		const auto fail = [&]() -> std::expected<std::unique_ptr<MixArchiveSource>, std::string> {
			return std::unexpected("invalid MIX1 metadata: " + path.generic_string());
		};
		char magic[4]{};
		stream.read(magic, 4);
		const auto index = ReadU32(stream), names = ReadU32(stream);
		if (!stream || std::string_view(magic, 4) != "MIX1" || index < 12 || names < 12 || index > size || names > size)
			return fail();
		stream.seekg(index);
		const auto count = ReadU32(stream);
		if (!stream || count > (size - index) / 12 || std::uint64_t{index} + 4 + std::uint64_t{count} * 12 > names)
			return fail();
		std::map<std::uint32_t, Location> locations;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const auto key = ReadU32(stream), offset = ReadU32(stream), length = ReadU32(stream);
			if (!stream || offset < 12 || offset > index || length > index - offset || !locations.emplace(key, Location{offset, length}).second)
				return fail();
		}
		stream.seekg(names);
		if (ReadU32(stream) != count || !stream) return fail();
		auto source = std::unique_ptr<MixArchiveSource>(new MixArchiveSource(path));
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const int length = stream.get();
			if (length <= 0) return fail();
			std::string name(static_cast<std::size_t>(length), '\0');
			stream.read(name.data(), length);
			if (!stream || name.back() != '\0') return fail();
			name.pop_back();
			if (name.empty() || name.find('\0') != std::string::npos) return fail();
			const auto location = locations.find(mix::FilenameKey(name));
			if (location == locations.end()) return fail();
			const auto normalized = NormalizePath(name);
			if (!source->m_entries.emplace(normalized, location->second).second) return fail();
		}
		return source;
	}

	std::string_view Name() const noexcept override { return m_name; }
	std::vector<std::string> Paths() const override
	{
		std::vector<std::string> result;
		result.reserve(m_entries.size());
		for (const auto &[name, location] : m_entries) result.push_back(name);
		return result;
	}
	std::optional<std::vector<std::byte>> Read(std::string_view path) const override
	{
		const auto found = m_entries.find(NormalizePath(path));
		if (found == m_entries.end()) return std::nullopt;
		std::ifstream stream(m_path, std::ios::binary);
		if (!stream) return std::nullopt;
		std::vector<std::byte> bytes(found->second.size);
		stream.seekg(found->second.offset);
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream) return std::nullopt;
		return bytes;
	}
private:
	struct Location { std::uint32_t offset; std::uint32_t size; };
	static std::uint32_t ReadU32(std::istream &stream)
	{
		std::uint32_t result = 0;
		for (int i = 0; i < 4; ++i)
		{
			const int byte = stream.get();
			if (byte < 0) return 0;
			result |= static_cast<std::uint32_t>(byte) << (8 * i);
		}
		return result;
	}
	explicit MixArchiveSource(const std::filesystem::path &path) : m_path(path), m_name(path.generic_string()) {}
	std::filesystem::path m_path;
	std::string m_name;
	std::map<std::string, Location, std::less<>> m_entries;
};
}
