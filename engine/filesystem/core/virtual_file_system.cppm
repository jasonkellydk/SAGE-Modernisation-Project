export module engine.filesystem.core.virtual_file_system;
import std;

export import engine.filesystem.core.path;

export namespace engine::filesystem
{
// A place files come from: a directory, an archive, a test fixture. Paths
// given to and returned from a source are normalized (see NormalizePath).
// Read must be safe to call from several threads at once.
class FileSource
{
public:
	virtual ~FileSource() = default;
	virtual std::string_view Name() const noexcept = 0;
	virtual std::vector<std::string> Paths() const = 0;
	virtual std::optional<std::vector<std::byte>> Read(std::string_view normalizedPath) const = 0;
};

// Merges sources into one read-only namespace. Precedence is explicit: a
// source mounted earlier wins over one mounted later (the host mounts loose
// directories first, then archives in their load order). The merged index is
// sorted, so listings are identical on every platform and file system.
class VirtualFileSystem
{
public:
	VirtualFileSystem() = default;
	VirtualFileSystem(const VirtualFileSystem &) = delete;
	VirtualFileSystem &operator=(const VirtualFileSystem &) = delete;

	void Mount(std::unique_ptr<FileSource> source)
	{
		const std::size_t index = m_sources.size();
		for (std::string &path : source->Paths())
			m_index.try_emplace(std::move(path), index);
		m_sources.push_back(std::move(source));
	}

	bool Exists(std::string_view path) const { return m_index.contains(NormalizePath(path)); }

	std::optional<std::vector<std::byte>> Read(std::string_view path) const
	{
		const std::string normalized = NormalizePath(path);
		const auto found = m_index.find(normalized);
		if (found == m_index.end())
			return std::nullopt;
		return m_sources[found->second]->Read(normalized);
	}

	std::optional<std::string> ReadText(std::string_view path) const
	{
		auto bytes = Read(path);
		if (!bytes)
			return std::nullopt;
		return std::string(reinterpret_cast<const char *>(bytes->data()), bytes->size());
	}

	// Name of the source that provides `path`, for diagnostics.
	std::optional<std::string_view> Origin(std::string_view path) const
	{
		const auto found = m_index.find(NormalizePath(path));
		if (found == m_index.end())
			return std::nullopt;
		return m_sources[found->second]->Name();
	}

	// Sorted paths under `directory` (recursive) with the given extension
	// (".ini"); empty arguments match everything.
	std::vector<std::string> List(std::string_view directory = {}, std::string_view extension = {}) const
	{
		std::string prefix = NormalizePath(directory);
		if (!prefix.empty() && prefix.back() != '/')
			prefix += '/';
		const std::string wantedExtension = NormalizePath(extension);
		std::vector<std::string> paths;
		for (auto entry = m_index.lower_bound(prefix); entry != m_index.end() && entry->first.starts_with(prefix); ++entry)
			if (wantedExtension.empty() || Extension(entry->first) == wantedExtension)
				paths.push_back(entry->first);
		return paths;
	}

	std::size_t FileCount() const noexcept { return m_index.size(); }
	std::size_t SourceCount() const noexcept { return m_sources.size(); }

private:
	std::vector<std::unique_ptr<FileSource>> m_sources;
	std::map<std::string, std::size_t, std::less<>> m_index;
};
}
