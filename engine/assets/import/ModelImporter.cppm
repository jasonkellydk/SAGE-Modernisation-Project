export module Assets.Importers.Models;
import std;

import Assets.Identity;
import Assets.Models;

namespace Assets
{

export struct ModelImportResult final
{
	std::unique_ptr<ModelAssetDesc> description;
	std::string error;
	~ModelImportResult();

	bool Succeeded() const noexcept
	{
		return description != nullptr && error.empty();
	}
};

inline ModelImportResult::~ModelImportResult() = default;

export class IModelAdapter
{
public:
	virtual ~IModelAdapter() = default;
	virtual bool Can_Import(const AssetIdentity &identity, std::span<const std::byte> source) const noexcept = 0;
	virtual ModelImportResult Import(const AssetIdentity &identity, std::span<const std::byte> source) const = 0;
	// Immutable assemblies may reference geometry in other containers. Source
	// resolution belongs to preparation, before publication or GPU submission.
	virtual ModelImportResult Import_With_Source(const AssetIdentity &identity, std::span<const std::byte> bytes,
		const std::function<std::vector<std::byte>(const AssetIdentity &)> &source) const;
	// Reads only the rig of a source: a separately stored skeleton hierarchy
	// or a file of animation clips, neither of which carries geometry.
	// Adapters without rig-only sources keep this default.
	virtual bool Import_Rig(const AssetIdentity &identity, std::span<const std::byte> source, ModelRigDesc &result,
		std::string &error) const;
};

ModelImportResult IModelAdapter::Import_With_Source(const AssetIdentity &identity, std::span<const std::byte> bytes,
	const std::function<std::vector<std::byte>(const AssetIdentity &)> &) const { return Import(identity, bytes); }

bool IModelAdapter::Import_Rig(const AssetIdentity &, std::span<const std::byte>, ModelRigDesc &, std::string &error) const
{
	error = "model adapter does not import rig-only sources";
	return false;
}

export using AssetSource = std::function<std::vector<std::byte>(const AssetIdentity &)>;

}
