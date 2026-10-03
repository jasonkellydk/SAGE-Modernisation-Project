module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DMeshTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.W3DMeshParser;
import std;
import Assets.Adapters.W3D;
import Assets.Identity;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Mesh;
import Assets.Materials;
import Assets.Models;
import Assets.Adapters.W3D.Model;
import Assets.Adapters.W3D.MeshData;

namespace
{

using Byte = std::byte;

void Append_U32(std::vector<Byte> &bytes, std::uint32_t value)
{
	bytes.push_back(static_cast<Byte>(value & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 8) & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 16) & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 24) & 0xFF));
}

void Append_F32(std::vector<Byte> &bytes, float value)
{
	Append_U32(bytes, std::bit_cast<std::uint32_t>(value));
}

void Write_U32(std::vector<Byte> &bytes, std::size_t offset, std::uint32_t value)
{
	bytes[offset + 0] = static_cast<Byte>(value & 0xFF);
	bytes[offset + 1] = static_cast<Byte>((value >> 8) & 0xFF);
	bytes[offset + 2] = static_cast<Byte>((value >> 16) & 0xFF);
	bytes[offset + 3] = static_cast<Byte>((value >> 24) & 0xFF);
}

void Write_F32(std::vector<Byte> &bytes, std::size_t offset, float value)
{
	Write_U32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void Write_Fixed_String(std::vector<Byte> &bytes, std::size_t offset, std::size_t length, std::string_view value)
{
	for (std::size_t index = 0; index < length; ++index)
		bytes[offset + index] = index < value.size() ? static_cast<Byte>(value[index]) : Byte{0};
}

void Append_Vector3(std::vector<Byte> &bytes, float x, float y, float z)
{
	Append_F32(bytes, x);
	Append_F32(bytes, y);
	Append_F32(bytes, z);
}

void Append_Chunk(std::vector<Byte> &bytes, std::uint32_t id, const std::vector<Byte> &payload, bool children)
{
	Append_U32(bytes, id);
	Append_U32(bytes, static_cast<std::uint32_t>(payload.size()) | (children ? 0x80000000u : 0u));
	bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<Byte> Make_Mesh(bool invalid_index)
{
	std::vector<Byte> header(116, Byte{0});
	Write_U32(header, 0, 0x00030000);
	Write_Fixed_String(header, 8, 16, "mesh");
	Write_Fixed_String(header, 24, 16, "container");
	Write_U32(header, 40, 1);
	Write_U32(header, 44, 3);
	Write_U32(header, 68, 7);
	Write_U32(header, 72, 1);
	Write_F32(header, 76, 0.0f);
	Write_F32(header, 80, 0.0f);
	Write_F32(header, 84, 0.0f);
	Write_F32(header, 88, 1.0f);
	Write_F32(header, 92, 1.0f);
	Write_F32(header, 96, 0.0f);
	Write_F32(header, 100, 0.333f);
	Write_F32(header, 104, 0.333f);
	Write_F32(header, 112, 1.0f);

	std::vector<Byte> mesh;
	Append_Chunk(mesh, Assets::W3D::W3DChunkMeshHeader3, header, false);
	std::vector<Byte> vertices;
	Append_Vector3(vertices, 0.0f, 0.0f, 0.0f);
	Append_Vector3(vertices, 1.0f, 0.0f, 0.0f);
	Append_Vector3(vertices, 0.0f, 1.0f, 0.0f);
	Append_Chunk(mesh, Assets::W3D::W3DChunkVertices, vertices, false);
	std::vector<Byte> triangles;
	Append_U32(triangles, 0);
	Append_U32(triangles, 1);
	Append_U32(triangles, invalid_index ? 4 : 2);
	Append_U32(triangles, 0);
	Append_Vector3(triangles, 0.0f, 0.0f, 1.0f);
	Append_F32(triangles, 0.0f);
	Append_Chunk(mesh, Assets::W3D::W3DChunkTriangles, triangles, false);
	return mesh;
}

}

BOOST_AUTO_TEST_CASE(optional_skin_bindings_preserve_weights_and_reject_malformed_payloads)
{
    using namespace Assets::W3D;
    for (int scenario = 0; scenario < 6; ++scenario) {
        auto bytes = Make_Mesh(false);
        Write_U32(bytes, 12, 0x20000);
        std::vector<Byte> payload;
        Append_U32(payload, scenario == 1 ? 2 : 1); Append_U32(payload, 3);
        for (int i = 0; i < 3; ++i) {
            Append_U32(payload, 2u | (5u << 16)); Append_U32(payload, 0);
            Append_F32(payload, scenario == 2 ? -0.4f : 0.4f);
            Append_F32(payload, scenario == 3 ? 0.5f : 0.6f);
            Append_F32(payload, 0); Append_F32(payload, 0);
        }
        if (scenario == 4) payload.pop_back();
        Append_Chunk(bytes, W3DChunkSkinBindings, payload, false);
        if (scenario == 5) Append_Chunk(bytes, W3DChunkSkinBindings, payload, false);
        W3DParsedMesh mesh; std::string error;
        const bool parsed = W3DParse_Mesh(bytes, mesh, error);
        BOOST_TEST(parsed == (scenario == 0));
        if (parsed) {
            BOOST_REQUIRE_EQUAL(mesh.skin_indices.size(), 3u);
            BOOST_TEST(mesh.skin_indices[0][1] == 5u);
            BOOST_TEST(mesh.skin_weights[0][0] == 0.4f);
            BOOST_TEST(mesh.skin_weights[0][1] == 0.6f);
        }
    }
}

BOOST_AUTO_TEST_CASE(legacy_influence_padding_does_not_enable_weighted_skin)
{
    using namespace Assets::W3D;
    auto bytes = Make_Mesh(false);
    std::vector<Byte> payload;
    for (int i = 0; i < 3; ++i) {
        Append_U32(payload, 2u | (5u << 16));
        Append_U32(payload, 40u | (60u << 16));
    }
    Append_Chunk(bytes, W3DChunkVertexInfluences, payload, false);
    W3DParsedMesh mesh; std::string error;
    BOOST_REQUIRE(W3DParse_Mesh(bytes, mesh, error));
    BOOST_TEST(mesh.bone_indices[0] == 2u);
    BOOST_TEST(mesh.skin_indices.empty());
    BOOST_TEST(mesh.skin_weights.empty());
}

BOOST_AUTO_TEST_CASE(mesh_parser_extracts_geometry_and_bounds)
{
	Assets::W3D::W3DParsedMesh mesh;
	std::string error;
	BOOST_REQUIRE(Assets::W3D::W3DParse_Mesh(Make_Mesh(false), mesh, error));
	BOOST_REQUIRE_EQUAL(mesh.positions.size(), 3);
	BOOST_REQUIRE_EQUAL(mesh.triangles.size(), 1);
	BOOST_CHECK(mesh.triangles[0][2] == 2);
	BOOST_CHECK(mesh.header.bounds.Is_Valid());
	BOOST_CHECK(mesh.header.name == "mesh");
}

BOOST_AUTO_TEST_CASE(authored_triangle_surface_and_hidden_physical_category_survive_decode) {
    using namespace Assets::W3D;
    auto bytes=Make_Mesh(false);Write_U32(bytes,12,W3DMeshAttributeHidden|0x10);
    BOOST_REQUIRE(W3DVisit_Chunks(bytes,[&](const W3DChunkView& chunk) {
        if(chunk.id==W3DChunkTriangles) Write_U32(bytes,static_cast<std::size_t>(chunk.payload.data()-bytes.data())+12,23);
        return true;
    }));
    W3DParsedMesh mesh;std::string error;BOOST_REQUIRE(W3DParse_Mesh(bytes,mesh,error));
    BOOST_REQUIRE(mesh.triangle_surfaces.size()==1u);BOOST_TEST(mesh.triangle_surfaces[0]==23u);
    Assets::ModelAssetDesc model;W3DAppend_Mesh(model,mesh);
    BOOST_REQUIRE(model.collision.parts.size()==1u);BOOST_TEST(model.collision.parts[0].categories==1u);
    BOOST_TEST(model.collision.surfaces[0]==23u);BOOST_TEST(model.collision.triangles[0][2]==2u);
}

BOOST_AUTO_TEST_CASE(prelit_runtime_selects_source_default_and_preserves_each_lightmap_pass)
{
	using namespace Assets::W3D;
	auto bytes = Make_Mesh(false);
	Write_U32(bytes, 12, 0x06000000); // Vertex and multipass sets available.
	Write_U32(bytes, 56, 1);
	const auto materials = [](unsigned passes, unsigned emissive) {
		std::vector<Byte> result, info;
		for (const auto value : {passes, 1u, 1u, 0u}) Append_U32(info, value);
		Append_Chunk(result, W3DChunkMaterialInfo, info, false);
		std::vector<Byte> table, material, values(32, Byte{0});
		values[16] = values[17] = values[18] = Byte(emissive);
		Write_F32(values, 20, 1); Write_F32(values, 24, 1);
		Append_Chunk(material, W3DChunkVertexMaterialName, {Byte{'M'}, Byte{0}}, false);
		Append_Chunk(material, W3DChunkVertexMaterialInfo, values, false);
		// Shipped prelit containers omit the advisory child bit for these
		// semantic containers. Their bounded payload still contains chunks.
		Append_Chunk(table, W3DChunkVertexMaterial, material, false);
		Append_Chunk(result, W3DChunkVertexMaterials, table, false);
		std::vector<Byte> shader(16, Byte{0}); shader[0] = Byte{3}; shader[1] = Byte{1}; shader[6] = Byte{1};
		Append_Chunk(result, W3DChunkShaders, shader, false);
		for (unsigned i = 0; i < passes; ++i) {
			std::vector<Byte> pass, id, uv, stage;
			Append_U32(id, 0);
			Append_Chunk(pass, W3DChunkVertexMaterialIds, id, false);
			Append_Chunk(pass, W3DChunkShaderIds, id, false);
			for (unsigned vertex = 0; vertex < 3; ++vertex) {Append_F32(uv, i ? .75f : .25f); Append_F32(uv, i ? .1f : .9f);}
			Append_Chunk(stage, W3DChunkStageTextureCoords, uv, false);
			Append_Chunk(pass, W3DChunkTextureStage, stage, true);
			if (!i) {
				std::vector<Byte> dcg, dig;
				for (unsigned vertex = 0; vertex < 3; ++vertex) {Append_U32(dcg, 0xff0080ff); Append_U32(dig, 0x00ff80ff);}
				Append_Chunk(pass, 0x3b, dcg, false); Append_Chunk(pass, 0x3c, dig, false);
			}
			Append_Chunk(result, W3DChunkMaterialPass, pass, false);
		}
		return result;
	};
	Append_Chunk(bytes, W3DChunkPrelitVertex, materials(1, 64), true);
	Append_Chunk(bytes, W3DChunkPrelitLightmapMultiPass, materials(2, 255), true);
	W3DParsedMesh mesh; std::string error;
	BOOST_REQUIRE_MESSAGE(W3DParse_Mesh(bytes, mesh, error), error);
	BOOST_TEST(mesh.materials.passes.size() == 2u);
	BOOST_TEST(mesh.materials.vertex_materials[0].material.emissive_color.r == 1.f);
	Assets::ModelAssetDesc model; W3DAppend_Mesh(model, mesh);
	BOOST_REQUIRE_EQUAL(model.submeshes.size(), 2u);
	const auto &base = model.vertices[model.indices[model.submeshes[0].first_index]];
	const auto &lightmap = model.vertices[model.indices[model.submeshes[1].first_index]];
	BOOST_TEST(base.texcoord.x == .25f); BOOST_TEST(lightmap.texcoord.x == .75f);
	BOOST_TEST(base.texcoord.y == .1f, boost::test_tools::tolerance(.0001f));
	BOOST_TEST(lightmap.texcoord.y == .9f, boost::test_tools::tolerance(.0001f));
	BOOST_TEST(base.color.g == (128.f / 255.f) * (128.f / 255.f));
	BOOST_TEST(lightmap.color.g == 1.f);
	W3DParsedMesh vertex;
	BOOST_REQUIRE(W3DParse_Mesh(bytes, vertex, error, W3DMeshPrelighting::Vertex));
	BOOST_TEST(vertex.materials.passes.size() == 1u);
	BOOST_TEST(vertex.prelit_chunk == W3DChunkPrelitVertex);
	Assets::ModelAssetDesc vertex_model;W3DAppend_Mesh(vertex_model,vertex);
	BOOST_TEST(!vertex_model.submeshes[0].lighting_enabled);
	BOOST_TEST(vertex.materials.vertex_materials[0].material.emissive_color.r == 64.f / 255.f);
	Append_Chunk(bytes, W3DChunkPrelitLightmapMultiPass, materials(2, 255), true);
	W3DParsedMesh duplicate; BOOST_TEST(!W3DParse_Mesh(bytes, duplicate, error));
}

BOOST_AUTO_TEST_CASE(container_mesh_selection_is_exact_and_keeps_whole_file_requests)
{
	using namespace Assets::W3D;
	auto first = Make_Mesh(false), second = Make_Mesh(false);
	Write_Fixed_String(first, 16, 16, "first"); Write_Fixed_String(second, 16, 16, "second");
	std::vector<Byte> source;
	Append_Chunk(source, W3DChunkMesh, first, true); Append_Chunk(source, W3DChunkMesh, second, true);
	Assets::W3DAdapter adapter;
	const auto whole = adapter.Import({Assets::AssetType::Model, "container.w3d"}, source);
	BOOST_REQUIRE_MESSAGE(whole.description, whole.error); BOOST_TEST(whole.description->indices.size() == 6u);
	const auto selected = adapter.Import({Assets::AssetType::Model, "container.w3d::container.second"}, source);
	BOOST_REQUIRE_MESSAGE(selected.description, selected.error); BOOST_TEST(selected.description->indices.size() == 3u);
	BOOST_TEST(selected.description->name == "second"); BOOST_TEST(selected.description->rig.bones.empty());
	BOOST_TEST(!adapter.Import({Assets::AssetType::Model, "container.w3d::container.second_suffix"}, source).description);
	BOOST_TEST(!adapter.Import({Assets::AssetType::Model, "container.w3d::"}, source).description);
	Append_Chunk(source, W3DChunkMesh, second, true);
	BOOST_TEST(!adapter.Import({Assets::AssetType::Model, "container.w3d::container.second"}, source).description);
}

BOOST_AUTO_TEST_CASE(mesh_parser_rejects_out_of_range_indices)
{
	Assets::W3D::W3DParsedMesh mesh;
	std::string error;
	BOOST_CHECK(!Assets::W3D::W3DParse_Mesh(Make_Mesh(true), mesh, error));
	BOOST_CHECK(!error.empty());
}

namespace
{
void Counted_String(std::vector<Byte> &bytes, std::string_view value)
{
	Append_U32(bytes, static_cast<std::uint32_t>(value.size() + 1));
	for (char c : value) bytes.push_back(static_cast<Byte>(c));
	bytes.push_back(Byte{0});
}

std::vector<Byte> Surface_Mesh(std::uint32_t material_id, bool extra_id = false, bool bad_uv = false,
	bool auxiliary_uv = false, bool auxiliary_texture = false)
{
	using namespace Assets::W3D;
	auto mesh = Make_Mesh(false);
	std::vector<Byte> header(37);
	header[0] = Byte{1};
	Write_Fixed_String(header, 1, 32, "objectsallied.fx");
	std::vector<Byte> material, table;
	Append_Chunk(material, W3DChunkShaderMaterialHeader, header, false);
	for (auto name : {"DiffuseTexture", "NormalMap", "SpecMap"}) {
		std::vector<Byte> property;
		Append_U32(property, 1); Counted_String(property, name); Counted_String(property, std::string(name) + ".dds");
		Append_Chunk(material, W3DChunkShaderMaterialProperty, property, false);
	}
	Append_Chunk(table, W3DChunkShaderMaterial, material, true);
	Append_Chunk(mesh, W3DChunkShaderMaterials, table, true);
	std::vector<Byte> pass, ids;
	Append_U32(ids, material_id);
	if (extra_id) Append_U32(ids, material_id);
	Append_Chunk(pass, W3DChunkShaderMaterialIds, ids, false);
	std::vector<Byte> stage, uvs, uv_ids;
	for (float coordinate : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, .5f, .5f}) Append_F32(uvs, coordinate);
	Append_U32(uv_ids, 3); Append_U32(uv_ids, 1); Append_U32(uv_ids, bad_uv ? 4 : 2);
	Append_Chunk(stage, W3DChunkStageTextureCoords, uvs, false);
	Append_Chunk(stage, W3DChunkPerFaceTextureCoordIds, uv_ids, false);
	Append_Chunk(pass, W3DChunkTextureStage, stage, true);
	if (auxiliary_uv) {
		if (auxiliary_texture) {
			std::vector<Byte> texture; Append_U32(texture, 0);
			Append_Chunk(stage, W3DChunkTextureIds, texture, false);
		}
		Append_Chunk(pass, W3DChunkTextureStage, stage, true);
	}
	Append_Chunk(mesh, W3DChunkMaterialPass, pass, true);
	return mesh;
}
}

