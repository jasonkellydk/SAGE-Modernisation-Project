module;

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

export module Assets.Adapters.W3D.Mesh;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Materials;
import Assets.Math;
import Assets.Models;

namespace Assets::W3D
{

export struct W3DMeshHeader final
{
	std::uint32_t version = 0;
	std::uint32_t attributes = 0;
	std::string name;
	std::string container_name;
	std::uint32_t triangle_count = 0;
	std::uint32_t vertex_count = 0;
	std::uint32_t material_count = 0;
	std::int32_t sort_level = 0;
	std::uint32_t vertex_channels = 0;
	std::uint32_t face_channels = 0;
	Bounds3f bounds{};
	float sphere_radius = 0.0f;
};

export struct W3DParsedMesh final
{
	W3DMeshHeader header;
	std::vector<Vector3f> positions;
	std::vector<Vector3f> normals;
	std::vector<Vector2f> legacy_texcoords;
	std::vector<Vector2f> stage_texcoords;
	std::vector<Color4f> colors;
	std::vector<std::uint16_t> bone_indices;
	std::vector<std::array<std::uint32_t, 3>> triangles;
	W3DMaterialData materials;
};

namespace MeshDetail
{

bool Parse_Mesh_Header(W3DByteSpan bytes, W3DMeshHeader &header)
{
	// GeneralsMD writes the 116-byte W3dMeshHeader3Struct. Reading by offset
	// avoids compiler packing and keeps WW3D2 types outside this boundary.
	if (bytes.size() < 116)
		return false;

	std::uint32_t sort_level_bits = 0;
	Vector3f sphere_center;
	if (!W3DRead_U32(bytes, 0, header.version) ||
		!W3DRead_U32(bytes, 4, header.attributes) ||
		!W3DRead_U32(bytes, 40, header.triangle_count) ||
		!W3DRead_U32(bytes, 44, header.vertex_count) ||
		!W3DRead_U32(bytes, 48, header.material_count) ||
		!W3DRead_U32(bytes, 56, sort_level_bits) ||
		!W3DRead_U32(bytes, 68, header.vertex_channels) ||
		!W3DRead_U32(bytes, 72, header.face_channels) ||
		!W3DRead_Vector3(bytes, 76, header.bounds.minimum) ||
		!W3DRead_Vector3(bytes, 88, header.bounds.maximum) ||
		!W3DRead_Vector3(bytes, 100, sphere_center) ||
		!W3DRead_F32(bytes, 112, header.sphere_radius))
		return false;

	header.sort_level = std::bit_cast<std::int32_t>(sort_level_bits);
	if (!std::isfinite(sphere_center.x) || !std::isfinite(sphere_center.y) || !std::isfinite(sphere_center.z) ||
		!std::isfinite(header.sphere_radius) || header.sphere_radius < 0.0f)
		return false;

	header.name = W3DRead_Fixed_String(bytes, 8, 16);
	header.container_name = W3DRead_Fixed_String(bytes, 24, 16);
	return header.bounds.Is_Valid();
}

bool Read_Vector_Array(W3DByteSpan bytes, std::uint32_t count, std::vector<Vector3f> &values)
{
	if (count > bytes.size() / 12 || bytes.size() != static_cast<std::size_t>(count) * 12)
		return false;

	values.resize(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		if (!W3DRead_Vector3(bytes, static_cast<std::size_t>(index) * 12, values[index]) ||
			!std::isfinite(values[index].x) || !std::isfinite(values[index].y) || !std::isfinite(values[index].z))
			return false;
	}
	return true;
}

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
		values[index].y = 1.0f - values[index].y;
	}
	return true;
}

bool Read_Triangle_Array(
	W3DByteSpan bytes,
	std::uint32_t count,
	std::vector<std::array<std::uint32_t, 3>> &triangles)
{
	if (count > bytes.size() / 32 || bytes.size() != static_cast<std::size_t>(count) * 32)
		return false;

	triangles.resize(count);
	for (std::uint32_t index = 0; index < count; ++index) {
		const std::size_t offset = static_cast<std::size_t>(index) * 32;
		for (std::size_t vertex = 0; vertex < 3; ++vertex) {
			if (!W3DRead_U32(bytes, offset + vertex * 4, triangles[index][vertex]))
				return false;
		}
	}
	return true;
}

}

