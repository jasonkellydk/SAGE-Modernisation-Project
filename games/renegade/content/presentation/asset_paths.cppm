export module games.renegade.content.presentation.asset_paths;
import std;
import Assets.Identity;

export namespace renegade::content
{
// Renegade's archive namespace is flat. The texture fallback follows
// ww3d2/textureloader.cpp (DDS before the requested TGA); models and separately
// stored animation/skeleton files follow ww3d2/assetmgr.cpp.
std::vector<std::string> AssetPaths(const Assets::AssetIdentity& identity) {
    std::string name = identity.canonical_name;
	if (const auto selector = name.find("::"); selector != std::string::npos) name.erase(selector);
    if (identity.type == Assets::AssetType::Animation) {
        if (const auto separator=name.find('.'); separator!=std::string::npos) name.erase(0,separator+1);
    }
    if (identity.type == Assets::AssetType::Texture) {
        auto path=std::filesystem::path(name);
        path.replace_extension(".dds");
        return {path.generic_string(),name};
    }
    if (!name.ends_with(".w3d")) name += ".w3d";
    return {name};
}

struct MenuAssets {
    static constexpr std::string_view backdrop="IF_BACK01";
    static constexpr std::string_view logo="IF_RENLOGO";
    static constexpr std::string_view transition="IF_TITLETRANS";
    static constexpr std::string_view gizmo="IF_EVAGIZMO";
    static constexpr std::string_view music="menu.mp3";
};
}
