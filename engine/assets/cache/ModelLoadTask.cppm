module;

#include <exception>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module Assets.Cache.ModelLoadTask;

import Assets.Identity;
import Assets.Importers.Models;
import Assets.Models;

namespace Assets
{

export struct ModelLoadResult final
{
	std::shared_ptr<const ModelAsset> asset;
	std::string error;

	bool Succeeded() const noexcept
	{
		return asset != nullptr && error.empty();
	}
};

export ModelLoadResult Load_Model_Asset(
	const AssetIdentity &identity,
	const AssetSource &source,
	std::span<const std::shared_ptr<const IModelAdapter>> adapters);

ModelLoadResult Load_Model_Asset(
	const AssetIdentity &identity,
	const AssetSource &source,
	std::span<const std::shared_ptr<const IModelAdapter>> adapters)
{
	try {
		if (!source)
			return {nullptr, "no asset source is configured"};

		std::vector<std::byte> bytes = source(identity);
		const std::shared_ptr<const IModelAdapter> *selected = nullptr;
		for (const std::shared_ptr<const IModelAdapter> &adapter : adapters) {
			if (adapter && adapter->Can_Import(identity, bytes)) {
				selected = &adapter;
				break;
			}
		}

		if (selected == nullptr)
			return {nullptr, "no model adapter accepts the requested asset"};

		ModelImportResult imported = (*selected)->Import(identity, bytes);
		if (!imported.Succeeded())
			return {nullptr, imported.error.empty() ? "model import failed" : std::move(imported.error)};

		return {std::make_shared<const ModelAsset>(identity, std::move(*imported.description)), {}};
	} catch (const std::exception &exception) {
		return {nullptr, exception.what()};
	} catch (...) {
		return {nullptr, "unknown exception while loading model"};
	}
}

}
