export module games.renegade.content.install.install_mount;
import std;
export import engine.filesystem.adapters.mix.mix_archive_source;
export import engine.filesystem.adapters.directory.directory_source;

export namespace renegade::content
{
struct MountedInstall
{
	std::unique_ptr<engine::filesystem::VirtualFileSystem> files;
	std::vector<std::string> archives;
};
inline std::expected<MountedInstall, std::string> MountInstall(const std::filesystem::path &install)
{
	using namespace engine::filesystem;
	const auto data = install / "Data";
	if (!std::filesystem::is_directory(data)) return std::unexpected("Renegade install requires a Data directory");
	// Physical paths resolved case-insensitively, including on POSIX hosts.
	std::map<std::string, std::filesystem::path> physical;
	for (const auto &entry : std::filesystem::directory_iterator(data))
		if (entry.is_regular_file()) physical.emplace(NormalizePath(entry.path().filename().generic_string()), entry.path());
	MountedInstall result{std::make_unique<VirtualFileSystem>(), {}};
	const auto mountDirectory = [&](const std::filesystem::path &path) -> std::optional<std::string> {
		if (!std::filesystem::is_directory(path)) return {};
		result.files->Mount(std::make_unique<DirectorySource>(path));
		return {};
	};
	// Commando/init.cpp: loose Data, save, config, then Always2, dbs, dat,
	// then maps. Map order is lexical here for reproducibility across OSes.
	for (const auto &directory : {data, data / "save", data / "config"})
		if (auto error = mountDirectory(directory)) return std::unexpected(*error);
	const auto mountArchive = [&](std::string_view name) -> std::optional<std::string> {
		const auto found = physical.find(std::string(name));
		if (found == physical.end()) return "missing required archive: " + std::string(name);
		auto source = MixArchiveSource::Open(found->second);
		if (!source) return source.error();
		result.files->Mount(std::move(*source));
		result.archives.emplace_back(name);
		return {};
	};
	for (const auto name : {"always2.dat", "always.dbs", "always.dat"})
		if (auto error = mountArchive(name)) return std::unexpected(*error);
	for (const auto &[name, path] : physical)
		if (name.ends_with(".mix"))
			if (auto error = mountArchive(name)) return std::unexpected(*error);
	return result;
}
}
