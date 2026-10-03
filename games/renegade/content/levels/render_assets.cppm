export module games.renegade.content.levels.render_assets;
import std;
export import games.renegade.content.levels.level_scene;
import Assets.Adapters.W3D.RequestPolicy;
import Assets.Adapters.W3D.Box;
import Assets.Identity;
import engine.filesystem.core.virtual_file_system;

export namespace renegade::content
{
inline std::expected<std::string,std::string> ResolvePresetModel(const engine::filesystem::VirtualFileSystem& files,std::string_view authored) {
    // Retail presets retain editor directory paths, while MIX filenames are
    // flat. Preserve mod directory overrides first, then the original leaf.
    std::string path(authored);std::ranges::replace(path,'\\','/');
    if(files.Exists(path)) return path;
    const auto leaf=path.substr(path.find_last_of('/')==std::string::npos ? 0 : path.find_last_of('/')+1);
    if(files.Exists(leaf)) return leaf;
    return std::unexpected("missing preset model "+path);
}
inline std::expected<bool, std::string> IsCollisionBox(Assets::W3D::W3DByteSpan source, std::string_view object)
{
	// BoxRenderObjClass's display mask starts at zero: these are authored
	// collision/debug objects, not visible mesh exports. Decode through the
	// shared box adapter; the game's ordinary visibility policy stays here.
	bool found = false; std::string error;
	if (!Assets::W3D::W3DVisit_Chunks(source, [&](const auto &chunk) {
		if (chunk.id != Assets::W3D::W3DChunkBox) return true;
		Assets::W3D::W3DBoxDescription box;
		if (!Assets::W3D::W3DRead_Box(chunk.payload, box, error)) return false;
		if (Assets::Canonicalize_Asset_Name(box.name) == Assets::Canonicalize_Asset_Name(object)) {
			if (found) {error = "duplicate collision box export"; return false;}
			found = true;
		}
		return true;
	})) return std::unexpected(error.empty() ? "malformed W3D collision box container" : error);
	return found;
}
inline std::expected<std::string, std::string> ResolveRenderAsset(const engine::filesystem::VirtualFileSystem &files,
	const PhysicsPlacement &placement, const DefinitionCatalog &catalog)
{
	if (placement.render_factory != 0x10000 || placement.model.empty() || Assets::Canonicalize_Asset_Name(placement.model) == "null") return std::string{};
	// Original assetmgr.cpp on-demand first-dot request policy. A physics
	// preset can name the preload container explicitly when its exports have
	// a different root. It is content, not a renderer naming convention.
	const auto requested = Assets::W3D::W3D_Request_Filename(Assets::W3D::W3DRequestKind::Model, placement.model);
	if (!requested) return std::unexpected("invalid render-object identity");
	std::string source = *requested;
	if (!files.Exists(source)) {
		const auto *definition = catalog.Find(placement.definition);
		if (!definition || definition->model.empty() || !files.Exists(definition->model))
			return std::unexpected("unresolved W3D container for " + placement.model);
		source = definition->model;
	}
	if (placement.model.find('.') != std::string::npos) source += "::" + placement.model;
	return source;
}
}
