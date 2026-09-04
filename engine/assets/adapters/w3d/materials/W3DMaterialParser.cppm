module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

export module Assets.Adapters.W3D.Materials;

import Assets.Adapters.W3D.Chunks;
import Assets.Math;
import Assets.Models;

namespace Assets::W3D
{

export struct W3DMaterialPass final
{
	std::uint32_t vertex_material_index = W3DInvalidIndex;
	std::uint32_t texture_index = W3DInvalidIndex;
	std::vector<Vector2f> texcoords;
};

export struct W3DMaterialInfo final
{
	std::uint32_t pass_count = 0;
	std::uint32_t vertex_material_count = 0;
	std::uint32_t shader_count = 0;
	std::uint32_t texture_count = 0;
};

export struct W3DMaterialData final
{
	std::vector<ModelMaterialDesc> vertex_materials;
	std::vector<std::string> textures;
	std::vector<W3DMaterialPass> passes;
	W3DMaterialInfo info;
	bool has_info = false;
};

namespace MaterialDetail
{

bool Read_UV_Array(W3DByteSpan bytes, std::uint32_t count, std::vector<Vector2f> &values)
{
	if (count > bytes.size() / 8 || bytes.size() != static_cast<std::size_t>(count) * 8)
		return false;

	values.resize(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		const std::size_t offset = static_cast<std::size_t>(index) * 8;
		if (!W3DRead_F32(bytes, offset, values[index].x) || !W3DRead_F32(bytes, offset + 4, values[index].y) ||
			!std::isfinite(values[index].x) || !std::isfinite(values[index].y))
			return false;
		// W3D stores V from the opposite origin used by generic model data.
		values[index].y = 1.0f - values[index].y;
	}
	return true;
}

bool Parse_Vertex_Material(W3DByteSpan bytes, ModelMaterialDesc &material)
{
	bool has_info = false;
	bool has_name = false;
	const bool valid = W3DVisit_Chunks(bytes, [&material, &has_info, &has_name](const W3DChunkView &chunk) {
		switch (chunk.id) {
			case W3DChunkVertexMaterialName:
				material.name = W3DRead_String(chunk.payload);
				has_name = true;
				return true;
			case W3DChunkVertexMaterialInfo: {
				if (chunk.payload.size() < 32)
					return false;
				std::uint32_t attributes = 0;
				if (!W3DRead_U32(chunk.payload, 0, attributes))
					return false;
				material.source_attributes = attributes;
				material.base_color = {
					static_cast<float>(static_cast<std::uint8_t>(chunk.payload[8])) / 255.0f,
					static_cast<float>(static_cast<std::uint8_t>(chunk.payload[9])) / 255.0f,
					static_cast<float>(static_cast<std::uint8_t>(chunk.payload[10])) / 255.0f,
					1.0f};
				return W3DRead_F32(chunk.payload, 20, material.shininess) &&
					W3DRead_F32(chunk.payload, 24, material.opacity) &&
					W3DRead_F32(chunk.payload, 28, material.translucency) &&
					std::isfinite(material.shininess) && std::isfinite(material.opacity) &&
					std::isfinite(material.translucency) && (has_info = true);
			}
			default:
				return true;
		}
	});
	return valid && has_info && has_name;
}

bool Parse_Vertex_Materials(W3DByteSpan bytes, std::vector<ModelMaterialDesc> &materials)
{
	return W3DVisit_Chunks(bytes, [&materials](const W3DChunkView &chunk) {
		if (chunk.id != W3DChunkVertexMaterial)
			return false;
		ModelMaterialDesc material;
		if (!chunk.contains_children || !Parse_Vertex_Material(chunk.payload, material))
			return false;
		materials.push_back(std::move(material));
		return true;
	});
}

bool Parse_Textures(W3DByteSpan bytes, std::vector<std::string> &textures)
{
	return W3DVisit_Chunks(bytes, [&textures](const W3DChunkView &chunk) {
		if (chunk.id != W3DChunkTexture || !chunk.contains_children)
			return false;
		std::string name;
		if (!W3DVisit_Chunks(chunk.payload, [&name](const W3DChunkView &child) {
			if (child.id == W3DChunkTextureName)
				name = W3DRead_String(child.payload);
			return true;
		}) || name.empty())
			return false;
		textures.push_back(std::move(name));
		return true;
	});
}

bool Parse_Texture_Stage(W3DByteSpan bytes, std::uint32_t vertex_count, W3DMaterialPass &pass)
{
	return W3DVisit_Chunks(bytes, [&pass, vertex_count](const W3DChunkView &chunk) {
		switch (chunk.id) {
			case W3DChunkTextureIds:
				if (chunk.payload.size() < 4 || chunk.payload.size() % 4 != 0)
					return false;
				return W3DRead_U32(chunk.payload, 0, pass.texture_index);
			case W3DChunkStageTextureCoords:
				return Read_UV_Array(chunk.payload, vertex_count, pass.texcoords);
			case W3DChunkPerFaceTextureCoordIds:
				return chunk.payload.size() % 12 == 0;
			default:
				return true;
		}
	});
}

bool Parse_Material_Pass(W3DByteSpan bytes, std::uint32_t vertex_count, W3DMaterialPass &pass)
{
	return W3DVisit_Chunks(bytes, [&pass, vertex_count](const W3DChunkView &chunk) {
		switch (chunk.id) {
			case W3DChunkVertexMaterialIds:
				if (chunk.payload.size() < 4 || chunk.payload.size() % 4 != 0)
					return false;
				return W3DRead_U32(chunk.payload, 0, pass.vertex_material_index);
			case W3DChunkTextureStage:
				return chunk.contains_children && Parse_Texture_Stage(chunk.payload, vertex_count, pass);
			case W3DChunkShaderIds:
				return chunk.payload.size() >= 4 && chunk.payload.size() % 4 == 0;
			default:
				return true;
		}
	});
}

}

export bool W3DParse_Materials(W3DByteSpan bytes, std::uint32_t vertex_count, W3DMaterialData &materials)
{
	materials = {};
	if (!W3DValidate_Chunk_Tree(bytes))
		return false;

	if (!W3DVisit_Chunks(bytes, [&materials, vertex_count](const W3DChunkView &chunk) {
		switch (chunk.id) {
			case W3DChunkMaterialInfo:
				if (chunk.payload.size() < 16 ||
					!W3DRead_U32(chunk.payload, 0, materials.info.pass_count) ||
					!W3DRead_U32(chunk.payload, 4, materials.info.vertex_material_count) ||
					!W3DRead_U32(chunk.payload, 8, materials.info.shader_count) ||
					!W3DRead_U32(chunk.payload, 12, materials.info.texture_count))
					return false;
				materials.has_info = true;
				return true;
			case W3DChunkVertexMaterials:
				return chunk.contains_children && MaterialDetail::Parse_Vertex_Materials(
					chunk.payload,
					materials.vertex_materials);
			case W3DChunkTextures:
				return chunk.contains_children && MaterialDetail::Parse_Textures(chunk.payload, materials.textures);
			case W3DChunkMaterialPass: {
				if (!chunk.contains_children)
					return false;
				W3DMaterialPass pass;
				if (!MaterialDetail::Parse_Material_Pass(chunk.payload, vertex_count, pass))
					return false;
				materials.passes.push_back(std::move(pass));
				return true;
			}
			default:
				return true;
		}
	}))
		return false;

	if (materials.has_info &&
		(materials.info.vertex_material_count != materials.vertex_materials.size() ||
		 materials.info.texture_count != materials.textures.size() ||
		 materials.info.pass_count != materials.passes.size()))
		return false;

	return true;
}

}
