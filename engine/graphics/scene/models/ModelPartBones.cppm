export module Graphics.Scene.Models.PartBones;
import std;

import Assets.ModelRig;

// Which bone a model's part (one of its source meshes, by name) hangs on: the HLOD's attachment whose object name
// ("<container>.<mesh>") names it, the lowest level of detail's when several do (the first such). A model without a
// skeleton has no bones (0). No attachment for the name when the model has attachments: none.
export namespace Graphics
{
inline std::optional<std::pair<std::uint32_t, std::uint32_t>> Model_Part_Bone(const Assets::ModelRigDesc &rig, std::string_view part)
{
	if (rig.bones.empty() || rig.attachments.empty())
		return std::pair<std::uint32_t, std::uint32_t>{0u, 0u};
	std::optional<std::pair<std::uint32_t, std::uint32_t>> found;
	for (const Assets::ModelAttachmentDesc &attachment : rig.attachments)
	{
		const auto dot = attachment.object_name.find('.');
		const std::string_view name = std::string_view(attachment.object_name).substr(dot == std::string::npos ? 0 : dot + 1);
		if (name != part || (found && attachment.lod >= found->second))
			continue;
		found = std::pair<std::uint32_t, std::uint32_t>{attachment.bone, attachment.lod};
	}
	return found;
}
}