BOOST_AUTO_TEST_CASE(shader_only_pass_resolves_material_and_retains_indexed_uvs)
{
	Assets::W3D::W3DParsedMesh mesh;
	std::string error;
	BOOST_REQUIRE_MESSAGE(Assets::W3D::W3DParse_Mesh(Surface_Mesh(0), mesh, error), error);
	BOOST_REQUIRE_EQUAL(mesh.surface_materials.size(), 1u);
	BOOST_CHECK(mesh.surface_materials[0].surface.shading_model == Assets::MaterialShadingModel::SpecularGlossiness);
	BOOST_TEST(mesh.materials.passes[0].uses_shader_material);
	BOOST_REQUIRE_EQUAL(mesh.shader_pass_bindings.size(), 1u);
	BOOST_TEST(mesh.shader_pass_bindings[0].stages[0].texcoords.size() == 4u);
	BOOST_TEST(mesh.shader_pass_bindings[0].stages[0].face_texcoord_ids[0][0] == 3u);
}

BOOST_AUTO_TEST_CASE(object_shader_retains_valid_auxiliary_uvs_without_enabling_an_extra_texture_stage)
{
	Assets::W3D::W3DParsedMesh mesh;
	std::string error;
	BOOST_REQUIRE_MESSAGE(Assets::W3D::W3DParse_Mesh(Surface_Mesh(0,false,false,true),mesh,error),error);
	BOOST_REQUIRE_EQUAL(mesh.shader_pass_bindings[0].stages.size(),2u);
	BOOST_TEST(mesh.shader_pass_bindings[0].stages[1].texcoords[3].x==.5f);
	BOOST_TEST(!Assets::W3D::W3DParse_Mesh(Surface_Mesh(0,false,false,true,true),mesh,error));
	BOOST_TEST(error.find("auxiliary texture binding")!=std::string::npos);
}

