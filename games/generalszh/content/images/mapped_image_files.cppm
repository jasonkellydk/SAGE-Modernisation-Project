export module games.generalszh.content.images.mapped_image_files;
import std;

export import engine.filesystem.core.virtual_file_system;

// The mapped-image INI files, in the order the original loads them: the
// texture-size pack (Data/INI/MappedImages/TextureSize_<size>), then the
// hand-made images (…/HandCreated). Within a directory, its own files come
// first and then those of its subdirectories, each group sorted without
// regard to case. Every file overwrites what earlier ones defined, so apply
// them in this order with last-definition-wins.
export namespace generalszh::content
{
std::vector<std::string> MappedImageFiles(const engine::filesystem::VirtualFileSystem &files, int textureSize = 512)
{
	std::vector<std::string> ordered;
	const auto appendDirectory = [&](const std::string &directory) {
		// Listings are normalized (lower case) and sorted, as the original's case-insensitive file list.
		const std::vector<std::string> paths = files.List(directory, ".ini");
		const std::string prefix = engine::filesystem::NormalizePath(directory) + "/";
		for (const bool nested : {false, true})
			for (const std::string &path : paths)
				if ((path.find('/', prefix.size()) != std::string::npos) == nested)
					ordered.push_back(path);
	};
	appendDirectory("Data/INI/MappedImages/TextureSize_" + std::to_string(textureSize));
	appendDirectory("Data/INI/MappedImages/HandCreated");
	return ordered;
}
}
