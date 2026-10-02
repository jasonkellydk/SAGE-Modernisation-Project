export module games.generalszh.presentation.models.model_entry;
import std;

export import games.generalszh.presentation.objects.resources.look_models;
export import games.generalszh.presentation.objects.resources.object_instance;
export import Graphics.Scene.Models.AssetPose;
export import Graphics.Scene.Models.RayCast;
import Graphics.Scene.Models.PartBones;
import Graphics.Scene.Models.CollisionBuild;
import Assets.Models;
import Assets.Identity;

// A look's model as plain data (no GPU state): its parts (in drawing order), which of them show when (hidden parts,
// muzzle flashes, headlights, treads, loaded projectiles), the bones its object turns, its animation and its collision
// shape; and the free functions that bind it once its asset has loaded and pose it for an instance (W3DModelDraw's
// condition state, W3DTruckDraw's tires, turrets, recoil). The renderer draws its parts in that pose; picking casts
// rays against its collision shape in the same pose.
export namespace generalszh::presentation
{
// A model's default animation, sampled once per frame into a pose all its instances share.
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

struct ModelEntry
{
	AnimationRequest request;
	// What the look shows of the model's parts: hidden ones never draw, muzzle flashes only while their weapon fires
	// (both resolved when the model binds).
	std::vector<std::string> hideNames;
	std::vector<std::string> showNames;
	std::vector<std::string> muzzleNames;
	// Its parts in drawing order (PropAssetBinding's: one per submesh) and the bone each hangs on.
	std::vector<std::string> partNames;
	std::vector<std::uint32_t> partBones;
	std::vector<std::uint8_t> partHidden;
	std::vector<std::uint8_t> partMuzzle;
	std::vector<std::uint8_t> partTread;     // 0: not a tread; 1: left (or middle); 2: right
	std::vector<std::uint8_t> partHeadlight; // HEADLIGHT sub-objects: shown only at night
	// Loaded projectiles (ProjectileBoneFeedbackEnabledSlots): per part, its slot + 1 in the high nibble and which of
	// the slot's projectiles it is (1-based; 15: the slot's single hide/show sub-object), 0: none; the names to find.
	std::vector<std::uint8_t> partProjectile;
	std::uint8_t projectileSlots{0};
	std::array<std::string, 3> projectileBones;
	std::array<std::string, 3> projectileHideShow;
	std::optional<ModelAnimation> animation;
	// Tires turned per instance: their bones, and a pose to turn them in (reused instance after instance: parts draw
	// at once).
	std::vector<std::string> tireNames;
	std::vector<std::string> steeredNames;
	std::string cabName;
	std::string trailerName;
	std::vector<std::size_t> tireBones;
	std::vector<std::uint8_t> tireSteers;  // per tire bone: it also steers
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
	// The bones muzzle flashes hang on (the original shows the sub-object on each), and the barrels' recoil bones, in
	// barrel order.
	std::vector<std::size_t> muzzleBones;
	std::string recoilName;
	std::vector<std::size_t> recoilBones;
	std::optional<Graphics::ModelAssetPose> controlled;
	// Its collision shape (one mesh per source mesh) and each collision mesh's bone, first part (its show rules) and
	// bounding sphere in its own space (centre and radius: the vertices' box centre, the farthest vertex).
	Graphics::ModelCollisionShape collision;
	std::vector<std::uint32_t> collisionBones;
	std::vector<std::uint32_t> collisionFirstPart;
	std::vector<std::array<float, 4>> collisionSpheres;
	// Its skeleton at rest (a model with bones that neither animates nor turns any: its parts sit there).
	std::optional<Graphics::ModelAssetPose> rest;
};

// A look's request: what to hide, show and turn, taken from its model description.
inline ModelEntry DescribeModel(const ObjectModel &look)
{
	ModelEntry model;
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
	if (!look.animation.empty())
		model.request = {look.animation, look.animationMode, look.repeat};
	return model;
}

namespace model_entry_detail
{
inline char Upper(char c) noexcept { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c; }

inline bool SameName(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Upper(x) == Upper(y); });
}

