export module games.generalszh.content.install.asset_paths;
import std;

// Where the original looks for a texture or model file, in order: the
// language's own art first (Data/<Language>/Art/Textures, …/Art/W3D: baked-in
// text and localized units), then the shared art (Art/Textures, Art/W3D).
export namespace generalszh::content
{
enum class AssetFolder
{
	Textures,
	Models,
};

std::vector<std::string> AssetCandidates(AssetFolder folder, std::string_view file, std::string_view language = "English")
{
	const std::string_view art = folder == AssetFolder::Textures ? "Art/Textures/" : "Art/W3D/";
	std::vector<std::string> candidates;
	candidates.push_back("Data/" + std::string(language) + "/" + std::string(art) + std::string(file));
	candidates.push_back(std::string(art) + std::string(file));
	return candidates;
}
}
