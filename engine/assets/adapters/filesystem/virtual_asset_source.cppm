export module engine.assets.adapters.filesystem.virtual_asset_source;
import std;
import Assets.Identity;
import Assets.Importers.Models;
import engine.filesystem.core.virtual_file_system;

export namespace engine::assets
{
using AssetPaths = std::function<std::vector<std::string>(const Assets::AssetIdentity&)>;
using FontSource = std::function<std::vector<std::byte>(std::string_view)>;

// Content naming and precedence belong to the caller. AssetCache receives the
// same bytes regardless of whether the VFS source is loose, BIG, MIX or a mod.
class VirtualAssetSource {
public:
    VirtualAssetSource(const filesystem::VirtualFileSystem& files, AssetPaths paths, FontSource fonts = {})
        : m_files(files), m_paths(std::move(paths)), m_fonts(std::move(fonts)) {}
    std::vector<std::byte> operator()(const Assets::AssetIdentity& identity) const {
        if (identity.type == Assets::AssetType::Font) return m_fonts ? m_fonts(identity.canonical_name) : std::vector<std::byte>{};
        for (const auto& candidate : m_paths(identity))
            if (auto bytes = m_files.Read(candidate)) return std::move(*bytes);
        return {};
    }
private:
    const filesystem::VirtualFileSystem& m_files;
    AssetPaths m_paths;
    FontSource m_fonts;
};
}
