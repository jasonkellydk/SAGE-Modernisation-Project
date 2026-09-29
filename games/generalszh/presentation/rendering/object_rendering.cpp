module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

module games.generalszh.presentation.rendering.object_rendering;

import Engine.Core.Math.FixedPresentation;
import Assets.Runtime;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import Assets.Models;
import Assets.States;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Lighting.Local;
import Graphics.Scene.Props.LightingParameters;

namespace generalszh::presentation
{
namespace
{
enum class ModelStatus : std::uint8_t
{
	Loading,
	Ready,
	Failed,
};

// A model's default animation, sampled once per frame into a pose all its
// instances share.
struct ModelAnimation
{
	ObjectAnimationMode mode{ObjectAnimationMode::Once};
	bool repeat{false};
	float frameRate{0};
	std::uint32_t frameCount{0};
	Graphics::ModelAssetPose pose;
	// The clip frame the pose holds (instances at the same frame share it).
	float evaluatedFrame{-1.0f};
	bool valid{false};
};

struct AnimationRequest
{
	std::string name; // empty: the model does not animate
	ObjectAnimationMode mode{ObjectAnimationMode::Once};
	bool repeat{false};
};

struct Model
{
	ModelStatus status{ModelStatus::Loading};
	Assets::ModelAssetHandle handle;
	Assets::TextureAssetHandle texture; // its base maps' replacement (none: its own)
	std::unique_ptr<Graphics::PropAssetBinding> binding;
	AnimationRequest request;
	// What the look shows of the model's parts: hidden ones never draw, muzzle
	// flashes only while their weapon fires (both resolved when the model binds).
	std::vector<std::string> hideNames;
	std::vector<std::string> showNames;
	std::vector<std::string> muzzleNames;
	std::vector<std::uint8_t> partHidden;
	std::vector<std::uint8_t> partMuzzle;
	std::vector<std::uint8_t> partTread; // 0: not a tread; 1: left (or middle); 2: right
	std::vector<std::uint8_t> partHeadlight; // HEADLIGHT sub-objects: shown only at night
	// Loaded projectiles (ProjectileBoneFeedbackEnabledSlots): per part, its slot + 1 in the high nibble and which of
	// the slot's projectiles it is (1-based; 15: the slot's single hide/show sub-object), 0: none; the names to find.
	std::vector<std::uint8_t> partProjectile;
	std::uint8_t projectileSlots{0};
	std::array<std::string, 3> projectileBones;
	std::array<std::string, 3> projectileHideShow;
	// The animation's source file, read in the background alongside the model.
	std::shared_future<std::shared_ptr<const Assets::ModelRigDesc>> animationSource;
	std::optional<ModelAnimation> animation;
	// Tires turned per instance: their bones, and a pose to turn them in
	// (reused instance after instance: parts draw at once).
	std::vector<std::string> tireNames;
	std::vector<std::string> steeredNames;
	std::string cabName;
	std::string trailerName;
	std::vector<std::size_t> tireBones;
	std::vector<std::uint8_t> tireSteers; // per tire bone: it also steers
	std::vector<std::uint8_t> tireCorners; // per tire bone: its suspension corner
	std::vector<std::uint8_t> cornerNames; // per tire name: its corner
	std::size_t cabBone{~std::size_t{0}};
	std::size_t trailerBone{~std::size_t{0}};
	std::string turretName;
	std::string turretPitchName;
	float turretArtAngle{0.0f};
	float turretArtPitch{0.0f};
	std::size_t turretBone{~std::size_t{0}};
	std::size_t turretPitchBone{~std::size_t{0}};
	std::string altTurretName;
	std::string altTurretPitchName;
	float altTurretArtAngle{0.0f};
	float altTurretArtPitch{0.0f};
	std::size_t altTurretBone{~std::size_t{0}};
	std::size_t altTurretPitchBone{~std::size_t{0}};
	// The bones muzzle flashes hang on (the original shows the sub-object on each),
	// and the barrels' recoil bones, in barrel order.
	std::vector<std::size_t> muzzleBones;
	std::string recoilName;
	std::vector<std::size_t> recoilBones;
	std::optional<Graphics::ModelAssetPose> controlled;
};

// The clip `name` ("<skeleton>.<clip>") among `clips`.
const Assets::ModelAnimationDesc *FindClip(const std::vector<Assets::ModelAnimationDesc> &clips, std::string_view name)
{
	const std::string wanted = Assets::Canonicalize_Asset_Name(name);
	for (const auto &clip : clips)
		if (Assets::Canonicalize_Asset_Name(clip.skeleton_name + "." + clip.name) == wanted)
			return &clip;
	const auto dot = wanted.find('.');
	const std::string clipName = dot == std::string::npos ? wanted : wanted.substr(dot + 1);
	for (const auto &clip : clips)
		if (Assets::Canonicalize_Asset_Name(clip.name) == clipName)
			return &clip;
	return clips.size() == 1 ? &clips.front() : nullptr;
}

// Binds the clip to the model's resolved skeleton for pose sampling.
bool PrepareAnimation(const Assets::ModelAsset &asset, const Assets::ModelRigDesc *source, const AnimationRequest &request,
	ModelAnimation &result, std::string &error)
{
	const Assets::ModelRigDesc &modelRig = asset.Rig();
	if (modelRig.bones.empty())
	{
		error = "model has no skeleton to animate";
		return false;
	}
	const Assets::ModelAnimationDesc *clip = FindClip(modelRig.animations, request.name);
	if (clip == nullptr && source != nullptr)
		clip = FindClip(source->animations, request.name);
	if (clip == nullptr)
	{
		error = "no such clip";
		return false;
	}
	// W3D plays a clip onto a hierarchy by pivot index (HRawAnimClass channels), so a clip made for another
	// skeleton plays too when that skeleton has every pivot it moves (a damaged model's own hierarchy under a
	// pristine model's clip, as the combat bike's turns).
	if (Assets::Canonicalize_Asset_Name(clip->skeleton_name) != Assets::Canonicalize_Asset_Name(modelRig.skeleton_name) &&
		std::any_of(clip->channels.begin(), clip->channels.end(), [&](const Assets::ModelAnimationChannel &channel) { return channel.bone >= modelRig.bones.size(); }))
	{
		error = "clip is for skeleton " + clip->skeleton_name + ", model uses " + modelRig.skeleton_name;
		return false;
	}
	Assets::ModelRigDesc rig;
	rig.skeleton_name = modelRig.skeleton_name;
	rig.bones = modelRig.bones;
	Assets::ModelAnimationDesc bound = *clip;
	bound.skeleton_name = modelRig.skeleton_name;
	// Keep the channels this skeleton has; the pose samples translation,
	// quaternion rotation and visibility channels.
	std::erase_if(bound.channels, [&](const Assets::ModelAnimationChannel &channel) {
		return channel.bone >= rig.bones.size() || channel.component >= Assets::ModelChannelComponent::RotationX;
	});
	rig.animations.push_back(std::move(bound));
	if (!result.pose.Initialize(rig, error))
		return false;
	result.mode = request.mode;
	result.repeat = request.repeat;
	result.frameRate = clip->frame_rate;
	result.frameCount = clip->frame_count;
	result.valid = true;
	return true;
}

// The clip frame at `seconds` for the animation's mode; `loop` when it wraps.
float ClipFrame(const ModelAnimation &animation, double seconds, bool &loop)
{
	const double count = animation.frameCount;
	const double frames = seconds * animation.frameRate;
	const double last = count - 1;
	loop = false;
	switch (animation.mode)
	{
	case ObjectAnimationMode::Manual:
		return 0;
	case ObjectAnimationMode::Loop:
		loop = true;
		return static_cast<float>(std::fmod(frames, count));
	case ObjectAnimationMode::LoopBackwards:
		loop = true;
		return static_cast<float>(last - std::fmod(frames, count));
	case ObjectAnimationMode::LoopPingPong: {
		if (last <= 0)
			return 0;
		const double phase = std::fmod(frames, 2 * last);
		return static_cast<float>(phase <= last ? phase : 2 * last - phase);
	}
	case ObjectAnimationMode::Once:
		return static_cast<float>(animation.repeat ? std::fmod(frames, count) : std::min(frames, last));
	case ObjectAnimationMode::OnceBackwards:
		return static_cast<float>(animation.repeat ? last - std::fmod(frames, count) : std::max(last - frames, 0.0));
	}
	return 0;
}
}

struct ObjectRendering::State
{
	std::map<std::string, Model> models;
	// Per look: its model, or null when it has none (resolved once).
	std::vector<std::optional<Model *>> byLook;
	std::string failures;