inline bool StartsWith(std::string_view name, std::string_view prefix) noexcept
{
	return name.size() >= prefix.size() && SameName(name.substr(0, prefix.size()), prefix);
}

// The clip `name` ("<skeleton>.<clip>") among `clips`.
inline const Assets::ModelAnimationDesc *FindClip(const std::vector<Assets::ModelAnimationDesc> &clips, std::string_view name)
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
}

// The model's parts as its drawing binds them (PropAssetBinding::Load): one per submesh, in order, each on the bone its
// HLOD attachment names when the model has a skeleton (none otherwise). False when a part has no attachment.
inline bool BindPartList(ModelEntry &model, const Assets::ModelAsset &asset, std::string &error)
{
	model.partNames.clear();
	model.partBones.clear();
	const bool skeleton = !asset.Rig().skeleton_name.empty() && !asset.Rig().bones.empty();
	for (const Assets::ModelSubmesh &submesh : asset.Submeshes())
	{
		std::uint32_t bone = std::numeric_limits<std::uint32_t>::max();
		if (skeleton)
		{
			const auto attached = Graphics::Model_Part_Bone(asset.Rig(), submesh.name);
			if (!attached)
			{
				error = "model part has no hierarchy attachment";
				return false;
			}
			bone = attached->first;
		}
		model.partNames.push_back(submesh.name);
		model.partBones.push_back(bone);
	}
	if (!Graphics::Build_Model_Collision_Shape(asset, model.collision, model.collisionBones, error))
		return false;
	// Each collision mesh is one source mesh: its parts are the run of parts of its name.
	model.collisionFirstPart.clear();
	for (std::uint32_t part = 0; part < model.partNames.size(); ++part)
		if (part == 0 || model.partNames[part] != model.partNames[part - 1])
			model.collisionFirstPart.push_back(part);
	model.collisionSpheres.clear();
	for (const Graphics::ModelCollisionMesh &mesh : model.collision.meshes)
	{
		std::array<float, 3> low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
		std::array<float, 3> high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
		for (std::uint32_t vertex = mesh.firstVertex; vertex < mesh.firstVertex + mesh.vertexCount; ++vertex)
		{
			const std::array<float, 3> at{model.collision.x[vertex], model.collision.y[vertex], model.collision.z[vertex]};
			for (std::size_t axis = 0; axis < 3; ++axis)
			{
				low[axis] = std::min(low[axis], at[axis]);
				high[axis] = std::max(high[axis], at[axis]);
			}
		}
		std::array<float, 4> sphere{};
		if (mesh.vertexCount != 0)
		{
			for (std::size_t axis = 0; axis < 3; ++axis)
				sphere[axis] = (low[axis] + high[axis]) * 0.5f;
			for (std::uint32_t vertex = mesh.firstVertex; vertex < mesh.firstVertex + mesh.vertexCount; ++vertex)
			{
				const float dx = model.collision.x[vertex] - sphere[0], dy = model.collision.y[vertex] - sphere[1], dz = model.collision.z[vertex] - sphere[2];
				sphere[3] = std::max(sphere[3], std::sqrt(dx * dx + dy * dy + dz * dz));
			}
		}
		model.collisionSpheres.push_back(sphere);
	}
	if (!asset.Rig().skeleton_name.empty() && !asset.Rig().bones.empty())
	{
		Assets::ModelRigDesc rig;
		rig.skeleton_name = asset.Rig().skeleton_name;
		rig.bones = asset.Rig().bones;
		Graphics::ModelAssetPose pose;
		if (pose.Initialize(rig, error))
			model.rest.emplace(std::move(pose));
		error.clear();
	}
	return true;
}

