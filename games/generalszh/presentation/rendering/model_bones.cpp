module;
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module games.generalszh.presentation.rendering.model_bones;

import Assets.Runtime;
import Assets.Cache;
import Assets.Handles;
import Assets.Models;
import Assets.States;
import Assets.ModelRig;

namespace generalszh::presentation
{
namespace
{
std::string Upper(std::string_view text)
{
	std::string upper(text);
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return upper;
}

struct RestPose
{
	std::array<float, 3> position{};
	std::array<float, 4> rotation{0, 0, 0, 1}; // x y z w
};

std::array<float, 3> Rotate(const std::array<float, 4> &q, const std::array<float, 3> &v)
{
	// v + 2 w (q x v) + 2 q x (q x v)
	const std::array<float, 3> u{q[0], q[1], q[2]};
	const auto cross = [](const std::array<float, 3> &a, const std::array<float, 3> &b) {
		return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	};
	const auto t = cross(u, v);
	const std::array<float, 3> t2{2 * t[0], 2 * t[1], 2 * t[2]};
	const auto c = cross(u, t2);
	return {v[0] + q[3] * t2[0] + c[0], v[1] + q[3] * t2[1] + c[1], v[2] + q[3] * t2[2] + c[2]};
}

std::array<float, 4> Multiply(const std::array<float, 4> &a, const std::array<float, 4> &b)
{
	return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

struct ModelEntry
{
	Assets::ModelAssetHandle handle;
	bool resolved{false};
	std::map<std::string, BonePose> bones; // upper-case name -> rest pose
	std::map<std::string, std::string> parents; // upper-case name -> its nearest named ancestor's
};

using ModelTable = std::map<std::string, ModelEntry, std::less<>>;

// The model's bones once it has loaded (requested on first ask).
const ModelEntry *Resolve(ModelTable &models, std::string_view model)
{
	if (model.empty())
		return nullptr;
	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	if (cache == nullptr)
		return nullptr;
	auto found = models.find(model);
	if (found == models.end())
		// The model's file, as the model library asks for it (W3DAssetManager: <name>.w3d).
		found = models.emplace(std::string(model), ModelEntry{cache->Request_Model(std::string(model) + ".w3d"), false, {}}).first;
	ModelEntry &entry = found->second;
	if (!entry.resolved)
	{
		// Still loading: not known yet. Failed to load: known, without bones (what rides a bone it lacks sits at the
		// object's origin, as the original's).
		const auto state = cache->Get_State(entry.handle);
		if (state == Assets::AssetState::Loading || state == Assets::AssetState::Unloaded)
			return nullptr;
		entry.resolved = true;
		if (const Assets::ModelAsset *asset = cache->Try_Get_Model(entry.handle))
		{
			const auto &bones = asset->Rig().bones;
			std::vector<RestPose> poses(bones.size());
			std::vector<std::string> named(bones.size()); // each bone's own name, else its nearest named ancestor's
			for (std::size_t index = 0; index < bones.size(); ++index)
			{
				const Assets::ModelBoneDesc &desc = bones[index];
				const std::array<float, 3> local{desc.translation.x, desc.translation.y, desc.translation.z};
				if (desc.parent == Assets::ModelRootParent || desc.parent >= index)
					poses[index] = {local, desc.rotation};
				else
				{
					const RestPose &parent = poses[desc.parent];
					const auto moved = Rotate(parent.rotation, local);
					poses[index] = {{parent.position[0] + moved[0], parent.position[1] + moved[1], parent.position[2] + moved[2]},
						Multiply(parent.rotation, desc.rotation)};
				}
				const std::string parentName = desc.parent == Assets::ModelRootParent || desc.parent >= index ? std::string{} : named[desc.parent];
				named[index] = desc.name.empty() ? parentName : Upper(desc.name);
				if (!desc.name.empty() && !parentName.empty())
					entry.parents.emplace(Upper(desc.name), parentName);
				if (!desc.name.empty())
				{
					const auto &q = poses[index].rotation;
					const float yaw = std::atan2(2.0f * (q[3] * q[2] + q[0] * q[1]), 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]));
					entry.bones.emplace(Upper(desc.name), BonePose{poses[index].position, yaw});
				}
			}
		}
	}
	return &entry;
}
}

struct ModelBones::State
{
	ModelTable models;
};

ModelBones::ModelBones() : m_state(std::make_unique<State>()) {}
ModelBones::~ModelBones() = default;

std::vector<std::array<float, 3>> ModelBones::Find(std::string_view model, std::string_view bone, bool family)
{
	const ModelEntry *entry = Resolve(m_state->models, model);
	if (entry == nullptr)
		return {};
	std::vector<std::array<float, 3>> positions;
	const std::string name = Upper(bone);
	if (!family)
	{
		if (const auto at = entry->bones.find(name); at != entry->bones.end())
			positions.push_back(at->second.position);
		return positions;
	}
	// The original numbers a family from 01 and stops at the first gap.
	for (int index = 1; index < 100; ++index)
	{
		char suffix[4];
		std::snprintf(suffix, sizeof suffix, "%02d", index);
		const auto at = entry->bones.find(name + suffix);
		if (at == entry->bones.end())
			break;
		positions.push_back(at->second.position);
	}
	return positions;
}

bool ModelBones::Ready(std::string_view model) { return Resolve(m_state->models, model) != nullptr; }

bool ModelBones::Descends(std::string_view model, std::string_view bone, std::string_view ancestor)
{
	const ModelEntry *entry = Resolve(m_state->models, model);
	if (entry == nullptr || ancestor.empty())
		return false;
	const std::string wanted = Upper(ancestor);
	std::string at = Upper(bone);
	for (int depth = 0; depth < 256; ++depth)
	{
		const auto parent = entry->parents.find(at);
		if (parent == entry->parents.end())
			return false;
		if (parent->second == wanted)
			return true;
		at = parent->second;
	}
	return false;
}

std::optional<BonePose> ModelBones::Pose(std::string_view model, std::string_view bone)
{
	const ModelEntry *entry = Resolve(m_state->models, model);
	if (entry == nullptr)
		return std::nullopt;
	const auto at = entry->bones.find(Upper(bone));
	return at != entry->bones.end() ? std::optional(at->second) : std::nullopt;
}
}
