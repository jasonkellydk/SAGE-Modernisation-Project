export module games.generalszh.content.install.install_mount;
import std;

export import engine.filesystem.core.virtual_file_system;
import engine.filesystem.adapters.directory.directory_source;
import engine.filesystem.adapters.big.big_archive_source;

export namespace generalszh::content
{
struct MountReport
{
	std::vector<std::string> archives; // in precedence order
	std::vector<std::string> skipped;
	std::vector<std::string> errors;
};

// Mounts a Zero Hour install the way the original game resolves files:
//  1. loose files under the install root override everything;
//  2. every *.big below the root, in the original load order (paths compared
//     byte-wise with '\' separators), where the first archive providing a
//     path wins;
//  3. Data\INI\INIZH.big is skipped: some releases ship a stale duplicate of
//     the root INIZH.big there.
// The base Generals archives of a Steam install (ZH_Generals\) are found by
// step 2 and, sorting after the Zero Hour archives, only fill gaps.
inline MountReport MountInstall(engine::filesystem::VirtualFileSystem &files, const std::filesystem::path &root)
{
	namespace fs = std::filesystem;
	MountReport report;
	files.Mount(std::make_unique<engine::filesystem::DirectorySource>(root));

	std::vector<std::pair<std::string, fs::path>> archives;
	std::error_code error;
	for (auto entry = fs::recursive_directory_iterator(root, error); !error && entry != fs::recursive_directory_iterator(); entry.increment(error))
	{
		std::string extension = entry->path().extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (!entry->is_regular_file() || extension != ".big")
			continue;
		std::string relative = fs::relative(entry->path(), root).string();
		std::replace(relative.begin(), relative.end(), '/', '\\');
		archives.emplace_back(std::move(relative), entry->path());
	}
	// Case-insensitive, as the original's file list (a set ordered without
	// case): "gensecZH.big" must come before "ZH_Generals\gensec.big".
	const auto lower = [](std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	};
	std::sort(archives.begin(), archives.end(), [&](const auto &a, const auto &b) { return lower(a.first) < lower(b.first); });

	for (const auto &[relative, path] : archives)
	{
		if (engine::filesystem::NormalizePath(relative) == "data/ini/inizh.big")
		{
			report.skipped.push_back(relative);
			continue;
		}
		auto archive = engine::filesystem::BigArchiveSource::Open(path);
		if (!archive)
		{
			report.errors.push_back(archive.error());
			continue;
		}
		files.Mount(std::move(*archive));
		report.archives.push_back(relative);
	}
	return report;
}
}
