export module Assets.Adapters.W3D;
import std;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Mesh;
import Assets.Adapters.W3D.Model;
import Assets.Adapters.W3D.Rig;
import Assets.Adapters.W3D.ResolvedModel;
import Assets.Adapters.W3D.Box;
import Assets.Identity;
import Assets.Importers.Models;
import Assets.Models;

namespace Assets
{
namespace W3DAdapterDetail
{

std::string Extension(std::string_view name)
{
	if (const auto selector = name.find("::"); selector != std::string_view::npos) name = name.substr(0, selector);
	const std::size_t separator = name.find_last_of('/');
	const std::size_t dot = name.find_last_of('.');
	if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator))
		return {};

	std::string extension(name.substr(dot));
	for (char &character : extension) {
		if (character >= 'A' && character <= 'Z')
			character = static_cast<char>(character - 'A' + 'a');
	}
	return extension;
}

bool Has_W3D_Root(W3D::W3DByteSpan source) noexcept
{
	if (source.size() < 8)
		return false;

	std::uint32_t id = 0;
	std::uint32_t encoded_size = 0;
	if (!W3D::W3DRead_U32(source, 0, id) || !W3D::W3DRead_U32(source, 4, encoded_size))
		return false;

	return id == W3D::W3DChunkMesh && (encoded_size & W3D::W3DChunkContainsChildren) != 0 &&
		(encoded_size & W3D::W3DChunkSizeMask) <= source.size() - 8;
}

void Add_Dependency(ModelAssetDesc &description, AssetType type, std::string_view name)
{
	if (name.empty())
		return;

	const std::string canonical_name = Canonicalize_Asset_Name(name);
	if (canonical_name.empty())
		return;
	for (const AssetDependencyDesc &dependency : description.dependencies) {
		if (dependency.type == type && Canonicalize_Asset_Name(dependency.name) == canonical_name)
			return;
	}
	description.dependencies.push_back({type, std::string(name)});
}

void Read_Top_Level_Dependency(const W3D::W3DChunkView &chunk, ModelAssetDesc &description)
{
	if (!chunk.contains_children)
		return;

	W3D::W3DVisit_Chunks(chunk.payload, [&description, &chunk](const W3D::W3DChunkView &child) {
		if (chunk.id == W3D::W3DChunkHierarchy && child.id == W3D::W3DChunkHierarchyHeader)
			Add_Dependency(description, AssetType::Skeleton, W3D::W3DRead_Fixed_String(child.payload, 4, 16));
		if (chunk.id == W3D::W3DChunkAnimation && child.id == W3D::W3DChunkAnimationHeader)
			Add_Dependency(description, AssetType::Animation, W3D::W3DRead_Fixed_String(child.payload, 4, 16));
		return true;
	});
}

}

export class W3DAdapter final : public IModelAdapter
{
public:
	bool Can_Import(const AssetIdentity &identity, std::span<const std::byte> source) const noexcept override
	{
		if (W3DAdapterDetail::Extension(identity.canonical_name) == ".w3d")
			return true;
		return W3DAdapterDetail::Has_W3D_Root(source);
	}