BOOST_AUTO_TEST_CASE(shader_pass_rejects_out_of_range_material_and_invalid_face_or_uv_counts)
{
	for (const auto &bytes : {Surface_Mesh(1), Surface_Mesh(0, true), Surface_Mesh(0, false, true)}) {
		Assets::W3D::W3DParsedMesh mesh;
		std::string error;
		BOOST_TEST(!Assets::W3D::W3DParse_Mesh(bytes, mesh, error));
		BOOST_TEST(!error.empty());
	}
}

BOOST_AUTO_TEST_CASE(mesh_tangent_arrays_preserve_authored_vectors_and_reject_corruption)
{
	using namespace Assets::W3D;
	for (int scenario = 0; scenario < 4; ++scenario) {
		auto bytes = Surface_Mesh(0);
		std::vector<Byte> tangents, bitangents;
		for (int vertex = 0; vertex < 3; ++vertex) {
			Append_Vector3(tangents, scenario == 1 ? std::numeric_limits<float>::infinity() : 1, 0, 0);
			Append_Vector3(bitangents, 0, 1, 0);
		}
		if (scenario == 2) tangents.pop_back();
		Append_Chunk(bytes, W3DChunkTangents, tangents, false);
		Append_Chunk(bytes, W3DChunkBitangents, bitangents, false);
		if (scenario == 3) Append_Chunk(bytes, W3DChunkTangents, tangents, false);
		W3DParsedMesh mesh; std::string error;
		const bool parsed = W3DParse_Mesh(bytes, mesh, error);
		BOOST_TEST(parsed == (scenario == 0));
		if (parsed) {
			BOOST_REQUIRE_EQUAL(mesh.tangents.size(), 3u);
			BOOST_TEST(mesh.tangents[0].x == 1.0f);
			BOOST_TEST(mesh.bitangents[0].y == 1.0f);
		}
	}
}
