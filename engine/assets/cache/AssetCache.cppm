module;

#include <atomic>
#include <cstddef>
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module Assets.Cache;

import Assets.Cache.ModelLoadTask;
import Assets.Handles;
import Assets.Identity;
import Assets.Importers.Models;
import Assets.Models;
import Assets.States;

namespace Assets
{

export class AssetCache final
{
public:
	explicit AssetCache(AssetSource source);
	~AssetCache();

	AssetCache(const AssetCache &) = delete;
	AssetCache &operator=(const AssetCache &) = delete;

	bool Register_Model_Adapter(std::shared_ptr<const IModelAdapter> adapter);
	ModelAssetHandle Request_Model(std::string_view name);

	AssetState Get_State(ModelAssetHandle handle) const noexcept;
	// Returns a non-owning view. Callers retain the typed handle and must keep
	// this cache alive while using the returned asset.
	const ModelAsset *Try_Get_Model(ModelAssetHandle handle) const noexcept;
	std::string Get_Error(ModelAssetHandle handle) const;
	void Wait(ModelAssetHandle handle) const;

	std::size_t Model_Count() const noexcept;

private:
	struct ModelEntry final
	{
		AssetIdentity identity;
		ModelAssetHandle handle;
		std::atomic<AssetState> state{AssetState::Unloaded};
		std::shared_ptr<const ModelAsset> asset;
		mutable std::mutex error_mutex;
		std::string error;
		std::shared_future<void> completion;
	};

	struct ModelSnapshot final
	{
		std::vector<std::shared_ptr<ModelEntry>> entries;
		std::unordered_map<std::string, ModelAssetHandle> handles;
	};

	static std::shared_ptr<ModelEntry> Find_Entry(
		const std::shared_ptr<const ModelSnapshot> &snapshot,
		ModelAssetHandle handle) noexcept;
	static void Set_Failure(const std::shared_ptr<ModelEntry> &entry, std::string error) noexcept;
	void Wait_All() const;

	AssetSource m_source;
	mutable std::mutex m_request_mutex;
	std::vector<std::shared_ptr<const IModelAdapter>> m_model_adapters;
	std::shared_ptr<const ModelSnapshot> m_model_snapshot;
};

AssetCache::AssetCache(AssetSource source)
	: m_source(std::move(source)),
	  m_model_snapshot(std::make_shared<const ModelSnapshot>())
{
}

AssetCache::~AssetCache()
{
	Wait_All();
}

bool AssetCache::Register_Model_Adapter(std::shared_ptr<const IModelAdapter> adapter)
{
	if (!adapter)
		return false;

	std::lock_guard lock(m_request_mutex);
	m_model_adapters.push_back(std::move(adapter));
	return true;
}

ModelAssetHandle AssetCache::Request_Model(std::string_view name)
{
	const std::string canonical_name = Canonicalize_Asset_Name(name);
	if (canonical_name.empty())
		return ModelAssetHandle::Invalid();

	std::lock_guard lock(m_request_mutex);
	const std::shared_ptr<const ModelSnapshot> current =
		std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	const auto existing = current->handles.find(canonical_name);
	if (existing != current->handles.end())
		return existing->second;

	if (current->entries.size() >= std::numeric_limits<ModelAssetHandle::Index>::max())
		return ModelAssetHandle::Invalid();

	const ModelAssetHandle handle(
		static_cast<ModelAssetHandle::Index>(current->entries.size()),
		ModelAssetHandle::Generation{1});
	auto entry = std::make_shared<ModelEntry>();
	entry->identity = {AssetType::Model, canonical_name};
	entry->handle = handle;
	entry->state.store(AssetState::Loading, std::memory_order_release);

	const AssetSource source = m_source;
	const std::vector<std::shared_ptr<const IModelAdapter>> adapters = m_model_adapters;
	try {
		entry->completion = std::async(
			std::launch::async,
			[entry, source, adapters]() {
				const ModelLoadResult loaded = Load_Model_Asset(entry->identity, source, adapters);
				if (!loaded.Succeeded()) {
					AssetCache::Set_Failure(entry, loaded.error);
					return;
				}

				std::atomic_store_explicit(&entry->asset, loaded.asset, std::memory_order_release);
				entry->state.store(AssetState::Ready, std::memory_order_release);
			});
	} catch (const std::exception &exception) {
		Set_Failure(entry, exception.what());
	} catch (...) {
		Set_Failure(entry, "could not start asynchronous model load");
	}

	auto next = std::make_shared<ModelSnapshot>(*current);
	next->entries.push_back(entry);
	next->handles.emplace(canonical_name, handle);
	std::atomic_store_explicit(
		&m_model_snapshot,
		std::shared_ptr<const ModelSnapshot>(std::move(next)),
		std::memory_order_release);
	return handle;
}

AssetState AssetCache::Get_State(ModelAssetHandle handle) const noexcept
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	const auto entry = Find_Entry(snapshot, handle);
	return entry ? entry->state.load(std::memory_order_acquire) : AssetState::Unloaded;
}

const ModelAsset *AssetCache::Try_Get_Model(ModelAssetHandle handle) const noexcept
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	const auto entry = Find_Entry(snapshot, handle);
	if (!entry || entry->state.load(std::memory_order_acquire) != AssetState::Ready)
		return nullptr;
	const std::shared_ptr<const ModelAsset> asset =
		std::atomic_load_explicit(&entry->asset, std::memory_order_acquire);
	return asset.get();
}

std::string AssetCache::Get_Error(ModelAssetHandle handle) const
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	const auto entry = Find_Entry(snapshot, handle);
	if (!entry)
		return "invalid model asset handle";

	std::lock_guard lock(entry->error_mutex);
	return entry->error;
}

void AssetCache::Wait(ModelAssetHandle handle) const
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	const auto entry = Find_Entry(snapshot, handle);
	if (entry && entry->completion.valid())
		entry->completion.wait();
}

std::size_t AssetCache::Model_Count() const noexcept
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	return snapshot->entries.size();
}

std::shared_ptr<AssetCache::ModelEntry> AssetCache::Find_Entry(
	const std::shared_ptr<const ModelSnapshot> &snapshot,
	ModelAssetHandle handle) noexcept
{
	if (!handle.Is_Valid() || handle.Get_Index() >= snapshot->entries.size())
		return {};

	const std::shared_ptr<ModelEntry> &entry = snapshot->entries[handle.Get_Index()];
	if (!entry || entry->handle != handle)
		return {};
	return entry;
}

void AssetCache::Set_Failure(const std::shared_ptr<ModelEntry> &entry, std::string error) noexcept
{
	try {
		{
			std::lock_guard lock(entry->error_mutex);
			entry->error = std::move(error);
		}
		entry->state.store(AssetState::Failed, std::memory_order_release);
	} catch (...) {
		entry->state.store(AssetState::Failed, std::memory_order_release);
	}
}

void AssetCache::Wait_All() const
{
	const auto snapshot = std::atomic_load_explicit(&m_model_snapshot, std::memory_order_acquire);
	for (const std::shared_ptr<ModelEntry> &entry : snapshot->entries) {
		if (entry && entry->completion.valid())
			entry->completion.wait();
	}
}

}