export bool W3DParse_Mesh(W3DByteSpan bytes, W3DParsedMesh &mesh, std::string &error)
{
	if (!W3DValidate_Chunk_Tree(bytes)) {
		error = "malformed chunk tree";
		return false;
	}

	bool has_header = false;
	if (!W3DVisit_Chunks(bytes, [&mesh, &has_header](const W3DChunkView &chunk) {
		if (chunk.id != W3DChunkMeshHeader3)
			return true;
		if (has_header)
			return false;
		has_header = MeshDetail::Parse_Mesh_Header(chunk.payload, mesh.header);
		return has_header;
	})) {
		error = "invalid mesh header chunk";
		return false;
	}
	if (!has_header || mesh.header.vertex_count == 0 || mesh.header.triangle_count == 0 || mesh.header.face_channels == 0) {
		error = "mesh header has no usable geometry";
		return false;
	}

	if (!W3DVisit_Chunks(bytes, [&mesh](const W3DChunkView &chunk) {
		switch (chunk.id) {
			case W3DChunkVertices:
				return MeshDetail::Read_Vector_Array(chunk.payload, mesh.header.vertex_count, mesh.positions);
			case W3DChunkVertexNormals:
				return MeshDetail::Read_Vector_Array(chunk.payload, mesh.header.vertex_count, mesh.normals);
			case W3DChunkTextureCoords:
				return MeshDetail::Read_UV_Array(chunk.payload, mesh.header.vertex_count, mesh.legacy_texcoords);
			case W3DChunkTriangles:
				return MeshDetail::Read_Triangle_Array(chunk.payload, mesh.header.triangle_count, mesh.triangles);
			case W3DChunkVertexColors:
				if (chunk.payload.size() != static_cast<std::size_t>(mesh.header.vertex_count) * 4)
					return false;
				mesh.colors.resize(mesh.header.vertex_count);
				for (std::uint32_t index = 0; index < mesh.header.vertex_count; ++index) {
					const auto *color = reinterpret_cast<const std::uint8_t *>(chunk.payload.data() + index * 4);
					mesh.colors[index] = {
						static_cast<float>(color[0]) / 255.0f,
						static_cast<float>(color[1]) / 255.0f,
						static_cast<float>(color[2]) / 255.0f,
						1.0f};
				}
				return true;
			case W3DChunkVertexInfluences:
				if (chunk.payload.size() != static_cast<std::size_t>(mesh.header.vertex_count) * 8)
					return false;
				mesh.bone_indices.resize(mesh.header.vertex_count);
				for (std::uint32_t index = 0; index < mesh.header.vertex_count; ++index) {
					const auto *data = reinterpret_cast<const std::uint8_t *>(chunk.payload.data() + index * 8);
					const std::uint32_t bone_index = static_cast<std::uint32_t>(data[0]) |
						(static_cast<std::uint32_t>(data[1]) << 8);
					if (bone_index > std::numeric_limits<std::uint16_t>::max())
						return false;
					mesh.bone_indices[index] = static_cast<std::uint16_t>(bone_index);
				}
				return true;
			case W3DChunkVertexShadeIndices:
				return chunk.payload.size() == static_cast<std::size_t>(mesh.header.vertex_count) * 4;
			case W3DChunkMeshUserText:
			case W3DChunkMaterialInfo:
			case W3DChunkShaders:
			case W3DChunkVertexMaterials:
			case W3DChunkTextures:
			case W3DChunkMaterialPass:
				return true;
			default:
				return true;
		}
	})) {
		error = "invalid mesh data chunk";
		return false;
	}

	if (mesh.positions.size() != mesh.header.vertex_count ||
		mesh.triangles.size() != mesh.header.triangle_count ||
		(!mesh.normals.empty() && mesh.normals.size() != mesh.header.vertex_count)) {
		error = "mesh data counts do not match the header";
		return false;
	}

	if (!W3DParse_Materials(bytes, mesh.header.vertex_count, mesh.materials)) {
		error = "invalid material chunk";
		return false;
	}
	if (mesh.materials.passes.empty() && mesh.materials.vertex_materials.empty() && mesh.header.material_count != 0) {
		error = "mesh declares materials but contains no material data";
		return false;
	}
	for (const W3DMaterialPass &pass : mesh.materials.passes) {
		if (pass.vertex_material_index >= mesh.materials.vertex_materials.size()) {
			error = "material pass references a vertex material outside the material table";
			return false;
		}
		if (pass.texture_index != W3DInvalidIndex && pass.texture_index >= mesh.materials.textures.size()) {
			error = "material pass references a texture outside the texture table";
			return false;
		}
	}
	for (const auto &triangle : mesh.triangles) {
		for (const std::uint32_t index : triangle) {
			if (index >= mesh.positions.size()) {
				error = "triangle references a vertex outside the vertex array";
				return false;
			}
		}
	}

	if (mesh.normals.empty())
		mesh.normals.assign(mesh.header.vertex_count, {0.0f, 0.0f, 1.0f});
	if (mesh.colors.empty())
		mesh.colors.assign(mesh.header.vertex_count, {});
	if (mesh.legacy_texcoords.size() != mesh.header.vertex_count)
		mesh.legacy_texcoords.assign(mesh.header.vertex_count, {});
	if (!mesh.materials.passes.empty() && !mesh.materials.passes.front().texcoords.empty())
		mesh.stage_texcoords = std::move(mesh.materials.passes.front().texcoords);
	if (mesh.stage_texcoords.size() != mesh.header.vertex_count)
		mesh.stage_texcoords = mesh.legacy_texcoords;

	if (mesh.materials.vertex_materials.empty())
		mesh.materials.vertex_materials.push_back({"default", {}, {}, {}, 1.0f, 1.0f, 0.0f, 0});

	return true;
}

}
