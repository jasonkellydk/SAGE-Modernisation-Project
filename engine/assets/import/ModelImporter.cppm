module;

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Assets.Importers.Models;

import Assets.Identity;
import Assets.Models;

namespace Assets
{

export struct ModelImportResult final
{
	std::unique_ptr<ModelAssetDesc> description;
	std::string error;

	bool Succeeded() const noexcept
	{
		return description != nullptr && error.empty();
	}
};

export class IModelAdapter
{
public:
	virtual ~IModelAdapter() = default;
	virtual bool Can_Import(const AssetIdentity &identity, std::span<const std::byte> source) const noexcept = 0;
	virtual ModelImportResult Import(const AssetIdentity &identity, std::span<const std::byte> source) const = 0;
};

export using AssetSource = std::function<std::vector<std::byte>(const AssetIdentity &)>;

}
