export module engine.filesystem.adapters.big.big_archive_source;
import std;

export import engine.filesystem.core.virtual_file_system;
export import engine.filesystem.adapters.big.big_index;

export namespace engine::filesystem
{
// Files inside one .big archive. The index is read once; file data is read on
// demand, each read through its own handle opened for it (reads are rare and
// large, and callers cache what they decode): no handle stays open between
// reads, so any number of mounted archives stay within the C runtime's limit
// on open streams (512), and reads on several threads never wait on each other.
class BigArchiveSource final : public FileSource
{
public:
	static std::expected<std::unique_ptr<BigArchiveSource>, std::string> Open(const std::filesystem::path &path)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return std::unexpected(path.generic_string() + ": cannot open");
		std::error_code error;
		const std::uint64_t fileSize = std::filesystem::file_size(path, error);
		if (error)
			return std::unexpected(path.generic_string() + ": cannot read size");

		std::vector<std::byte> bytes(big::HeaderSize);
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		const auto header = big::ParseHeader(bytes);
		if (!stream || !header)
			return std::unexpected(path.generic_string() + ": " + (header ? std::string("truncated header") : header.error()));
		if (header->indexEnd > fileSize)
			return std::unexpected(path.generic_string() + ": index runs past the end of the file");
		bytes.resize(header->indexEnd);
		stream.read(reinterpret_cast<char *>(bytes.data() + big::HeaderSize), static_cast<std::streamsize>(header->indexEnd - big::HeaderSize));
		if (!stream)
			return std::unexpected(path.generic_string() + ": truncated index");
		auto entries = big::ParseIndex(bytes, fileSize);
		if (!entries)
			return std::unexpected(path.generic_string() + ": " + entries.error());

		stream.close();
		auto source = std::unique_ptr<BigArchiveSource>(new BigArchiveSource(path));
		// Within one archive the first entry for a path wins, as in the original loader.
		for (big::Entry &entry : *entries)
			source->m_entries.try_emplace(std::move(entry.path), Location{entry.offset, entry.size});
		return source;
	}

	std::string_view Name() const noexcept override { return m_name; }

	std::vector<std::string> Paths() const override
	{
		std::vector<std::string> paths;
		paths.reserve(m_entries.size());
		for (const auto &entry : m_entries)
			paths.push_back(entry.first);
		return paths;
	}

	std::optional<std::vector<std::byte>> Read(std::string_view normalizedPath) const override
	{
		const auto found = m_entries.find(normalizedPath);
		if (found == m_entries.end())
			return std::nullopt;
		std::ifstream stream(m_path, std::ios::binary);
		if (!stream)
			return std::nullopt;
		std::vector<std::byte> bytes(found->second.size);
		stream.seekg(found->second.offset);
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream)
			return std::nullopt;
		return bytes;
	}

private:
	struct Location
	{
		std::uint32_t offset;
		std::uint32_t size;
	};

	explicit BigArchiveSource(const std::filesystem::path &path) : m_path(path), m_name(path.generic_string()) {}

	std::filesystem::path m_path;
	std::string m_name;
	std::map<std::string, Location, std::less<>> m_entries;
};
}
