export module games.generalszh.presentation.models.model_library;
import std;

export import games.generalszh.presentation.models.model_entry;
export import games.generalszh.presentation.objects.algorithms.look_models;
import engine.ecs.system.system;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import Assets.Models;
import Assets.States;

// The looks' models as presentation data (one world, no renderer state): per look the entry it draws (looks sharing a
// model, its animation and its shown parts share one), and per entry its load (the asset requests, read in the
// background) and, once loaded, its ModelEntry: parts, turned bones, animation and collision shape. The renderer draws
// the entries; picking casts rays at them; effects ask them where bones are.
export namespace generalszh::presentation
{
enum class ModelStatus : std::uint8_t
{
	Loading,
	Ready,
	Failed,
};

struct ModelLoad
{
	ModelStatus status{ModelStatus::Loading};
	Assets::ModelAssetHandle handle;
	Assets::TextureAssetHandle texture; // its base maps' replacement (none: its own)
	// The animation's source file, read in the background alongside the model.
	std::shared_future<std::shared_ptr<const Assets::ModelRigDesc>> animationSource;
};

struct ModelLibrary
{
	static constexpr std::uint32_t NoModel = 0xFFFFFFFFu;    // a look that draws nothing
	static constexpr std::uint32_t Unresolved = 0xFFFFFFFEu; // a look not asked for yet
	std::vector<std::uint32_t> entryOfLook;
	// Per entry.
	std::vector<std::string> keys;
	std::vector<ModelLoad> loads;
	std::vector<ModelEntry> entries;
	std::string failures;

