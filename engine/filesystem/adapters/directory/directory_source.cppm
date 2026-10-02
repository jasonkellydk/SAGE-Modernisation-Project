export module engine.filesystem.adapters.directory.directory_source;
import std;

export import engine.filesystem.core.virtual_file_system;

export namespace engine::filesystem
{
// Loose files under a directory, indexed once at construction. `mountPoint`
// places them inside the virtual namespace (e.g. root "Run", mount "").
class DirectorySource final : public FileSource
{
public:
	explicit DirectorySource(std::filesystem::path root, std::string_view mountPoint = {}) :
		m_root(std::move(root)), m_name(m_root.generic_string())
	{
		std::string prefix = NormalizePath(mountPoint);
		if (!prefix.empty() && prefix.back() != '/')
			prefix += '/';
		std::error_code error;
		for (auto entry = std::filesystem::recursive_directory_iterator(m_root, error);
			!error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error))
		{
			if (entry->is_regular_file(error))
				m_files.try_emplace(prefix + NormalizePath(std::filesystem::relative(entry->path(), m_root).generic_string()), entry->path());
		}
	}

	std::string_view Name() const noexcept override { return m_name; }

	std::vector<std::string> Paths() const override
	{
		std::vector<std::string> paths;
		paths.reserve(m_files.size());
		for (const auto &entry : m_files)
			paths.push_back(entry.first);
		return paths;
	}

	std::optional<std::vector<std::byte>> Read(std::string_view normalizedPath) const override
	{
		const auto found = m_files.find(normalizedPath);
		if (found == m_files.end())
			return std::nullopt;
		std::ifstream stream(found->second, std::ios::binary);
		if (!stream)
			return std::nullopt;
		stream.seekg(0, std::ios::end);
		std::vector<std::byte> bytes(static_cast<std::size_t>(stream.tellg()));
		stream.seekg(0);
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream)
			return std::nullopt;
		return bytes;
	}

private:
	std::filesystem::path m_root;
	std::string m_name;
	std::map<std::string, std::filesystem::path, std::less<>> m_files;
};
}
