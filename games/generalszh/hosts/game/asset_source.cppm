export module games.generalszh.hosts.game.asset_source;
import std;

import Assets.Identity;
import Assets.Importers.Models;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.install.asset_paths;

namespace generalszh::host::detail
{
inline std::string Lower(std::string_view value)
{
	std::string out(value);
	for (char &c : out)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

inline std::vector<std::byte> ReadFile(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return {};
	std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return bytes;
}
}

export namespace generalszh::host
{
// Serves the asset runtime from the game's virtual file system: textures are
// looked up by file name anywhere under Art/Textures (as the original does),
// with its known naming aliases; models by name + ".w3d" under Art/W3D;
// fonts from the system font directory, as the original used Windows fonts.
class GameAssetSource
{
public:
	explicit GameAssetSource(const engine::filesystem::VirtualFileSystem &files) : m_files(files)
	{
		for (const std::string &path : files.List("art/textures"))
			m_textures.try_emplace(std::filesystem::path(path).filename().string(), path);
		for (const std::string &path : files.List("art/w3d"))
			m_models.try_emplace(std::filesystem::path(path).filename().string(), path);
	}

	std::vector<std::byte> operator()(const Assets::AssetIdentity &identity) const
	{
		if (identity.type == Assets::AssetType::Font)
			return Font(identity.canonical_name);
		const auto &index = identity.type == Assets::AssetType::Texture ? m_textures : m_models;
		// A texture named by its path (a map's preview, "maps\<name>\<name>.tga") is read as it is.
		if (identity.type == Assets::AssetType::Texture && identity.canonical_name.find_first_of("/\\") != std::string::npos)
			if (auto bytes = m_files.Read(identity.canonical_name))
				return std::move(*bytes);
		std::string name = detail::Lower(std::filesystem::path(identity.canonical_name).filename().string());
		// Animations are named "<skeleton>.<file>", stored in <file>.w3d.
		if (identity.type == Assets::AssetType::Animation && name.find('.') != std::string::npos)
			name = name.substr(name.find('.') + 1) + ".w3d";
		if (identity.type != Assets::AssetType::Texture && std::filesystem::path(name).extension().empty())
			name += ".w3d";
		// Mapped-image INIs keep original names; shipped data renamed some UI
		// atlases (".dds", "...userinterface.tga").
		std::vector<std::string> names{name};
		if (identity.type == Assets::AssetType::Texture)
		{
			const std::filesystem::path path(name);
			names.push_back(path.stem().string() + "userinterface" + path.extension().string());
			names.push_back(path.stem().string() + ".dds");
			names.push_back(path.stem().string() + ".tga");
		}
		// The original's lookup: the language's art first, then the shared art.
		const auto folder = identity.type == Assets::AssetType::Texture ? content::AssetFolder::Textures : content::AssetFolder::Models;
		for (const std::string &candidate : names)
			for (const std::string &path : content::AssetCandidates(folder, candidate))
				if (auto bytes = m_files.Read(path))
					return std::move(*bytes);
		// Files shipped in subfolders of the art folders.
		for (const std::string &candidate : names)
			if (const auto found = index.find(candidate); found != index.end())
				return m_files.Read(found->second).value_or(std::vector<std::byte>{});
		return {};
	}

private:
	static std::vector<std::byte> Font(std::string_view identity)
	{
		// identity: "font/<family>/<size>/<bold>". The original asks Windows for the face by name and weight (GDI
		// CreateFont): the system font files of that family, bold ones for a bold font; an unknown face: Arial.
		std::string family;
		bool bold = false;
		if (identity.starts_with("font/"))
		{
			const auto end = identity.find('/', 5);
			family = detail::Lower(identity.substr(5, end == std::string_view::npos ? std::string_view::npos : end - 5));
			bold = identity.ends_with("/1") || identity.find("/1/") != std::string_view::npos;
		}
		const char *windows = std::getenv("WINDIR");
		const std::filesystem::path fonts = std::filesystem::path(windows != nullptr ? windows : "C:/Windows") / "Fonts";
		static constexpr std::array<std::array<std::string_view, 3>, 6> Files{{
			{"courier new", "cour.ttf", "courbd.ttf"},
			{"courier", "cour.ttf", "courbd.ttf"},
			{"arial narrow", "arialn.ttf", "arialnb.ttf"},
			{"arial", "arial.ttf", "arialbd.ttf"},
			{"times new roman", "times.ttf", "timesbd.ttf"},
			{"times", "times.ttf", "timesbd.ttf"},
		}};
		for (const auto &[name, regular, heavy] : Files)
			if (family == name)
			{
				if (auto bytes = detail::ReadFile(fonts / (bold ? heavy : regular)); !bytes.empty())
					return bytes;
				if (auto bytes = detail::ReadFile(fonts / regular); !bytes.empty())
					return bytes;
			}
		return detail::ReadFile(fonts / (bold ? "arialbd.ttf" : "arial.ttf"));
	}

	const engine::filesystem::VirtualFileSystem &m_files;
	std::map<std::string, std::string, std::less<>> m_textures;
	std::map<std::string, std::string, std::less<>> m_models;
};

// Model formats the install ships (W3D); defined in asset_source.cpp so
// importers do not pull the format adapters' module graph.
std::vector<std::shared_ptr<const Assets::IModelAdapter>> GameModelAdapters();
}