	Model *ModelOf(std::uint32_t lookId, const ModelLookup &modelFor, Assets::AssetCache &cache)
	{
		if (lookId >= byLook.size())
			byLook.resize(lookId + 1);
		auto &slot = byLook[lookId];
		if (!slot)
		{
			const ObjectModel look = modelFor ? modelFor(lookId) : ObjectModel{};
			if (look.model.empty())
				slot = nullptr;
			else
			{
				// Looks sharing a model but not its animation or visible parts draw apart.
				std::string key = look.animation.empty() ? look.model : look.model + "|" + look.animation;
				for (const auto *names : {&look.hidden, &look.shown, &look.muzzleFlashes, &look.tires, &look.steeredTires})
				{
					key += "|";
					for (const auto &name : *names)
						key += name + ",";
				}
				key += "|" + std::to_string(look.projectileSlots);
				for (std::size_t slot = 0; slot < 3; ++slot)
					key += "," + look.projectileBones[slot] + "/" + look.projectileHideShow[slot];
				key += "|" + look.turret + "," + look.turretPitch + "," + std::to_string(look.turretArtAngle) + "," + std::to_string(look.turretArtPitch) + "," + look.recoilBone +
					"|" + look.altTurret + "," + look.altTurretPitch + "," + std::to_string(look.altTurretArtAngle) + "," + std::to_string(look.altTurretArtPitch) +
					"|" + look.texture;
				auto [it, inserted] = models.try_emplace(key);
				Model &model = it->second;
				if (inserted)
				{
					model.hideNames = look.hidden;
					model.projectileSlots = look.projectileSlots;
					model.projectileBones = look.projectileBones;
					model.projectileHideShow = look.projectileHideShow;
					model.showNames = look.shown;
					model.muzzleNames = look.muzzleFlashes;
					model.tireNames = look.tires;
					model.cornerNames = look.tireCorners;
					model.steeredNames = look.steeredTires;
					model.cabName = look.cab;
					model.trailerName = look.trailer;
					model.turretName = look.turret;
					model.turretPitchName = look.turretPitch;
					model.turretArtAngle = look.turretArtAngle;
					model.turretArtPitch = look.turretArtPitch;
					model.altTurretName = look.altTurret;
					model.altTurretPitchName = look.altTurretPitch;
					model.altTurretArtAngle = look.altTurretArtAngle;
					model.altTurretArtPitch = look.altTurretArtPitch;
					model.recoilName = look.recoilBone;
					model.handle = cache.Request_Model(look.model + ".w3d");
					if (!look.texture.empty())
						model.texture = cache.Request_Texture(look.texture);
					if (!look.animation.empty())
					{
						model.request = {look.animation, look.animationMode, look.repeat};
						Assets::AssetCache *source = &cache;
						model.animationSource = std::async(std::launch::async, [source, name = look.animation] {
							std::string error;
							return source->Load_Rig(Assets::AssetType::Animation, name, error);
						}).share();
					}
				}
				slot = &model;
			}
		}
		return *slot;
	}

