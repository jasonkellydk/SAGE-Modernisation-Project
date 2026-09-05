module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

export module Assets.Adapters.W3D.Model;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Materials;
import Assets.Adapters.W3D.Mesh;
import Assets.Identity;
import Assets.Models;
import Assets.Materials;

namespace Assets::W3D
{

namespace ModelBuilderDetail
{

void Apply_Shader_Settings(ModelMaterialDesc &material, const W3DShaderSettings &shader) noexcept
{
	material.depth_write = shader.depth_mask != 0;
	material.texturing = shader.texturing != 0;
	if (shader.alpha_test != 0) {
		material.render_mode = MaterialRenderMode::AlphaTest;
		return;
	}
	if (shader.source_blend == 1 && shader.destination_blend == 1) {
		material.render_mode = MaterialRenderMode::Additive;
		return;
	}
	if (shader.source_blend == 1 && shader.destination_blend == 2) {
		material.render_mode = MaterialRenderMode::Multiply;
		return;
	}
	if (shader.source_blend == 2 && shader.destination_blend == 5) {
		material.render_mode = MaterialRenderMode::AlphaBlend;
		return;
	}
	if (!material.depth_write)
		material.render_mode = MaterialRenderMode::AlphaBlend;
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

}

export void W3DAppend_Mesh(ModelAssetDesc &description, W3DParsedMesh &mesh)
{
	const bool first_mesh = description.vertices.empty();
	const std::uint32_t vertex_base = static_cast<std::uint32_t>(description.vertices.size());
	const std::uint32_t index_base = static_cast<std::uint32_t>(description.indices.size());
	const std::uint32_t material_base = static_cast<std::uint32_t>(description.materials.size());

	description.vertices.reserve(description.vertices.size() + mesh.positions.size());
	for (std::size_t index = 0; index < mesh.positions.size(); ++index) {
		ModelVertexDesc vertex;
		vertex.position = mesh.positions[index];
		vertex.normal = mesh.normals[index];
		vertex.texcoord = mesh.stage_texcoords[index];
		vertex.color = mesh.colors[index];
		if (!mesh.bone_indices.empty()) {
			vertex.bone_indices[0] = mesh.bone_indices[index];
			vertex.bone_weights = {1.0f, 0.0f, 0.0f, 0.0f};
		}
		 description.vertices.push_back(vertex);
	}
	if (!mesh.bone_indices.empty()) {
		std::uint16_t maximum_bone = 0;
		for (const std::uint16_t bone_index : mesh.bone_indices)
			maximum_bone = std::max(maximum_bone, bone_index);
		description.skin_bone_count = std::max(
			description.skin_bone_count,
			static_cast<std::uint32_t>(maximum_bone) + 1u);
	}

	description.indices.reserve(description.indices.size() + mesh.triangles.size() * 3);
	for (const auto &triangle : mesh.triangles) {
		for (const std::uint32_t index : triangle)
			description.indices.push_back(vertex_base + index);
	}

	std::uint32_t submesh_material_count = 0;
	if (mesh.materials.passes.empty()) {
		for (ModelMaterialDesc &material : mesh.materials.vertex_materials)
			description.materials.push_back(std::move(material));
		submesh_material_count = description.materials.size() == material_base ? 0 : 1;
	} else {
		for (const W3DMaterialPass &pass : mesh.materials.passes) {
			if (pass.vertex_material_index >= mesh.materials.vertex_materials.size())
				continue;
			ModelMaterialDesc material = mesh.materials.vertex_materials[pass.vertex_material_index];
			if (pass.texture_index < mesh.materials.textures.size())
				material.primary_texture = mesh.materials.textures[pass.texture_index];
			if (pass.shader_index < mesh.materials.shaders.size())
				ModelBuilderDetail::Apply_Shader_Settings(material, mesh.materials.shaders[pass.shader_index]);
			description.materials.push_back(std::move(material));
			++submesh_material_count;
		}
	}
	if (description.materials.size() == material_base)
		description.materials.push_back({"default", {}, {}, {}, 1.0f, 1.0f, 0.0f, 0});

	if (submesh_material_count == 0)
		submesh_material_count = 1;
	for (std::uint32_t material_index = 0; material_index < submesh_material_count; ++material_index) {
		description.submeshes.push_back({
		index_base,
		static_cast<std::uint32_t>(description.indices.size() - index_base),
		material_base + material_index,
		mesh.header.name});
	}

	for (std::size_t material_index = material_base; material_index < description.materials.size(); ++material_index) {
		ModelMaterialDesc &material = description.materials[material_index];
		material.scope = MaterialScope::Model;
		ModelBuilderDetail::Add_Dependency(description, AssetType::Material, material.name);
		ModelBuilderDetail::Add_Dependency(description, AssetType::Texture, material.primary_texture);
		ModelBuilderDetail::Add_Dependency(description, AssetType::Texture, material.secondary_texture);
	}

	if (description.name.empty())
		description.name = mesh.header.name;
	if (description.container_name.empty())
		description.container_name = mesh.header.container_name;
	description.source_attributes |= mesh.header.attributes;
	description.sort_level = mesh.header.sort_level;
	if (first_mesh) {
		description.bounds = mesh.header.bounds;
	} else {
		description.bounds.minimum.x = std::min(description.bounds.minimum.x, mesh.header.bounds.minimum.x);
		description.bounds.minimum.y = std::min(description.bounds.minimum.y, mesh.header.bounds.minimum.y);
		description.bounds.minimum.z = std::min(description.bounds.minimum.z, mesh.header.bounds.minimum.z);
		description.bounds.maximum.x = std::max(description.bounds.maximum.x, mesh.header.bounds.maximum.x);
		description.bounds.maximum.y = std::max(description.bounds.maximum.y, mesh.header.bounds.maximum.y);
		description.bounds.maximum.z = std::max(description.bounds.maximum.z, mesh.header.bounds.maximum.z);
	}
}

}
