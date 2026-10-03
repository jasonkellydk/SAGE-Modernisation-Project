export module Graphics.Scene.Props.AssetPreparation;
import std;
export import Graphics.Scene.Props.AssetGeometry;
export import Graphics.Resources.MipChain;
import Assets.Cache;
import Assets.Models;
import Assets.Materials;
import Assets.Textures;
import Assets.Handles;
export import Graphics.Scene.Particles.EmitterAssetBinding;

export namespace Graphics
{
// One immutable filtered chain per texture, shared by all models in a load.
// Handles and CPU assets are supplied by the existing shared asset cache.
using PreparedPropTextures = std::vector<std::pair<Assets::TextureAssetHandle, std::shared_ptr<const PreparedMipChain>>>;
struct PreparedPropAsset
{
	Assets::ModelAssetHandle model;
	std::vector<PropAssetPart> geometry;
	std::shared_ptr<const PreparedPropTextures> textures;
};
struct PreparedModelEmitter {PreparedEmitterAsset asset;std::uint32_t bone{~0u},lod{};};
inline bool Prepare_Model_Emitters(Assets::AssetCache& assets,Assets::ModelAssetHandle handle,
    std::vector<PreparedModelEmitter>& result,std::string& error) {
    const auto* model=assets.Try_Get_Model(handle);if(!model) {error="emitter model is not ready";return false;}
    const auto prepare=[&](const Assets::EmitterAssetDesc& description,std::uint32_t bone,std::uint32_t lod) {
        PreparedModelEmitter emitter;emitter.asset.description=description;emitter.bone=bone;emitter.lod=lod;
        if(!description.texture_name.empty()) {
            const auto texture=assets.Request_Texture(description.texture_name);const auto* source=assets.Try_Get_Texture(texture);
            if(!source || !source->Has_Pixels()) {error="emitter texture is not ready";return false;}
            auto chain=Prepare_Mip_Chain(source->Width(),source->Height(),source->Pixels(),source->Row_Pitch());
            if(chain.levels.empty()) {error="emitter texture mip preparation failed";return false;}emitter.asset.texture=std::make_shared<const PreparedMipChain>(std::move(chain));
        }
        result.push_back(std::move(emitter));return true;
    };
    if(model->Emitter() && !prepare(*model->Emitter(),~0u,0)) return false;
    for(const auto& emitter:model->Emitters()) if(!prepare(emitter.description,emitter.bone,emitter.lod)) return false;
    return true;
}

inline bool Prepare_Prop_Textures(Assets::AssetCache &assets, Assets::ModelAssetHandle handle,
	PreparedPropTextures &textures, std::string &error, unsigned reduction = 0, unsigned minimum_dimension = 1)
{
	for (const auto texture : assets.Model_Texture_Dependencies(handle)) {
		if (!texture.Is_Valid() || std::ranges::any_of(textures, [&](const auto &entry) {return entry.first == texture;})) continue;
		const auto *source = assets.Try_Get_Texture(texture);
		if (!source || !source->Has_Pixels()) {error = "model texture is not ready"; return false;}
		auto chain = Prepare_Mip_Chain(source->Width(), source->Height(), source->Pixels(), source->Row_Pitch(), 0, reduction, minimum_dimension);
		if (chain.levels.empty()) {error = "could not prepare model texture"; return false;}
		textures.emplace_back(texture, std::make_shared<const PreparedMipChain>(std::move(chain)));
	}
	return true;
}
}
