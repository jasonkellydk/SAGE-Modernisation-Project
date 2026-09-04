module;

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Assets.Cache.TextureLoadTask;

import Assets.Identity;
import Assets.Importers.Models;
import Assets.Textures;

namespace Assets
{

export struct TextureLoadResult final
{
	std::shared_ptr<const TextureAsset> asset;
	std::string error;

	bool Succeeded() const noexcept
	{
		return asset != nullptr && error.empty();
	}
};

export TextureLoadResult Load_Texture_Asset(const AssetIdentity &identity, const AssetSource &source);

namespace TextureLoadDetail
{

std::string Source_Format(std::string_view name)
{
	const std::size_t extension = name.find_last_of('.');
	return extension == std::string_view::npos ? "unknown" : std::string(name.substr(extension + 1));
}

}

TextureLoadResult Load_Texture_Asset(const AssetIdentity &identity, const AssetSource &source)
{
	try {
		if (!source)
			return {nullptr, "no asset source is configured"};
		const std::vector<std::byte> bytes = source(identity);
		if (bytes.empty())
			return {nullptr, "texture source is empty"};

		return {
			std::make_shared<const TextureAsset>(
				identity,
				TextureLoadDetail::Source_Format(identity.canonical_name),
				bytes.size()),
			{}};
	} catch (const std::exception &exception) {
		return {nullptr, exception.what()};
	} catch (...) {
		return {nullptr, "unknown exception while loading texture"};
	}
}

}