	const ModelEntry *ReadyEntry(std::uint32_t look) const noexcept
	{
		if (look >= entryOfLook.size() || entryOfLook[look] >= entries.size())
			return nullptr;
		const std::uint32_t entry = entryOfLook[look];
		return loads[entry].status == ModelStatus::Ready ? &entries[entry] : nullptr;
	}
};

// The asset cache models load from (given to the world, not reached for).
struct ModelAssets
{
	Assets::AssetCache *cache{nullptr};
};

// A look's key: looks sharing a model but not its animation, how that animation plays (its AnimationMode: a strategy
// centre's door opens ONCE and closes ONCE_BACKWARDS on the one clip) or its visible parts draw apart.
inline std::string ModelKey(const ObjectModel &look)
{
	std::string key = look.animation.empty() ? look.model
											 : look.model + "|" + look.animation + "#" + std::to_string(static_cast<int>(look.animationMode)) + (look.repeat ? "r" : "");
	for (const auto *names : {&look.hidden, &look.shown, &look.muzzleFlashes, &look.tires, &look.steeredTires})
	{
		key += "|";
		for (const auto &name : *names)
			key += name + ",";
	}
	key += "|" + std::to_string(look.projectileSlots);
	for (std::size_t slot = 0; slot < 3; ++slot)
		key += "," + look.projectileBones[slot] + "/" + look.projectileHideShow[slot];
	key += "|" + look.turret + "," + look.turretPitch + "," + std::to_string(look.turretArtAngle) + "," + std::to_string(look.turretArtPitch) + "," +
		look.recoilBone + "|" + look.altTurret + "," + look.altTurretPitch + "," + std::to_string(look.altTurretArtAngle) + "," +
		std::to_string(look.altTurretArtPitch) + "|" + look.texture;
	return key;
}

// The entry a look draws (NoModel: nothing), asked for the first time it is: its model, its replacement texture and
// its animation's file are requested then.
inline std::uint32_t ResolveLook(ModelLibrary &library, const LookCatalog &catalog, const MotionLooks *motion, Assets::AssetCache &cache, std::uint32_t look)
{
	if (look >= library.entryOfLook.size())
		library.entryOfLook.resize(look + 1, ModelLibrary::Unresolved);
	std::uint32_t &slot = library.entryOfLook[look];
	if (slot != ModelLibrary::Unresolved)
		return slot;
	const ObjectModel model = LookModelOf(catalog, motion, look);
	if (model.model.empty())
		return slot = ModelLibrary::NoModel;
	const std::string key = ModelKey(model);
	if (const auto found = std::find(library.keys.begin(), library.keys.end(), key); found != library.keys.end())
		return slot = static_cast<std::uint32_t>(found - library.keys.begin());
	slot = static_cast<std::uint32_t>(library.entries.size());
	library.keys.push_back(key);
	library.entries.push_back(DescribeModel(model));
	ModelLoad load;
	load.handle = cache.Request_Model(model.model + ".w3d");
	if (!model.texture.empty())
		load.texture = cache.Request_Texture(model.texture);
	if (!model.animation.empty())
	{
		Assets::AssetCache *source = &cache;
		load.animationSource = std::async(std::launch::async, [source, name = model.animation] {
			std::string error;
			return source->Load_Rig(Assets::AssetType::Animation, name, error);
		}).share();
	}
	library.loads.push_back(std::move(load));
	return slot;
}

// Binds the entries whose model (and replacement texture and animation) finished loading (never waits); one that
// cannot bind fails, one whose animation cannot play draws at rest (both reported in `failures`).
inline void PollModels(ModelLibrary &library, Assets::AssetCache &cache)
{
	for (std::size_t entry = 0; entry < library.entries.size(); ++entry)
	{
		ModelLoad &load = library.loads[entry];
		if (load.status != ModelStatus::Loading)
			continue;
		const auto state = cache.Get_State(load.handle);
		if (state == Assets::AssetState::Loading || state == Assets::AssetState::Unloaded)
			continue;
		if (load.texture.Is_Valid())
			if (const auto textureState = cache.Get_State(load.texture); textureState == Assets::AssetState::Loading || textureState == Assets::AssetState::Unloaded)
				continue;
		if (state == Assets::AssetState::Ready && load.animationSource.valid() && load.animationSource.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			continue;
		std::string error;
		std::string animationError;
		const Assets::ModelAsset *asset = state == Assets::AssetState::Ready ? cache.Try_Get_Model(load.handle) : nullptr;
		const auto source = load.animationSource.valid() ? load.animationSource.get() : nullptr;
		if (asset != nullptr && BindModel(library.entries[entry], *asset, source.get(), error, animationError))
		{
			load.status = ModelStatus::Ready;
			if (!animationError.empty())
				library.failures += "  " + library.entries[entry].request.name + " (animation): " + animationError + "\n";
			continue;
		}
		if (error.empty())
			error = cache.Get_Error(load.handle);
		load.status = ModelStatus::Failed;
		library.failures += "  " + library.keys[entry] + ": " + error + "\n";
	}
}

// Waits for every entry asked for so far and binds them (as the original's load screen loads the map's models).
inline void WaitForModels(ModelLibrary &library, Assets::AssetCache &cache)
{
	for (const ModelLoad &load : library.loads)
	{
		cache.Wait(load.handle);
		if (load.animationSource.valid())
			load.animationSource.wait();
	}
	PollModels(library, cache);
}

// A look's animation clip once its model has loaded (its frames and their rate; zero frames when it has none or
// failed); none while it loads or before it was asked for.
inline std::optional<std::pair<float, float>> ClipOfLook(const ModelLibrary &library, std::uint32_t look)
{
	if (look >= library.entryOfLook.size() || library.entryOfLook[look] == ModelLibrary::Unresolved)
		return std::nullopt;
	const std::uint32_t entry = library.entryOfLook[look];
	if (entry == ModelLibrary::NoModel || library.loads[entry].status == ModelStatus::Failed)
		return std::pair{0.0f, 0.0f};
	if (library.loads[entry].status == ModelStatus::Loading)
		return std::nullopt;
	const ModelEntry &model = library.entries[entry];
	if (!model.animation || !model.animation->valid)
		return std::pair{0.0f, 0.0f};
	return std::pair{static_cast<float>(model.animation->frameCount), model.animation->frameRate};
}

// A bone of a look's model in model space `seconds` into its animation (started `start` of the way in): its position
// and turn about z; none while the model loads, has no animation or no such bone.
inline std::optional<std::pair<std::array<float, 3>, float>> AnimatedBoneOf(ModelLibrary &library, std::uint32_t look, float seconds, float start,
	std::string_view bone)
{
	if (look >= library.entryOfLook.size() || library.entryOfLook[look] >= library.entries.size())
		return std::nullopt;
	const std::uint32_t entry = library.entryOfLook[look];
	ModelEntry &model = library.entries[entry];
	if (library.loads[entry].status != ModelStatus::Ready || !model.animation || !model.animation->valid)
		return std::nullopt;
	bool failed = false;
	const Graphics::ModelAssetPose *pose = PoseOf(model, seconds, start, failed);
	if (pose == nullptr)
		return std::nullopt;
	const std::size_t index = pose->Bone_Index(bone);
	Graphics::RenderTransform transform;
	if (index >= pose->Bone_Count() || !pose->Bone_Transform(index, transform))
		return std::nullopt;
	const auto &m = transform.matrix;
	return std::pair{std::array<float, 3>{m[3], m[7], m[11]}, std::atan2(m[4], m[0])};
}

// A bone's whole transform in model space (row-major 3x4) `seconds` into its look's animation (Get_Bone_Transform): its
// animated pose, else its rest pose; the root (identity) for a bone it does not have, as W3D's Get_Bone_Index answers
// 0; none while the model loads.
inline std::optional<std::array<float, 12>> BoneTransformOf(ModelLibrary &library, std::uint32_t look, float seconds, float start, std::string_view bone)
{
	if (look >= library.entryOfLook.size() || library.entryOfLook[look] >= library.entries.size())
		return std::nullopt;
	const std::uint32_t entry = library.entryOfLook[look];
	ModelEntry &model = library.entries[entry];
	if (library.loads[entry].status != ModelStatus::Ready)
		return std::nullopt;
	std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	bool failed = false;
	const Graphics::ModelAssetPose *pose = PoseOf(model, seconds, start, failed);
	if (pose == nullptr && model.controlled)
		pose = &*model.controlled;
	if (pose == nullptr)
		return identity;
	const std::size_t index = pose->Bone_Index(bone);
	Graphics::RenderTransform transform;
	if (index >= pose->Bone_Count() || !pose->Bone_Transform(index, transform))
		return identity;
	const auto &m = transform.matrix;
	return std::array<float, 12>{m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]};
}

inline std::string ModelSummary(const ModelLibrary &library)
{
	std::size_t ready = 0, loading = 0, failed = 0, animated = 0;
	for (std::size_t entry = 0; entry < library.entries.size(); ++entry)
	{
		const ModelStatus status = library.loads[entry].status;
		(status == ModelStatus::Ready ? ready : status == ModelStatus::Loading ? loading : failed)++;
		animated += library.entries[entry].animation.has_value();
	}
	return std::to_string(library.entries.size()) + " models: " + std::to_string(ready) + " ready, " + std::to_string(loading) + " loading, " +
		std::to_string(failed) + " failed, " + std::to_string(animated) + " animated";
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::ModelLibrary>
{
	static constexpr std::string_view StableName = "generalszh.presentation.model_library";
};
template<>
struct ResourceTraits<generalszh::presentation::ModelAssets>
{
	static constexpr std::string_view StableName = "generalszh.presentation.model_assets";
};
}
