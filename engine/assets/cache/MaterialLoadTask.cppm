module;

#include <exception>
#include <memory>
#include <string>
#include <utility>

export module Assets.Cache.MaterialLoadTask;

import Assets.Handles;
import Assets.Identity;
import Assets.Materials;

namespace Assets
{

export struct MaterialLoadResult final
{
	std::shared_ptr<const MaterialAsset> asset;
	std::string error;

	bool Succeeded() const noexcept
	{
		return asset != nullptr && error.empty();
	}
};

export MaterialLoadResult Finalize_Material_Asset(
	const AssetIdentity &identity,
	const MaterialAssetDesc &description,
	TextureAssetHandle primary_texture,
	TextureAssetHandle secondary_texture);

MaterialLoadResult Finalize_Material_Asset(
	const AssetIdentity &identity,
	const MaterialAssetDesc &description,
	TextureAssetHandle primary_texture,
	TextureAssetHandle secondary_texture)
{
	try {
		return {
			std::make_shared<const MaterialAsset>(identity, description, primary_texture, secondary_texture),
			{}};
	} catch (const std::exception &exception) {
		return {nullptr, exception.what()};
	} catch (...) {
		return {nullptr, "unknown exception while finalizing material"};
	}
}

}