	ModelImportResult Import(const AssetIdentity &identity, std::span<const std::byte> source) const override
	{
		if (source.empty())
			return {nullptr, "W3D source is empty"};
		if (!W3D::W3DValidate_Chunk_Tree(source))
			return {nullptr, "W3D source contains a malformed or truncated chunk"};

		auto description = std::make_unique<ModelAssetDesc>();
		description->source_name = identity.canonical_name;
		description->source_format = "W3D";
		// An explicit render-object selector addresses one exported mesh inside
		// a container without importing unrelated scene meshes. Ordinary file
		// requests retain their full model/hierarchy behavior.
		const auto marker = identity.canonical_name.find("::");
		const std::string selector = marker == std::string::npos ? std::string{} : identity.canonical_name.substr(marker + 2);
		if (marker != std::string::npos && selector.empty()) return {nullptr, "empty W3D render-object selector"};
		bool found_mesh = false;
		std::string error;
		if (selector.empty() && !W3D::W3DRead_Model_Rig(source, description->rig, error))
			return {nullptr, std::move(error)};
		if (!W3D::W3DVisit_Chunks(source, [&description, &found_mesh, &error, &selector](const W3D::W3DChunkView &chunk) {
			if (chunk.id == W3D::W3DChunkMesh) {
				if (!selector.empty()) {
					W3D::W3DMeshHeader header; bool decoded = false;
					if (!W3D::W3DVisit_Chunks(chunk.payload, [&](const W3D::W3DChunkView &child) {
						if (child.id == W3D::W3DChunkMeshHeader3) decoded = W3D::W3DRead_Mesh_Header(child.payload, header);
						return true;
					}) || !decoded) { error = "invalid W3D selected mesh header"; return false; }
					const auto full_name = header.container_name.empty() ? header.name : header.container_name + "." + header.name;
					if (Canonicalize_Asset_Name(full_name) != selector) return true;
					if (found_mesh) { error = "ambiguous W3D render-object selector"; return false; }
				}
				W3D::W3DParsedMesh mesh;
				if (!chunk.contains_children || !W3D::W3DParse_Mesh(chunk.payload, mesh, error))
					return false;
				if (!selector.empty() && (!mesh.bone_indices.empty() || !mesh.skin_indices.empty())) {
					error = "selected skinned mesh requires an explicit hierarchy binding"; return false;
				}
				W3D::W3DAppend_Mesh(*description, mesh);
				found_mesh = true;
			} else if(selector.empty() && chunk.id==W3D::W3DChunkBox) {
				W3D::W3DBoxDescription box;if(!W3D::W3DRead_Box(chunk.payload,box,error)) return false;
				description->collision.boxes.push_back({box.name,box.center,box.extent,
					(box.attributes>>W3D::W3DBoxAttributeCollisionTypeShift)&0xffu,box.Is_Aligned()});
			} else if (selector.empty() && (chunk.id == W3D::W3DChunkHierarchy || chunk.id == W3D::W3DChunkAnimation)) {
				W3DAdapterDetail::Read_Top_Level_Dependency(chunk, *description);
			}
			return true;
		}))
			return {nullptr, error.empty() ? "W3D top-level chunk traversal failed" : std::move(error)};

		if (!found_mesh || description->vertices.empty() || description->indices.empty())
			return {nullptr, selector.empty() ? "W3D source contains no static mesh" : "W3D render-object selector not found"};
		if (description->name.empty())
			description->name = identity.canonical_name;
		if (!description->bounds.Is_Valid())
			return {nullptr, "W3D mesh has invalid bounds"};
		return {std::move(description), {}};
	}

	// Skeleton (hierarchy) and animation files: the hierarchy's bones and the
	// file's clips, as the original catalog published them on demand.
	bool Import_Rig(const AssetIdentity &identity, std::span<const std::byte> source, ModelRigDesc &result,
		std::string &error) const override;
	ModelImportResult Import_With_Source(const AssetIdentity &identity, std::span<const std::byte> bytes, const AssetSource &source) const override
	{
		// Explicit mesh requests retain their bounded single-export contract.
		if (identity.canonical_name.find("::") != std::string::npos) return Import(identity, bytes);
		bool aggregate{}, mesh{};
		if (!W3D::W3DVisit_Chunks(bytes, [&](const auto &chunk) {
			aggregate |= chunk.id == 0x600; mesh |= chunk.id == W3D::W3DChunkMesh; return true;
		})) return {nullptr, "malformed W3D composition source"};
		if (aggregate || !mesh) return W3D::W3DResolve_Model(identity, bytes, source);
		return Import(identity, bytes);
	}
};

bool W3DAdapter::Import_Rig(const AssetIdentity &, std::span<const std::byte> source, ModelRigDesc &result,
	std::string &error) const
{
	if (source.empty()) {
		error = "W3D source is empty";
		return false;
	}
	if (!W3D::W3DValidate_Chunk_Tree(source)) {
		error = "W3D source contains a malformed or truncated chunk";
		return false;
	}
	ModelRigDesc rig;
	if (!W3D::W3DRead_Model_Rig(source, rig, error))
		return false;
	if (rig.bones.empty() && rig.animations.empty()) {
		error = "W3D source contains no hierarchy or animation";
		return false;
	}
	result = std::move(rig);
	error.clear();
	return true;
}

}
