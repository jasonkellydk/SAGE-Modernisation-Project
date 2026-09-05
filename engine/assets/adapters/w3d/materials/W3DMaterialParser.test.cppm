module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DMaterialTests

#include <boost/test/included/unit_test.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Assets.Tests.W3DMaterialParser;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Materials;

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

void Append_Chunk(std::vector<Byte> &bytes, std::uint32_t id, const std::vector<Byte> &payload, bool children)
{
	Append_U32(bytes, id);
	Append_U32(bytes, static_cast<std::uint32_t>(payload.size()) | (children ? 0x80000000u : 0u));
	bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void Append_String(std::vector<Byte> &bytes, std::string_view value)
{
	for (const char character : value)
		bytes.push_back(static_cast<Byte>(character));
	bytes.push_back(Byte{0});
}

void Write_F32(std::vector<Byte> &bytes, std::size_t offset, float value)
{
	const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
	bytes[offset + 0] = static_cast<Byte>(bits & 0xFF);
	bytes[offset + 1] = static_cast<Byte>((bits >> 8) & 0xFF);
	bytes[offset + 2] = static_cast<Byte>((bits >> 16) & 0xFF);
	bytes[offset + 3] = static_cast<Byte>((bits >> 24) & 0xFF);
}

std::vector<Byte> Make_Material_Data()
{
	std::vector<Byte> material_info;
	Append_U32(material_info, 1);
	Append_U32(material_info, 1);
	Append_U32(material_info, 1);
	Append_U32(material_info, 1);

	std::vector<Byte> vertex_material_info(32, Byte{0});
	vertex_material_info[4] = Byte{20};
	vertex_material_info[13] = Byte{80};
	vertex_material_info[18] = Byte{160};
	vertex_material_info[8] = static_cast<Byte>(200);
	vertex_material_info[9] = static_cast<Byte>(100);
	vertex_material_info[10] = static_cast<Byte>(50);
	// The first four bytes are attributes, followed by four RGBA values.
	vertex_material_info[0] = Byte{0};
	vertex_material_info[1] = Byte{0};
	vertex_material_info[2] = Byte{0};
	vertex_material_info[3] = Byte{0};
	Write_F32(vertex_material_info, 20, 8.0f);
	Write_F32(vertex_material_info, 24, 1.0f);
	Write_F32(vertex_material_info, 28, 0.0f);

	std::vector<Byte> vertex_material;
	std::vector<Byte> name;
	Append_String(name, "paint");
	Append_Chunk(vertex_material, Assets::W3D::W3DChunkVertexMaterialName, name, false);
	Append_Chunk(vertex_material, Assets::W3D::W3DChunkVertexMaterialInfo, vertex_material_info, false);
	std::vector<Byte> vertex_materials;
	Append_Chunk(vertex_materials, Assets::W3D::W3DChunkVertexMaterial, vertex_material, true);

	std::vector<Byte> texture;
	std::vector<Byte> texture_name;
	Append_String(texture_name, "paint.tga");
	Append_Chunk(texture, Assets::W3D::W3DChunkTextureName, texture_name, false);
	std::vector<Byte> textures;
	Append_Chunk(textures, Assets::W3D::W3DChunkTexture, texture, true);

	std::vector<Byte> stage;
	std::vector<Byte> texture_ids;
	Append_U32(texture_ids, 0);
	Append_Chunk(stage, Assets::W3D::W3DChunkTextureIds, texture_ids, false);
	std::vector<Byte> pass;
	std::vector<Byte> vertex_material_ids;
	Append_U32(vertex_material_ids, 0);
	Append_Chunk(pass, Assets::W3D::W3DChunkVertexMaterialIds, vertex_material_ids, false);
	Append_Chunk(pass, Assets::W3D::W3DChunkTextureStage, stage, true);

	std::vector<Byte> bytes;
	Append_Chunk(bytes, Assets::W3D::W3DChunkMaterialInfo, material_info, false);
	Append_Chunk(bytes, Assets::W3D::W3DChunkVertexMaterials, vertex_materials, true);
	Append_Chunk(bytes, Assets::W3D::W3DChunkTextures, textures, true);
	Append_Chunk(bytes, Assets::W3D::W3DChunkMaterialPass, pass, true);
	return bytes;
}

}

BOOST_AUTO_TEST_CASE(material_parser_preserves_material_texture_and_pass_relationships)
{
	Assets::W3D::W3DMaterialData data;
	BOOST_REQUIRE(Assets::W3D::W3DParse_Materials(Make_Material_Data(), 3, data));
	BOOST_REQUIRE_EQUAL(data.vertex_materials.size(), 1);
	BOOST_REQUIRE_EQUAL(data.textures.size(), 1);
	BOOST_REQUIRE_EQUAL(data.passes.size(), 1);
	BOOST_CHECK(data.vertex_materials[0].name == "paint");
	BOOST_CHECK_CLOSE(data.vertex_materials[0].ambient_color.r, 20.0f / 255, 0.001f);
	BOOST_CHECK_CLOSE(data.vertex_materials[0].specular_color.g, 80.0f / 255, 0.001f);
	BOOST_CHECK_CLOSE(data.vertex_materials[0].emissive_color.b, 160.0f / 255, 0.001f);
	BOOST_CHECK(data.textures[0] == "paint.tga");
	BOOST_CHECK(data.passes[0].vertex_material_index == 0);
	BOOST_CHECK(data.passes[0].texture_index == 0);
}

BOOST_AUTO_TEST_CASE(material_parser_rejects_inconsistent_material_counts)
{
	std::vector<Byte> malformed = Make_Material_Data();
	// Corrupt the material-info pass count while keeping its chunk bounded.
	malformed[8] = Byte{2};
	Assets::W3D::W3DMaterialData data;
	BOOST_CHECK(!Assets::W3D::W3DParse_Materials(malformed, 3, data));
}