// Binds the clip to the model's resolved skeleton for pose sampling.
inline bool PrepareAnimation(const Assets::ModelAsset &asset, const Assets::ModelRigDesc *source, const AnimationRequest &request, ModelAnimation &result,
	std::string &error)
{
	const Assets::ModelRigDesc &modelRig = asset.Rig();
	if (modelRig.bones.empty())
	{
		error = "model has no skeleton to animate";
		return false;
	}
	const Assets::ModelAnimationDesc *clip = model_entry_detail::FindClip(modelRig.animations, request.name);
	if (clip == nullptr && source != nullptr)
		clip = model_entry_detail::FindClip(source->animations, request.name);
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
	// Keep the channels this skeleton has; the pose samples translation, quaternion rotation and visibility channels.
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
inline float ClipFrame(const ModelAnimation &animation, double seconds, bool &loop)
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

// The clip frame an animation started `start` of the way in (RANDOMSTART, START_FRAME_LAST, a frame kept across
// states) shows `seconds` on: as if it had run that far already. A MANUAL clip never runs (W3D's ANIM_MODE_MANUAL holds
// the frame adjustAnimation set): it stays on its start frame (START_FRAME_LAST: its last, the uplink's raised dish).
inline float AnimationFrame(const ModelAnimation &animation, double seconds, float start, bool &loop)
{
	if (animation.mode == ObjectAnimationMode::Manual)
	{
		loop = false;
		return animation.frameCount > 1 ? std::clamp(start, 0.0f, 1.0f) * static_cast<float>(animation.frameCount - 1) : 0.0f;
	}
	if (start > 0.0f && animation.frameRate > 0.0f && animation.frameCount > 1)
		seconds += static_cast<double>(start) * static_cast<double>(animation.frameCount - 1) / static_cast<double>(animation.frameRate);
	return ClipFrame(animation, seconds, loop);
}

// W3DModelDraw::doHideShowProjectileObjects: a slot's projectiles are its launch bone's NAME01, NAME02... (or its
// one WeaponHideShowBone sub-object); hiding one hides every sub-object hanging below its bone
// (doHideShowBoneSubObjs).
inline void BindProjectiles(ModelEntry &model, const Assets::ModelAsset &asset)
{
	if (model.projectileSlots == 0)
		return;
	const auto &rig = asset.Rig();
	const auto below = [&](std::uint32_t bone, std::uint32_t ancestor) {
		for (std::uint32_t current = bone; current < rig.bones.size() && rig.bones[current].parent != Assets::ModelRootParent;)
		{
			current = rig.bones[current].parent;
			if (current == ancestor)
				return true;
		}
		return false;
	};
	const std::size_t parts = model.partNames.size();
	for (std::uint8_t slot = 0; slot < 3; ++slot)
	{
		if ((model.projectileSlots & (1u << slot)) == 0)
			continue;
		for (std::size_t part = 0; part < parts; ++part)
		{
			const std::string_view name = model.partNames[part];
			std::uint8_t which = 0;
			if (!model.projectileHideShow[slot].empty())
				which = model_entry_detail::SameName(name, model.projectileHideShow[slot]) ? 15 : 0;
			else if (!model.projectileBones[slot].empty())
				for (std::uint8_t index = 1; index < 15 && which == 0; ++index)
				{
					char suffix[4];
					std::snprintf(suffix, sizeof(suffix), "%02d", index);
					which = model_entry_detail::SameName(name, model.projectileBones[slot] + suffix) ? index : 0;
				}
			if (which == 0)
				continue;
			const std::uint8_t code = static_cast<std::uint8_t>(((slot + 1u) << 4) | which);
			model.partProjectile[part] = code;
			const std::uint32_t bone = model.partBones[part];
			for (std::size_t child = 0; child < parts; ++child)
				if (child != part && below(model.partBones[child], bone))
					model.partProjectile[child] = code;
		}
	}
}

inline void BindParts(ModelEntry &model, const Assets::ModelAsset &asset)
{
	using namespace model_entry_detail;
	const std::size_t parts = model.partNames.size();
	model.partHidden.assign(parts, 0);
	model.partProjectile.assign(parts, 0);
	model.partMuzzle.assign(parts, 0);
	model.partTread.assign(parts, 0);
	model.partHeadlight.assign(parts, 0);
	for (std::size_t part = 0; part < parts; ++part)
	{
		const std::string_view name = model.partNames[part];
		// As the original's hideAllHeadlights: any sub-object whose name has HEADLIGHT.
		model.partHeadlight[part] = name.find("HEADLIGHT") != std::string_view::npos ? 1 : 0;
		if (StartsWith(name, "TREADS"))
			model.partTread[part] = name.size() > 6 && Upper(name[6]) == 'R' ? 2 : 1;
		for (const auto &muzzle : model.muzzleNames)
			model.partMuzzle[part] |= StartsWith(name, muzzle) ? 1 : 0;
		// The barrel a flash belongs to (1-based; 255: a flash of no one barrel, shown with any).
		if (model.partMuzzle[part] != 0)
			model.partMuzzle[part] = 255;
		const std::uint32_t bone = model.partBones[part];
		if (const auto found = std::find(model.muzzleBones.begin(), model.muzzleBones.end(), bone); found != model.muzzleBones.end())
			model.partMuzzle[part] = static_cast<std::uint8_t>(std::min<std::ptrdiff_t>(found - model.muzzleBones.begin() + 1, 254));
		for (const auto &hidden : model.hideNames)
			model.partHidden[part] |= SameName(name, hidden) ? 1 : 0;
		// A shown flash still flashes: handleClientRecoil (after doHideShowSubObjs, every draw) hides each barrel's flash
		// but while its recoil starts.
		for (const auto &shown : model.showNames)
			if (SameName(name, shown))
				model.partHidden[part] = 0;
	}
	BindProjectiles(model, asset);
}

// Finds the bones the model turns per instance (tires, cab, trailer, turrets, barrels) and those its flashes hang
// on, and readies a pose to turn them in.
inline void BindBones(ModelEntry &model, const Assets::ModelAsset &asset)
{
	if (model.tireNames.empty() && model.cabName.empty() && model.trailerName.empty() && model.turretName.empty() && model.turretPitchName.empty() &&
		model.muzzleNames.empty() && model.recoilName.empty() && model.altTurretName.empty() && model.altTurretPitchName.empty())
		return;
	if (asset.Rig().bones.empty())
		return;
	Assets::ModelRigDesc rig;
	rig.skeleton_name = asset.Rig().skeleton_name;
	rig.bones = asset.Rig().bones;
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
	const auto find = [&](const std::string &name, std::size_t &bone) {
		if (!name.empty())
			if (const std::size_t index = pose.Bone_Index(name); index < pose.Bone_Count())
				bone = index;
	};
	find(model.cabName, model.cabBone);
	find(model.trailerName, model.trailerBone);
	find(model.turretName, model.turretBone);
	find(model.turretPitchName, model.turretPitchBone);
	find(model.altTurretName, model.altTurretBone);
	find(model.altTurretPitchName, model.altTurretPitchBone);
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
	if (!model.tireBones.empty() || model.cabBone < pose.Bone_Count() || model.trailerBone < pose.Bone_Count() || model.turretBone < pose.Bone_Count() ||
		model.turretPitchBone < pose.Bone_Count() || !model.recoilBones.empty() || model.altTurretBone < pose.Bone_Count() ||
		model.altTurretPitchBone < pose.Bone_Count())
		model.controlled.emplace(std::move(pose));
}

// A model whose animation cannot play still draws, at rest; `error` says why it cannot.
inline bool BindAnimation(ModelEntry &model, const Assets::ModelAsset &asset, const Assets::ModelRigDesc *source, std::string &error)
{
	if (model.request.name.empty())
		return true;
	ModelAnimation animation;
	if (!PrepareAnimation(asset, source, model.request, animation, error))
		return false;
	model.animation = std::move(animation);
	return true;
}

// Binds a loaded model: its parts (bones before parts: flashes hang on bones), its turned bones and its animation.
// False (the model fails) when its parts cannot bind; an animation that cannot play leaves `animationError` set.
inline bool BindModel(ModelEntry &model, const Assets::ModelAsset &asset, const Assets::ModelRigDesc *animationSource, std::string &error,
	std::string &animationError)
{
	if (!BindPartList(model, asset, error))
		return false;
	BindBones(model, asset);
	BindParts(model, asset);
	BindAnimation(model, asset, animationSource, animationError);
	return true;
}

// Whether a part is a projectile fired from its slot (hidden: the first `hidden` of them; 255: the slot's one).
inline bool ProjectileHidden(std::uint8_t code, const ObjectInstance &instance) noexcept
{
	if (code == 0)
		return false;
	const std::uint8_t hidden = instance.projectilesHidden[((code >> 4) - 1u) % 3u];
	const std::uint8_t which = code & 15u;
	return which == 15 ? hidden != 0 : which <= hidden;
}

// Whether a part of the instance's model draws: not hidden, a muzzle flash only while its barrel flashes, a headlight
// only at night, a loaded projectile until fired.
inline bool PartShown(const ModelEntry &model, std::size_t part, const ObjectInstance &instance) noexcept
{
	const std::uint8_t barrel = model.partMuzzle[part];
	const bool flashes = barrel == 255 || instance.flashBarrels == 0 ? instance.muzzleFlash : (instance.flashBarrels >> ((barrel - 1u) % 8u) & 1u) != 0;
	return !(model.partHidden[part] || (barrel != 0 && !flashes) || (model.partHeadlight[part] && !instance.night) ||
		ProjectileHidden(model.partProjectile[part], instance));
}

// Whether a part casts the instance's shadow (W3DDirectionalShadows' Collect_Object): not hidden, not a muzzle flash,
// not an unlit headlight, not a fired projectile.
inline bool PartCastsShadow(const ModelEntry &model, std::size_t part, const ObjectInstance &instance) noexcept
{
	return !(model.partHidden[part] || model.partMuzzle[part] != 0 || (model.partHeadlight[part] && !instance.night) ||
		ProjectileHidden(model.partProjectile[part], instance));
}

// The model's pose `seconds` into its animation (started `start` of the way in), or null for its rest pose; `failed`
// is set when sampling fails (the animation stops for good).
inline const Graphics::ModelAssetPose *PoseOf(ModelEntry &model, double seconds, float start, bool &failed)
{
	failed = false;
	if (!model.animation || !model.animation->valid)
		return nullptr;
	ModelAnimation &animation = *model.animation;
	bool loop = false;
	const float clipFrame = AnimationFrame(animation, seconds, start, loop);
	if (animation.evaluatedFrame != clipFrame)
	{
		animation.evaluatedFrame = clipFrame;
		if (!animation.pose.Evaluate(0, clipFrame, loop))
		{
			animation.valid = false;
			failed = true;
		}
	}
	return animation.valid ? &animation.pose : nullptr;
}

// Its model's pose at the instance's time, with its controlled bones (tires, cab, trailer, turrets, barrels) turned.
inline const Graphics::ModelAssetPose *InstancePose(ModelEntry &model, const ObjectInstance &instance, bool &failed)
{
	const Graphics::ModelAssetPose *pose = PoseOf(model, instance.animationSeconds, instance.animationStart, failed);
	// Tires turn about their axles (y) on top of the model's pose.
	bool recoiling = false;
	for (const float shift : instance.recoil)
		recoiling = recoiling || shift != 0.0f;
	const bool turretTurns = model.turretBone != ~std::size_t{0} || model.turretPitchBone != ~std::size_t{0} || (recoiling && !model.recoilBones.empty()) ||
		model.altTurretBone != ~std::size_t{0} || model.altTurretPitchBone != ~std::size_t{0};
	const bool suspended = instance.suspension[0] != 0.0f || instance.suspension[1] != 0.0f || instance.suspension[2] != 0.0f || instance.suspension[3] != 0.0f;
	if (!model.controlled ||
		!(instance.wheels != 0.0f || instance.rearWheels != 0.0f || suspended || instance.steer != 0.0f || instance.cab != 0.0f || instance.trailer != 0.0f ||
			turretTurns))
		return pose;
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
	for (std::size_t tire = 0; tire < model.tireBones.size(); ++tire)
		if (count < controls.size())
		{
			// W3DTruckDraw: raised by its corner's suspension offset (in its own frame), then steered and rolled.
			Graphics::RenderTransform control = model.tireSteers[tire] != 0 ? steeredRoll : rearRoll;
			control.matrix[11] += instance.suspension[std::min<std::size_t>(model.tireCorners[tire], 3)];
			controls[count++] = {model.tireBones[tire], control};
		}
	if (model.cabBone != ~std::size_t{0} && count < controls.size())
		controls[count++] = {model.cabBone, aboutZ(instance.cab)};
	if (model.trailerBone != ~std::size_t{0} && count < controls.size())
		controls[count++] = {model.trailerBone, aboutZ(instance.trailer)};
	// As the original: the turret bone turned by its aim about z, the pitch bone about -y.
	if (model.turretBone != ~std::size_t{0} && count < controls.size())
		controls[count++] = {model.turretBone, aboutZ(instance.turret + model.turretArtAngle)};
	if (model.turretPitchBone != ~std::size_t{0} && count < controls.size())
	{
		const float p = -(instance.turretPitch + model.turretArtPitch);
		const float cp = std::cos(p), sp = std::sin(p);
		controls[count++] = {model.turretPitchBone, Graphics::RenderTransform{{cp, 0, sp, 0, 0, 1, 0, 0, -sp, 0, cp, 0, 0, 0, 0, 1}}};
	}
	// The second turret the same way (AltTurret / AltTurretPitch).
	if (model.altTurretBone != ~std::size_t{0} && count < controls.size())
		controls[count++] = {model.altTurretBone, aboutZ(instance.altTurret + model.altTurretArtAngle)};
	if (model.altTurretPitchBone != ~std::size_t{0} && count < controls.size())
	{
		const float p = -(instance.altTurretPitch + model.altTurretArtPitch);
		const float cp = std::cos(p), sp = std::sin(p);
		controls[count++] = {model.altTurretPitchBone, Graphics::RenderTransform{{cp, 0, sp, 0, 0, 1, 0, 0, -sp, 0, cp, 0, 0, 0, 0, 1}}};
	}
	// Each barrel back along its x by its recoil (handleClientRecoil's gun transform).
	for (std::size_t barrel = 0; barrel < model.recoilBones.size() && barrel < instance.recoil.size(); ++barrel)
		if (instance.recoil[barrel] != 0.0f && count < controls.size())
			controls[count++] = {model.recoilBones[barrel], Graphics::RenderTransform{{1, 0, 0, -instance.recoil[barrel], 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
	if (model.controlled->Evaluate_Controlled(pose, std::span(controls.data(), count)))
		pose = &*model.controlled;
	return pose;
}

// A bone of a drawn instance in the world (W3D's Get_Bone_Transform on the render object as drawn: its animation, turret,
// pitch and recoil as InstancePose turns them, else at rest): where it is and its turn about z. None when the model has
// no such bone.
struct InstanceBone
{
	std::array<float, 3> at{};
	float yaw{0.0f};
};

inline std::optional<InstanceBone> InstanceBoneOf(ModelEntry &model, const ObjectInstance &instance, std::string_view bone)
{
	bool failed = false;
	const Graphics::ModelAssetPose *pose = InstancePose(model, instance, failed);
	if (pose == nullptr && model.rest)
		pose = &*model.rest;
	if (pose == nullptr && model.controlled && model.controlled->Evaluate_Controlled(nullptr, {}))
		pose = &*model.controlled;
	if (pose == nullptr)
		return std::nullopt;
	const std::size_t index = pose->Bone_Index(bone);
	Graphics::RenderTransform transform;
	if (index >= pose->Bone_Count() || !pose->Bone_Transform(index, transform))
		return std::nullopt;
	const auto &w = instance.world;
	const auto &b = transform.matrix;
	InstanceBone place;
	for (unsigned r = 0; r < 3; ++r)
		place.at[r] = w[r * 4 + 0] * b[3] + w[r * 4 + 1] * b[7] + w[r * 4 + 2] * b[11] + w[r * 4 + 3];
	// Its x axis in the world.
	const float x = w[0] * b[0] + w[1] * b[4] + w[2] * b[8];
	const float y = w[4] * b[0] + w[5] * b[4] + w[6] * b[8];
	place.yaw = std::atan2(y, x);
	return place;
}

// W3DView::pickDrawable for one drawn instance (RTS3DScene::castRay's step for its render object): its model's
// collision meshes in the instance's pose as drawn (InstancePose; at rest without one), each shown by its first part's
// rules (a hidden sub-object, a flash not firing, an unlit headlight, a fired projectile: not there) and by its
// animation; the ray's line must pass within its bounding sphere (the meshes' spheres as posed, together). Whether the
// ray met it nearer than before (the caster's end is pulled in to the hit).
inline bool CastInstanceRay(ModelEntry &model, const ObjectInstance &instance, Graphics::ModelRayCaster &ray, bool &failed)
{
	failed = false;
	const std::size_t meshes = model.collision.meshes.size();
	if (meshes == 0)
		return false;
	const Graphics::ModelAssetPose *pose = InstancePose(model, instance, failed);
	if (pose == nullptr && model.rest)
		pose = &*model.rest;
	const auto &m = instance.world;
	const auto compose = [&](const std::array<float, 16> *bone) {
		std::array<float, 12> out{};
		for (unsigned r = 0; r < 3; ++r)
			for (unsigned c = 0; c < 4; ++c)
			{
				float value = 0.0f;
				for (unsigned k = 0; k < 4; ++k)
				{
					const float right = bone != nullptr ? (*bone)[k * 4 + c] : (k == c ? 1.0f : 0.0f);
					value += m[r * 4 + k] * right;
				}
				out[r * 4 + c] = value;
			}
		return Engine::Math::AffineTransform3{out};
	};
	std::vector<Engine::Math::AffineTransform3> worlds(meshes);
	std::vector<std::uint8_t> shown(meshes, 1);
	Engine::Math::Vector3 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
	Engine::Math::Vector3 high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
	std::vector<std::array<float, 4>> spheres(meshes);
	for (std::size_t mesh = 0; mesh < meshes; ++mesh)
	{
		const std::uint32_t bone = mesh < model.collisionBones.size() ? model.collisionBones[mesh] : 0u;
		Graphics::RenderTransform transform;
		const bool posed = pose != nullptr && pose->Bone_Transform(bone, transform);
		worlds[mesh] = compose(posed ? &transform.matrix : nullptr);
		const std::uint32_t part = mesh < model.collisionFirstPart.size() ? model.collisionFirstPart[mesh] : 0u;
		shown[mesh] = static_cast<std::uint8_t>((part < model.partNames.size() && PartShown(model, part, instance)) && (pose == nullptr || pose->Visible(bone)) ? 1 : 0);
		const auto &local = model.collisionSpheres[mesh];
		const Engine::Math::Vector3 centre = worlds[mesh].Transform_Point({local[0], local[1], local[2]});
		// The mesh's scale: its world's longest axis.
		float scale = 0.0f;
		for (unsigned c = 0; c < 3; ++c)
		{
			const float x = worlds[mesh].elements[c], y = worlds[mesh].elements[4 + c], z = worlds[mesh].elements[8 + c];
			scale = std::max(scale, std::sqrt(x * x + y * y + z * z));
		}
		spheres[mesh] = {centre.x, centre.y, centre.z, local[3] * scale};
		low = {std::min(low.x, centre.x), std::min(low.y, centre.y), std::min(low.z, centre.z)};
		high = {std::max(high.x, centre.x), std::max(high.y, centre.y), std::max(high.z, centre.z)};
	}
	const Engine::Math::Vector3 centre{(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f, (low.z + high.z) * 0.5f};
	float radius = 0.0f;
	for (const auto &sphere : spheres)
	{
		const float dx = sphere[0] - centre.x, dy = sphere[1] - centre.y, dz = sphere[2] - centre.z;
		radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz) + sphere[3]);
	}
	if (!Graphics::RayNearSphere(ray, centre, radius))
		return false;
	return Graphics::CastModelRay(model.collision, worlds, shown, ray);
}
}
