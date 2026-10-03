export module Assets.Models.Composition;
import std;
import Assets.Models;
import Assets.Identity;

namespace Assets::CompositionDetail
{
// Prepare an immutable composite skeleton and geometry. Each child retains
// its own bind hierarchy beneath the named parent bone; skin indices never
// depend on actor objects or the renderer's allocation order.
bool Append(ModelAssetDesc &base, const ModelAssetDesc &child,
	std::uint32_t parent, bool match_levels, std::string_view label, std::string &error)
{
	if (parent >= base.rig.bones.size()) {error = "attachment parent bone is missing"; return false;}
	if (!Validate_Model_Rig(base.rig, error) || !Validate_Model_Rig(child.rig, error)) return false;
	const auto bone_offset = static_cast<std::uint32_t>(base.rig.bones.size());
	if (base.rig.bones.size() + std::max(std::size_t{1}, child.rig.bones.size()) > 65536) {error = "composite skeleton exceeds skin index width"; return false;}
	const auto prefix = std::string(label) + "/";
	if(!child.rig.bones.empty()) base.rig.attached_rigs.push_back({std::string(label),child.rig.skeleton_name,bone_offset,std::uint32_t(child.rig.bones.size())});
	for(auto nested:child.rig.attached_rigs) {nested.name=prefix+nested.name;nested.first_bone+=bone_offset;base.rig.attached_rigs.push_back(std::move(nested));}
	if (child.rig.bones.empty()) base.rig.bones.push_back({prefix + "root", parent});
	else for (const auto &source : child.rig.bones) {
		auto bone = source; bone.name = prefix + source.name;
		bone.parent = source.parent == ModelRootParent ? parent : bone_offset + source.parent;
		base.rig.bones.push_back(std::move(bone));
	}
	std::set<std::uint32_t> levels;
	for (const auto &attachment : base.rig.attachments) levels.insert(attachment.lod);
	if (levels.empty()) levels.insert(0);
	std::uint32_t child_detail{};
	for (const auto &attachment : child.rig.attachments) child_detail = std::max(child_detail, attachment.lod);
	const auto vertices = static_cast<std::uint32_t>(base.vertices.size());
	const auto indices = static_cast<std::uint32_t>(base.indices.size());
	const auto materials = static_cast<std::uint32_t>(base.materials.size());
	for (const auto &source : child.vertices) {
		auto vertex = source;
		for (unsigned i = 0; i < 4; ++i) if (vertex.bone_weights[i] != 0)
			vertex.bone_indices[i] = static_cast<std::uint16_t>(bone_offset + vertex.bone_indices[i]);
		base.vertices.push_back(vertex);
	}
	for (const auto index : child.indices) base.indices.push_back(vertices + index);
	base.materials.insert(base.materials.end(), child.materials.begin(), child.materials.end());
	base.dependencies.insert(base.dependencies.end(), child.dependencies.begin(), child.dependencies.end());
    for(auto emitter:child.emitters) {emitter.bone+=bone_offset;if(match_levels) for(const auto level:levels) {emitter.lod=level;base.emitters.push_back(emitter);}else base.emitters.push_back(std::move(emitter));}
    if(child.emitter) for(const auto level:levels) base.emitters.push_back({*child.emitter,bone_offset,level});
	const auto collision_offset=static_cast<std::uint32_t>(base.collision.triangles.size());
	for(auto triangle:child.collision.triangles) {
		for(auto& index:triangle) index+=vertices;
		base.collision.triangles.push_back(triangle);
	}
	base.collision.surfaces.insert(base.collision.surfaces.end(),child.collision.surfaces.begin(),child.collision.surfaces.end());
	for(const auto& source:child.collision.parts) {
		std::uint32_t bone{},lod{};
		const auto attached=std::ranges::find_if(child.rig.attachments,[&](const auto& attachment) {
			const auto dot=attachment.object_name.find('.');
			return Canonicalize_Asset_Name(attachment.object_name)==Canonicalize_Asset_Name(source.name) ||
				Canonicalize_Asset_Name(attachment.object_name.substr(dot==std::string::npos ? 0 : dot+1))==Canonicalize_Asset_Name(source.name);
		});
		if(attached!=child.rig.attachments.end()) {bone=attached->bone;lod=attached->lod;}
		if(source.attached) {bone=source.bone;lod=source.lod;}
		if(!match_levels && lod!=child_detail) continue;
		const auto append=[&](std::uint32_t level) {
			auto part=source;part.name=prefix+source.name+"/"+std::to_string(level);part.first_triangle+=collision_offset;
			part.bone=bone_offset+bone;part.lod=level;part.attached=true;
			base.rig.attachments.push_back({part.name,bone_offset+bone,level});base.collision.parts.push_back(std::move(part));
		};
		if(match_levels) {if(levels.contains(lod)) append(lod);}
		else for(const auto level:levels) append(level);
	}
	for (const auto &source : child.submeshes) {
		std::uint32_t bone{}, lod{};
		const auto attached = std::ranges::find_if(child.rig.attachments, [&](const auto &attachment) {
			const auto dot = attachment.object_name.find('.');
			return Canonicalize_Asset_Name(attachment.object_name) == Canonicalize_Asset_Name(source.name) ||
				Canonicalize_Asset_Name(attachment.object_name.substr(dot == std::string::npos ? 0 : dot + 1)) == Canonicalize_Asset_Name(source.name);
		});
		if (attached != child.rig.attachments.end()) {bone = attached->bone; lod = attached->lod;}
		if (!match_levels && lod != child_detail) continue;
		const auto append = [&](std::uint32_t level) {
			auto part = source; part.name = prefix + source.name + "/" + std::to_string(level);
			part.first_index += indices; part.material_index += materials;
			base.rig.attachments.push_back({part.name, bone_offset + bone, level}); base.submeshes.push_back(std::move(part));
		};
		if (match_levels) {if (levels.contains(lod)) append(lod);}
		else for (const auto level : levels) append(level);
	}
	for(const auto& source:child.collision.boxes) {
		std::uint32_t bone{},lod{};
		const auto attached=std::ranges::find_if(child.rig.attachments,[&](const auto& attachment) {
			const auto dot=attachment.object_name.find('.');
			return Canonicalize_Asset_Name(attachment.object_name)==Canonicalize_Asset_Name(source.name) ||
				Canonicalize_Asset_Name(attachment.object_name.substr(dot==std::string::npos ? 0 : dot+1))==Canonicalize_Asset_Name(source.name);
		});
		if(attached!=child.rig.attachments.end()) {bone=attached->bone;lod=attached->lod;}
		if(source.attached) {bone=source.bone;lod=source.lod;}
		if(!match_levels && lod!=child_detail) continue;
		const auto append=[&](std::uint32_t level) {
			auto box=source;box.name=prefix+source.name+"/"+std::to_string(level);
			box.bone=bone_offset+bone;box.lod=level;box.attached=true;
			base.rig.attachments.push_back({box.name,bone_offset+bone,level});base.collision.boxes.push_back(std::move(box));
		};
		if(match_levels) {if(levels.contains(lod)) append(lod);}
		else for(const auto level:levels) append(level);
	}
	if (child.skin_bone_count) base.skin_bone_count = std::max(base.skin_bone_count, bone_offset + child.skin_bone_count);
	for (const auto &source : child.rig.animations) {
		auto clip = source; clip.name = prefix + clip.name; clip.skeleton_name = base.rig.skeleton_name;
		for (auto &channel : clip.channels) channel.bone += bone_offset;
		base.rig.animations.push_back(std::move(clip));
	}
	// Conservative bind-pose bounds include the attached hierarchy. Rotation
	// preserves each vertex radius; each ancestor contributes its translation.
	const auto length = [](const auto &p) {return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);};
	std::vector<float> offsets(base.rig.bones.size()); float translation{};
	for (std::size_t i = 0; i < base.rig.bones.size(); ++i) {
		const auto &bone = base.rig.bones[i];
		offsets[i] = length(bone.translation) + (bone.parent == ModelRootParent ? 0.f : offsets[bone.parent]);
		if (i >= bone_offset) translation = std::max(translation, offsets[i]);
	}
	float radius{}; for (const auto &vertex : child.vertices) radius = std::max(radius, length(vertex.position)); radius += translation;
	base.bounds.minimum = {std::min(base.bounds.minimum.x, -radius), std::min(base.bounds.minimum.y, -radius), std::min(base.bounds.minimum.z, -radius)};
	base.bounds.maximum = {std::max(base.bounds.maximum.x, radius), std::max(base.bounds.maximum.y, radius), std::max(base.bounds.maximum.z, radius)};
	return Validate_Model_Rig(base.rig, error);
}
}
export namespace Assets
{
inline bool Append_Model_Attachment(ModelAssetDesc &base, const ModelAssetDesc &child,
	std::uint32_t parent, bool match_levels, std::string_view label, std::string &error)
{
	auto next = base;
	if (!CompositionDetail::Append(next, child, parent, match_levels, label, error)) return false;
	base = std::move(next); return true;
}
}