	// Binds models that finished loading since the last frame (never waits).
	void Poll(Graphics::Device &device, Assets::AssetCache &cache)
	{
		for (auto &[name, model] : models)
		{
			if (model.status != ModelStatus::Loading)
				continue;
			const auto state = cache.Get_State(model.handle);
			if (state == Assets::AssetState::Loading || state == Assets::AssetState::Unloaded)
				continue;
			if (model.texture.Is_Valid())
				if (const auto textureState = cache.Get_State(model.texture); textureState == Assets::AssetState::Loading || textureState == Assets::AssetState::Unloaded)
					continue;
			if (state == Assets::AssetState::Ready && model.animationSource.valid() &&
				model.animationSource.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				continue;
			std::string error;
			auto binding = std::make_unique<Graphics::PropAssetBinding>();
			// A replacement texture that failed to load leaves the model's own.
			const Assets::TextureAssetHandle replacement = model.texture.Is_Valid() && cache.Get_State(model.texture) == Assets::AssetState::Ready ? model.texture
				: Assets::TextureAssetHandle{};
			if (state == Assets::AssetState::Ready && binding->Load(device, Graphics::Get_Prop_Renderer(), cache, model.handle, error, replacement))
			{
				model.binding = std::move(binding);
				model.status = ModelStatus::Ready;
				BindBones(model, cache);
				BindParts(model, cache);
				BindAnimation(model, cache);
				continue;
			}
			if (error.empty())
				error = cache.Get_Error(model.handle);
			model.status = ModelStatus::Failed;
			failures += "  " + name + ": " + error + "\n";
		}
	}

	static char Upper(char c) noexcept { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }

	static bool SameName(std::string_view a, std::string_view b) noexcept
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Upper(x) == Upper(y); });
	}

	static bool StartsWith(std::string_view name, std::string_view prefix) noexcept
	{
		return name.size() >= prefix.size() && SameName(name.substr(0, prefix.size()), prefix);
	}

	void BindParts(Model &model, Assets::AssetCache &cache)
	{
		const std::size_t parts = model.binding->Part_Count();
		model.partHidden.assign(parts, 0);
		model.partProjectile.assign(parts, 0);
		model.partMuzzle.assign(parts, 0);
		model.partTread.assign(parts, 0);
		model.partHeadlight.assign(parts, 0);
		for (std::size_t part = 0; part < parts; ++part)
		{
			const std::string_view name = model.binding->Part_Name(part);
			// As the original's hideAllHeadlights: any sub-object whose name has HEADLIGHT.
			model.partHeadlight[part] = name.find("HEADLIGHT") != std::string_view::npos ? 1 : 0;
			if (StartsWith(name, "TREADS"))
				model.partTread[part] = name.size() > 6 && Upper(name[6]) == 'R' ? 2 : 1;
			for (const auto &muzzle : model.muzzleNames)
				model.partMuzzle[part] |= StartsWith(name, muzzle) ? 1 : 0;
			// The barrel a flash belongs to (1-based; 255: a flash of no one barrel, shown with any).
			if (model.partMuzzle[part] != 0)
				model.partMuzzle[part] = 255;
			const std::uint32_t bone = model.binding->Part_Bone(part);
			if (const auto found = std::find(model.muzzleBones.begin(), model.muzzleBones.end(), bone); found != model.muzzleBones.end())
				model.partMuzzle[part] = static_cast<std::uint8_t>(std::min<std::ptrdiff_t>(found - model.muzzleBones.begin() + 1, 254));
			for (const auto &hidden : model.hideNames)
				model.partHidden[part] |= SameName(name, hidden) ? 1 : 0;
			for (const auto &shown : model.showNames)
				if (SameName(name, shown))
					model.partHidden[part] = model.partMuzzle[part] = 0;
		}
		BindProjectiles(model, cache);
	}

	// W3DModelDraw::doHideShowProjectileObjects: a slot's projectiles are its launch bone's NAME01, NAME02... (or its
	// one WeaponHideShowBone sub-object); hiding one hides every sub-object hanging below its bone
	// (doHideShowBoneSubObjs).
	void BindProjectiles(Model &model, Assets::AssetCache &cache)
	{
		if (model.projectileSlots == 0)
			return;
		const auto *asset = cache.Try_Get_Model(model.handle);
		const auto *rig = asset != nullptr ? &asset->Rig() : nullptr;
		const auto below = [&](std::uint32_t bone, std::uint32_t ancestor) {
			if (rig == nullptr)
				return false;
			for (std::uint32_t current = bone; current < rig->bones.size() && rig->bones[current].parent != Assets::ModelRootParent;)
			{
				current = rig->bones[current].parent;
				if (current == ancestor)
					return true;
			}
			return false;
		};
		const std::size_t parts = model.binding->Part_Count();
		for (std::uint8_t slot = 0; slot < 3; ++slot)
		{
			if ((model.projectileSlots & (1u << slot)) == 0)
				continue;
			for (std::size_t part = 0; part < parts; ++part)
			{
				const std::string_view name = model.binding->Part_Name(part);
				std::uint8_t which = 0;
				if (!model.projectileHideShow[slot].empty())
					which = SameName(name, model.projectileHideShow[slot]) ? 15 : 0;
				else if (!model.projectileBones[slot].empty())
					for (std::uint8_t index = 1; index < 15 && which == 0; ++index)
					{
						char suffix[4];
						std::snprintf(suffix, sizeof(suffix), "%02d", index);
						which = SameName(name, model.projectileBones[slot] + suffix) ? index : 0;
					}
				if (which == 0)
					continue;
				const std::uint8_t code = static_cast<std::uint8_t>(((slot + 1u) << 4) | which);
				model.partProjectile[part] = code;
				const std::uint32_t bone = model.binding->Part_Bone(part);
				for (std::size_t child = 0; child < parts; ++child)
					if (child != part && below(model.binding->Part_Bone(child), bone))
						model.partProjectile[child] = code;
			}
		}
	}

	// Whether a part is a projectile fired from its slot (hidden: the first `hidden` of them; 255: the slot's one).
	static bool ProjectileHidden(std::uint8_t code, const ObjectInstance &instance) noexcept
	{
		if (code == 0)
			return false;
		const std::uint8_t hidden = instance.projectilesHidden[((code >> 4) - 1u) % 3u];
		const std::uint8_t which = code & 15u;
		return which == 15 ? hidden != 0 : which <= hidden;
	}

	// A model whose animation cannot play still draws, at rest.
	void BindAnimation(Model &model, Assets::AssetCache &cache)
	{
		if (model.request.name.empty())
			return;
		const auto *asset = cache.Try_Get_Model(model.handle);
		const auto source = model.animationSource.valid() ? model.animationSource.get() : nullptr;
		ModelAnimation animation;
		std::string error;
		if (asset != nullptr && PrepareAnimation(*asset, source.get(), model.request, animation, error))
			model.animation = std::move(animation);
		else
			failures += "  " + model.request.name + " (animation): " + error + "\n";
	}

	// Finds the model's tire bones and readies a pose to turn them in.
	// The bones the model turns per instance (tires, cab, trailer, turret) and those its flashes hang on.
	void BindBones(Model &model, Assets::AssetCache &cache)
	{
		if (model.tireNames.empty() && model.cabName.empty() && model.trailerName.empty() && model.turretName.empty() &&
			model.turretPitchName.empty() && model.muzzleNames.empty() && model.recoilName.empty() && model.altTurretName.empty() &&
			model.altTurretPitchName.empty())
			return;
		const auto *asset = cache.Try_Get_Model(model.handle);
		if (asset == nullptr || asset->Rig().bones.empty())
			return;
		Assets::ModelRigDesc rig;
		rig.skeleton_name = asset->Rig().skeleton_name;
		rig.bones = asset->Rig().bones;
		Graphics::ModelAssetPose pose;
		std::string error;
		if (!pose.Initialize(rig, error))
			return;
		for (std::size_t tire = 0; tire < model.tireNames.size(); ++tire)
			if (const auto &name = model.tireNames[tire]; pose.Bone_Index(name) < pose.Bone_Count())
			{
				const std::size_t bone = pose.Bone_Index(name);
				model.tireBones.push_back(bone);
				model.tireCorners.push_back(tire < model.cornerNames.size() ? model.cornerNames[tire] : 0);
				model.tireSteers.push_back(std::find(model.steeredNames.begin(), model.steeredNames.end(), name) != model.steeredNames.end() ? 1 : 0);
			}
		if (!model.cabName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.cabName); bone < pose.Bone_Count())
				model.cabBone = bone;
		if (!model.trailerName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.trailerName); bone < pose.Bone_Count())
				model.trailerBone = bone;
		if (!model.turretName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.turretName); bone < pose.Bone_Count())
				model.turretBone = bone;
		if (!model.turretPitchName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.turretPitchName); bone < pose.Bone_Count())
				model.turretPitchBone = bone;
		if (!model.altTurretName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.altTurretName); bone < pose.Bone_Count())
				model.altTurretBone = bone;
		if (!model.altTurretPitchName.empty())
			if (const std::size_t bone = pose.Bone_Index(model.altTurretPitchName); bone < pose.Bone_Count())
				model.altTurretPitchBone = bone;
		// As the original's barrels: NAME01, NAME02... while they exist, else NAME itself.
		for (const auto &muzzle : model.muzzleNames)
		{
			bool numbered = false;
			for (int index = 1; index <= 99; ++index)
			{
				char suffix[4];
				std::snprintf(suffix, sizeof(suffix), "%02d", index);
				const std::size_t bone = pose.Bone_Index(muzzle + suffix);
				if (bone >= pose.Bone_Count())
					break;
				model.muzzleBones.push_back(bone);
				numbered = true;
			}
			if (!numbered)
				if (const std::size_t bone = pose.Bone_Index(muzzle); bone < pose.Bone_Count())
					model.muzzleBones.push_back(bone);
		}
		if (!model.recoilName.empty())
		{
			for (int index = 1; index <= 99; ++index)
			{
				char suffix[4];
				std::snprintf(suffix, sizeof(suffix), "%02d", index);
				const std::size_t bone = pose.Bone_Index(model.recoilName + suffix);
				if (bone >= pose.Bone_Count())
					break;
				model.recoilBones.push_back(bone);
			}
			if (model.recoilBones.empty())
				if (const std::size_t bone = pose.Bone_Index(model.recoilName); bone < pose.Bone_Count())
					model.recoilBones.push_back(bone);
		}
		if (!model.tireBones.empty() || model.cabBone < pose.Bone_Count() || model.trailerBone < pose.Bone_Count() ||
			model.turretBone < pose.Bone_Count() || model.turretPitchBone < pose.Bone_Count() || !model.recoilBones.empty() ||
			model.altTurretBone < pose.Bone_Count() || model.altTurretPitchBone < pose.Bone_Count())
			model.controlled.emplace(std::move(pose));
	}

	// The model's pose `seconds` into its animation, or null for its rest pose.
	// Its model's pose at its time, with its controlled bones (tires, cab, trailer, turrets, barrels) turned.
	const Graphics::ModelAssetPose *InstancePose(Model &modelRef, const ObjectInstance &instance)
	{
		Model *model = &modelRef;
	const Graphics::ModelAssetPose *pose = PoseOf(*model, instance.animationSeconds, instance.animationStart);
	// Tires turn about their axles (y) on top of the model's pose.
	bool recoiling = false;
	for (const float shift : instance.recoil)
		recoiling = recoiling || shift != 0.0f;
	const bool turretTurns = model->turretBone != ~std::size_t{0} || model->turretPitchBone != ~std::size_t{0} || (recoiling && !model->recoilBones.empty()) ||
		model->altTurretBone != ~std::size_t{0} || model->altTurretPitchBone != ~std::size_t{0};
	const bool suspended = instance.suspension[0] != 0.0f || instance.suspension[1] != 0.0f || instance.suspension[2] != 0.0f || instance.suspension[3] != 0.0f;
	if (model->controlled && (instance.wheels != 0.0f || instance.rearWheels != 0.0f || suspended || instance.steer != 0.0f || instance.cab != 0.0f || instance.trailer != 0.0f || turretTurns))
	{
		const auto aboutZ = [](float angle) {
			const float c = std::cos(angle), s = std::sin(angle);
			return Graphics::RenderTransform{{c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
		};
		const float c = std::cos(instance.wheels), s = std::sin(instance.wheels);
		const Graphics::RenderTransform roll{{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1}};
		const float rc = std::cos(instance.rearWheels), rs = std::sin(instance.rearWheels);
		const Graphics::RenderTransform rearRoll{{rc, 0, rs, 0, 0, 1, 0, 0, -rs, 0, rc, 0, 0, 0, 0, 1}};
		// Steered tires: turned about z, then rolling about their (steered) axle.
		const Graphics::RenderTransform steer = aboutZ(instance.steer);
		Graphics::RenderTransform steeredRoll{};
		for (unsigned r = 0; r < 4; ++r)
			for (unsigned col = 0; col < 4; ++col)
			{
				float value = 0;
				for (unsigned k = 0; k < 4; ++k)
					value += steer.matrix[r * 4 + k] * roll.matrix[k * 4 + col];
				steeredRoll.matrix[r * 4 + col] = value;
			}
		std::array<std::pair<std::size_t, Graphics::RenderTransform>, 16> controls{};
		std::size_t count = 0;
		for (std::size_t tire = 0; tire < model->tireBones.size(); ++tire)
			if (count < controls.size())
			{
				// W3DTruckDraw: raised by its corner's suspension offset (in its own frame), then steered and rolled.
				Graphics::RenderTransform control = model->tireSteers[tire] != 0 ? steeredRoll : rearRoll;
				control.matrix[11] += instance.suspension[std::min<std::size_t>(model->tireCorners[tire], 3)];
				controls[count++] = {model->tireBones[tire], control};
			}
		if (model->cabBone != ~std::size_t{0} && count < controls.size())
			controls[count++] = {model->cabBone, aboutZ(instance.cab)};
		if (model->trailerBone != ~std::size_t{0} && count < controls.size())
			controls[count++] = {model->trailerBone, aboutZ(instance.trailer)};
		// As the original: the turret bone turned by its aim about z, the pitch bone about -y.
		if (model->turretBone != ~std::size_t{0} && count < controls.size())
			controls[count++] = {model->turretBone, aboutZ(instance.turret + model->turretArtAngle)};
		if (model->turretPitchBone != ~std::size_t{0} && count < controls.size())
		{
			const float p = -(instance.turretPitch + model->turretArtPitch);
			const float cp = std::cos(p), sp = std::sin(p);
			controls[count++] = {model->turretPitchBone, Graphics::RenderTransform{{cp, 0, sp, 0, 0, 1, 0, 0, -sp, 0, cp, 0, 0, 0, 0, 1}}};
		}
		// The second turret the same way (AltTurret / AltTurretPitch).
		if (model->altTurretBone != ~std::size_t{0} && count < controls.size())
			controls[count++] = {model->altTurretBone, aboutZ(instance.altTurret + model->altTurretArtAngle)};
		if (model->altTurretPitchBone != ~std::size_t{0} && count < controls.size())
		{
			const float p = -(instance.altTurretPitch + model->altTurretArtPitch);
			const float cp = std::cos(p), sp = std::sin(p);
			controls[count++] = {model->altTurretPitchBone, Graphics::RenderTransform{{cp, 0, sp, 0, 0, 1, 0, 0, -sp, 0, cp, 0, 0, 0, 0, 1}}};
		}
		// Each barrel back along its x by its recoil (handleClientRecoil's gun transform).
		for (std::size_t barrel = 0; barrel < model->recoilBones.size() && barrel < instance.recoil.size(); ++barrel)
			if (instance.recoil[barrel] != 0.0f && count < controls.size())
				controls[count++] = {model->recoilBones[barrel], Graphics::RenderTransform{{1, 0, 0, -instance.recoil[barrel], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
		if (model->controlled->Evaluate_Controlled(pose, std::span(controls.data(), count)))
			pose = &*model->controlled;
	}
		return pose;
	}

	const Graphics::ModelAssetPose *PoseOf(Model &model, double seconds, float start = 0.0f)
	{
		if (!model.animation || !model.animation->valid)
			return nullptr;
		ModelAnimation &animation = *model.animation;
		// Started part-way (RANDOMSTART, START_FRAME_LAST): as if it had run that far already.
		if (start > 0.0f && animation.frameRate > 0.0f && animation.frameCount > 1)
			seconds += static_cast<double>(start) * static_cast<double>(animation.frameCount - 1) / static_cast<double>(animation.frameRate);
		bool loop = false;
		const float clipFrame = ClipFrame(animation, seconds, loop);
		if (animation.evaluatedFrame != clipFrame)
		{
			animation.evaluatedFrame = clipFrame;
			if (!animation.pose.Evaluate(0, clipFrame, loop))
			{
				animation.valid = false;
				failures += "  " + model.request.name + " (animation): pose sampling failed\n";
			}
		}
		return animation.valid ? &animation.pose : nullptr;
	}
};

ObjectRendering::ObjectRendering() : m_state(std::make_unique<State>()) {}
ObjectRendering::~ObjectRendering() = default;

const std::string &ObjectRendering::Failures() const noexcept { return m_state->failures; }

std::optional<std::pair<float, float>> ObjectRendering::ClipOf(std::uint32_t look) const
{
	const auto &byLook = m_state->byLook;
	if (look >= byLook.size() || !byLook[look])
		return std::nullopt;
	const Model *model = *byLook[look];
	if (model == nullptr || model->status == ModelStatus::Failed)
		return std::pair{0.0f, 0.0f};
	if (model->status == ModelStatus::Loading)
		return std::nullopt;
	if (!model->animation || !model->animation->valid)
		return std::pair{0.0f, 0.0f};
	return std::pair{static_cast<float>(model->animation->frameCount), model->animation->frameRate};
}

std::string ObjectRendering::Summary() const
{
	std::size_t ready = 0, loading = 0, failed = 0, animated = 0;
	for (const auto &[name, model] : m_state->models)
	{
		(model.status == ModelStatus::Ready ? ready : model.status == ModelStatus::Loading ? loading : failed)++;
		animated += model.animation.has_value();
	}
	return std::to_string(m_state->models.size()) + " models: " + std::to_string(ready) + " ready, " + std::to_string(loading) + " loading, " +
		std::to_string(failed) + " failed, " + std::to_string(animated) + " animated";
}

std::size_t ObjectRendering::ReadyModelCount() const noexcept
{
	std::size_t count = 0;
	for (const auto &[name, model] : m_state->models)
		count += model.status == ModelStatus::Ready;
	return count;
}

void ObjectRendering::Preload(Graphics::Device &device, std::span<const ObjectInstance> instances, const ModelLookup &modelFor)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	for (const ObjectInstance &instance : instances)
		m_state->ModelOf(instance.look, modelFor, *cache);
	for (auto &[name, model] : m_state->models)
	{
		cache->Wait(model.handle);
		if (model.animationSource.valid())
			model.animationSource.wait();
	}
	m_state->Poll(device, *cache);
}

void ObjectRendering::Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection,
	const std::array<float, 3> &eye, const engine::level::LightingSet *lighting, std::span<const ObjectInstance> instances,
	const ModelLookup &modelFor, std::span<const ShownLight> lights)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	State &state = *m_state;
	// Resolve every look first so new models start loading this frame.
	for (const ObjectInstance &instance : instances)
		state.ModelOf(instance.look, modelFor, *cache);
	state.Poll(device, *cache);

	Graphics::PropParameters parameters;
	parameters.view_projection = viewProjection;
	parameters.camera_position = {eye[0], eye[1], eye[2], 1};
	if (lighting != nullptr && !lighting->objects.empty())
	{
		const auto &ambient = lighting->objects[0].ambient;
		parameters.scene_ambient = {Engine::Math::ToFloat(ambient[0]), Engine::Math::ToFloat(ambient[1]), Engine::Math::ToFloat(ambient[2]), 1};
		// The original applies GameData NumberGlobalLights (3) global lights.
		for (std::size_t index = 0; index < lighting->objects.size() && index < 3; ++index)
		{
			const auto &light = lighting->objects[index];
			// Map lights store the direction they shine; the shader takes the direction towards the light. w enables it.
			parameters.light_direction[index] = {-Engine::Math::ToFloat(light.direction.x), -Engine::Math::ToFloat(light.direction.y),
				-Engine::Math::ToFloat(light.direction.z), 1};
			parameters.light_diffuse[index] = {Engine::Math::ToFloat(light.diffuse[0]), Engine::Math::ToFloat(light.diffuse[1]),
				Engine::Math::ToFloat(light.diffuse[2]), 1};
		}
	}
	// W3DScene's per-object light environment: the scene ambient and global lights, then (if it takes them) each enabled
	// dynamic light whose sphere reaches its own; the four strongest light it (LocalLighting, Set_Prop_Lighting).
	std::array<float, 3> ambient{};
	std::vector<Graphics::MaterialLightSource> globals;
	if (lighting != nullptr && !lighting->objects.empty())
	{
		const auto &first = lighting->objects[0].ambient;
		ambient = {Engine::Math::ToFloat(first[0]), Engine::Math::ToFloat(first[1]), Engine::Math::ToFloat(first[2])};
		for (std::size_t index = 0; index < lighting->objects.size() && index < 3; ++index)
		{
			const auto &light = lighting->objects[index];
			Graphics::MaterialLightSource source;
			source.type = Graphics::RenderLightType::Directional;
			// Map lights store the direction they shine; the shader takes the direction towards the light.
			source.direction = {-Engine::Math::ToFloat(light.direction.x), -Engine::Math::ToFloat(light.direction.y), -Engine::Math::ToFloat(light.direction.z)};
			source.diffuse = {Engine::Math::ToFloat(light.diffuse[0]), Engine::Math::ToFloat(light.diffuse[1]), Engine::Math::ToFloat(light.diffuse[2])};
			globals.push_back(source);
		}
	}
	for (const ObjectInstance &instance : instances)
	{
		Model *model = state.ModelOf(instance.look, modelFor, *cache);
		if (model == nullptr || model->status != ModelStatus::Ready)
			continue;
		if (lighting != nullptr)
		{
			Graphics::LocalLighting environment;
			const std::array<float, 3> centre{instance.lightSphere[0], instance.lightSphere[1], instance.lightSphere[2]};
			environment.Reset(centre, ambient);
			for (const Graphics::MaterialLightSource &source : globals)
				environment.Add(source);
			if (instance.receivesDynamicLights)
				for (const ShownLight &light : lights)
				{
					if (!LightReaches(light, instance.lightSphere))
						continue;
					Graphics::MaterialLightSource source;
					source.type = Graphics::RenderLightType::Point;
					source.position = light.at;
					source.ambient = light.ambient;
					source.diffuse = light.diffuse;
					source.attenuate = true;
					source.attenuation_start = light.nearRange;
					source.attenuation_end = light.farRange;
					environment.Add(source);
				}
			environment.Finalize();
			Graphics::Set_Prop_Lighting(parameters, &environment);
			// Darkened (a pushed-aside tree: the tree shader's colour * sway.y): its lights scaled, set afresh per object.
			if (instance.shade != 1.0f)
			{
				for (std::size_t channel = 0; channel < 3; ++channel)
					parameters.scene_ambient[channel] *= instance.shade;
				for (std::size_t index = 0; index < parameters.light_diffuse.size(); ++index)
					for (std::size_t channel = 0; channel < 3; ++channel)
					{
						parameters.light_diffuse[index][channel] *= instance.shade;
						parameters.light_ambient[index][channel] *= instance.shade;
					}
			}
		}
		const Graphics::ModelAssetPose *pose = state.InstancePose(*model, instance);
		parameters.world = instance.world;
		parameters.surface.team_color = instance.teamColor;
		// The original's effective opacity: an override of the model's own.
		parameters.vertex_material_override = {instance.opacity, 1, 0, instance.opacity < 1.0f ? 1.0f : 0.0f};
		for (std::size_t part = 0; part < model->binding->Part_Count(); ++part)
		{
			const std::uint8_t barrel = model->partMuzzle[part];
			const bool flashes = barrel == 255 || instance.flashBarrels == 0 ? instance.muzzleFlash : (instance.flashBarrels >> ((barrel - 1u) % 8u) & 1u) != 0;
			if (model->partHidden[part] || (barrel != 0 && !flashes) || (model->partHeadlight[part] && !instance.night) ||
				State::ProjectileHidden(model->partProjectile[part], instance))
				continue;
			// Treads roll their texture along U (stage 0); everything else as authored.
			const std::uint8_t tread = model->partTread[part];
			parameters.uv_transform[0][3] = tread == 0 ? 0.0f : instance.treads[tread - 1];
			if (!instance.heatOnly)
			{
				if (instance.opacity < 1.0f)
					model->binding->Draw_Part_Translucent(commands, part, parameters, instance.opacity, pose);
				else
					model->binding->Draw_Part(commands, part, parameters, pose);
			}
			// W3DScene's heat vision pass (m_heatVisionMaterialPass / m_heatVisionOnlyPass): additive, lit with diffuse
			// (0.02, 0.01, 0) and emissive (0.5, 0.2, 0) scaled by its strength; over its own drawing (depth equal), or
			// alone for an enemy (depth less-equal, no depth write).
			// Drawable::colorTint (an illegal placement's red): its colour added over it, lit as emissive.
			if (instance.tint[0] > 0.0f || instance.tint[1] > 0.0f || instance.tint[2] > 0.0f)
			{
				Graphics::PropParameters tinted = parameters;
				tinted.material_diffuse_replacement = {0.0f, 0.0f, 0.0f, 1.0f};
				tinted.material_emissive_replacement = {instance.tint[0], instance.tint[1], instance.tint[2], 1.0f};
				tinted.vertex_material_override = {1.0f, instance.opacity, 0.0f, 0.0f};
				Graphics::PropStyle pass;
				pass.source_blend = Graphics::RHIBlendFactor::One;
				pass.destination_blend = Graphics::RHIBlendFactor::One;
				pass.depth_write = false;
				pass.depth_comparison = Graphics::RHIComparison::LessEqual;
				model->binding->Draw_Part_Pass(commands, part, tinted, pass, pose);
			}
			if (instance.heatVision > 0.0f)
			{
				Graphics::PropParameters heat = parameters;
				heat.material_diffuse_replacement = {0.02f, 0.01f, 0.0f, 1.0f};
				heat.material_emissive_replacement = {0.5f, 0.2f, 0.0f, 1.0f};
				heat.vertex_material_override = {1.0f, instance.heatVision, 0.0f, 0.0f};
				Graphics::PropStyle pass;
				pass.source_blend = Graphics::RHIBlendFactor::One;
				pass.destination_blend = Graphics::RHIBlendFactor::One;
				pass.depth_write = false;
				pass.depth_comparison = instance.heatOnly ? Graphics::RHIComparison::LessEqual : Graphics::RHIComparison::Equal;
				model->binding->Draw_Part_Pass(commands, part, heat, pass, pose);
			}
		}
		parameters.uv_transform[0][3] = 0.0f;
	}
}

// W3DDirectionalShadows' Collect_Object: each instance that casts a shadow, its visible parts (not muzzle flashes or
// unlit headlights) as directional shadow casters in its pose.
void ObjectRendering::SubmitShadows(std::span<const ObjectInstance> instances, const ModelLookup &modelFor)
{
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return;
	State &state = *m_state;
	auto &submission = Graphics::Get_Prop_Submission();
	for (const ObjectInstance &instance : instances)
	{
		if (!instance.castsShadow || instance.opacity <= 0.0f)
			continue;
		Model *model = state.ModelOf(instance.look, modelFor, *cache);
		if (model == nullptr || model->status != ModelStatus::Ready)
			continue;
		const Graphics::ModelAssetPose *pose = state.InstancePose(*model, instance);
		Graphics::PropParameters parameters;
		parameters.world = instance.world;
		for (std::size_t part = 0; part < model->binding->Part_Count(); ++part)
		{
			if (model->partHidden[part] || model->partMuzzle[part] != 0 || (model->partHeadlight[part] && !instance.night) ||
				State::ProjectileHidden(model->partProjectile[part], instance))
				continue;
			model->binding->Submit_Part(submission, part, parameters, Graphics::PropDrawPhase::Shadow, {}, pose);
		}
	}
}
}
