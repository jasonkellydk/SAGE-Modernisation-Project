module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DMaterialTests

#include <boost/test/included/unit_test.hpp>

#include <array>
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
	std::vector<Byte> arguments0, arguments1;
	Append_String(arguments0, "UPerSec=0.25\nVPerSec=-0.5");
	Append_String(arguments1, "FPS=8\nLog2Width=2");
	Append_Chunk(vertex_material, 0x2E, arguments0, false);
	Append_Chunk(vertex_material, 0x2F, arguments1, false);
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
	BOOST_CHECK(data.shader_materials.empty());
	BOOST_CHECK(data.vertex_materials[0].material.name == "paint");
	BOOST_CHECK_CLOSE(data.vertex_materials[0].material.ambient_color.r, 20.0f / 255, 0.001f);
	BOOST_CHECK_CLOSE(data.vertex_materials[0].material.specular_color.g, 80.0f / 255, 0.001f);
	BOOST_CHECK_CLOSE(data.vertex_materials[0].material.emissive_color.b, 160.0f / 255, 0.001f);
	BOOST_CHECK(data.vertex_materials[0].mapper_arguments[0] == "UPerSec=0.25\nVPerSec=-0.5");
	BOOST_CHECK(data.vertex_materials[0].mapper_arguments[1] == "FPS=8\nLog2Width=2");
	BOOST_CHECK(data.textures[0].name == "paint.tga");
	BOOST_CHECK(data.passes[0].vertex_material_index == 0);
	BOOST_CHECK(data.passes[0].texture_index == 0);
}

BOOST_AUTO_TEST_CASE(shader_material_extension_preserves_legacy_material_values)
{
    using namespace Assets::W3D;
    auto bytes = Make_Material_Data();
    W3DMaterialData legacy, extended;
    BOOST_REQUIRE(W3DParse_Materials(bytes, 3, legacy));
    std::vector<Byte> header(37, Byte{0}), shader, table;
    header[0] = Byte{1};
    const std::string_view shader_name = "objectsallied.fx";
    for (std::size_t i = 0; i < shader_name.size(); ++i) header[1+i] = static_cast<Byte>(shader_name[i]);
    Append_Chunk(shader, W3DChunkShaderMaterialHeader, header, false);
    Append_Chunk(table, W3DChunkShaderMaterial, shader, true);
    Append_Chunk(bytes, W3DChunkShaderMaterials, table, true);
    BOOST_REQUIRE(W3DParse_Materials(bytes, 3, extended));
    BOOST_REQUIRE_EQUAL(extended.shader_materials.size(), 1u);
    BOOST_TEST(extended.shader_materials[0].shader_name == "objectsallied.fx");
    const auto &before = legacy.vertex_materials[0].material;
    const auto &after = extended.vertex_materials[0].material;
    BOOST_TEST(after.name == before.name);
    BOOST_TEST(after.base_color.r == before.base_color.r);
    BOOST_TEST(after.ambient_color.r == before.ambient_color.r);
    BOOST_TEST(after.specular_color.g == before.specular_color.g);
    BOOST_TEST(after.emissive_color.b == before.emissive_color.b);
    BOOST_TEST(after.opacity == before.opacity);
    BOOST_TEST(after.shininess == before.shininess);
    BOOST_TEST(extended.passes[0].texture_index == legacy.passes[0].texture_index);
    BOOST_TEST(extended.textures[0].name == legacy.textures[0].name);
    Append_Chunk(bytes, W3DChunkShaderMaterials, table, true);
    BOOST_CHECK(!W3DParse_Materials(bytes, 3, extended));
}

BOOST_AUTO_TEST_CASE(material_parser_rejects_inconsistent_material_counts)
{
	std::vector<Byte> malformed = Make_Material_Data();
	// Corrupt the material-info pass count while keeping its chunk bounded.
	malformed[8] = Byte{2};
	Assets::W3D::W3DMaterialData data;
	BOOST_CHECK(!Assets::W3D::W3DParse_Materials(malformed, 3, data));
}

BOOST_AUTO_TEST_CASE(vertex_material_decoder_bounds_strings_and_accepts_unnamed_materials)
{
	using namespace Assets::W3D;
	std::vector<Byte> info(32, Byte{0}), bytes, arguments;
	info[0] = Byte{0xA5};
	Write_F32(info, 20, 24.0f);
	Write_F32(info, 24, 0.5f);
	Write_F32(info, 28, 0.25f);
	Append_Chunk(bytes, W3DChunkVertexMaterialInfo, info, false);
	W3DVertexMaterialData decoded;
	BOOST_REQUIRE(W3DRead_Vertex_Material(bytes, decoded));
	BOOST_CHECK(!decoded.has_name);
	BOOST_CHECK_EQUAL(decoded.material.source_attributes, 0xA5u);
	BOOST_CHECK_EQUAL(decoded.material.shininess, 24.0f);
	BOOST_CHECK_EQUAL(decoded.material.opacity, 0.5f);
	BOOST_CHECK_EQUAL(decoded.material.translucency, 0.25f);
	// A bounded chunk need not end in NUL; consumers receive an owned string.
	const std::string long_name(400, 'x');
	std::vector<Byte> name(long_name.size(), Byte{'x'});
	Append_Chunk(bytes, W3DChunkVertexMaterialName, name, false);
	Append_String(arguments, "UPerSec=2");
	arguments.push_back(Byte{'z'});
	Append_Chunk(bytes, 0x2E, arguments, false);
	BOOST_REQUIRE(W3DRead_Vertex_Material(bytes, decoded));
	BOOST_CHECK(decoded.has_name);
	BOOST_CHECK(decoded.material.name == long_name);
	BOOST_CHECK(decoded.mapper_arguments[0] == "UPerSec=2");
	BOOST_CHECK(decoded.mapper_arguments[1].empty());
	Append_Chunk(bytes, 0x2E, arguments, false);
	BOOST_CHECK(!W3DRead_Vertex_Material(bytes, decoded));
	BOOST_CHECK(!decoded.has_name);
	BOOST_CHECK(decoded.mapper_arguments[0].empty());
}

BOOST_AUTO_TEST_CASE(vertex_material_decoder_rejects_missing_truncated_and_nonfinite_information)
{
	using namespace Assets::W3D;
	W3DVertexMaterialData decoded;
	std::vector<Byte> bytes, name, info(31, Byte{0});
	Append_String(name, "paint");
	Append_Chunk(bytes, W3DChunkVertexMaterialName, name, false);
	BOOST_CHECK(!W3DRead_Vertex_Material(bytes, decoded));
	Append_Chunk(bytes, W3DChunkVertexMaterialInfo, info, false);
	BOOST_CHECK(!W3DRead_Vertex_Material(bytes, decoded));
	bytes.clear();
	info.resize(32);
	Write_F32(info, 24, std::bit_cast<float>(0x7FC00000u));
	Append_Chunk(bytes, W3DChunkVertexMaterialInfo, info, false);
	BOOST_CHECK(!W3DRead_Vertex_Material(bytes, decoded));
	BOOST_CHECK(!decoded.has_name);
}

BOOST_AUTO_TEST_CASE(texture_decoder_preserves_sampler_and_animation_metadata)
{
	using namespace Assets::W3D;
	std::vector<Byte> bytes, name, info;
	Append_String(name, "animated.tga");
	Append_U32(info, 0x00031234u);
	Append_U32(info, 8);
	Append_F32(info, 12.5f);
	Append_Chunk(bytes, 0x33, info, false);
	Append_Chunk(bytes, W3DChunkTextureName, name, false);
	W3DTextureData decoded;
	BOOST_REQUIRE(W3DRead_Texture(bytes,decoded));
	BOOST_CHECK(decoded.name == "animated.tga");
	BOOST_CHECK(decoded.has_info);
	BOOST_CHECK_EQUAL(decoded.attributes, 0x1234u);
	BOOST_CHECK_EQUAL(decoded.animation_type, 3u);
	BOOST_CHECK_EQUAL(decoded.frame_count, 8u);
	BOOST_CHECK_EQUAL(decoded.frame_rate, 12.5f);
	Append_Chunk(bytes, 0x33, info, false);
	BOOST_CHECK(!W3DRead_Texture(bytes,decoded));
	BOOST_CHECK(decoded.name.empty());
	BOOST_CHECK(!decoded.has_info);
	bytes.clear();
	Append_Chunk(bytes, W3DChunkTextureName, name, false);
	BOOST_REQUIRE(W3DRead_Texture(bytes,decoded));
	BOOST_CHECK(!decoded.has_info);
	info.resize(11);
	Append_Chunk(bytes, 0x33, info, false);
	BOOST_CHECK(!W3DRead_Texture(bytes,decoded));
}

BOOST_AUTO_TEST_CASE(shader_decoder_preserves_all_authored_fields_and_record_order)
{
	using namespace Assets::W3D;
	std::vector<Byte> bytes;
	for (unsigned record = 0; record < 2; ++record)
		for (unsigned field = 0; field < 16; ++field)
			bytes.push_back(Byte(record * 16 + field));
	std::vector<W3DShaderSettings> decoded;
	BOOST_REQUIRE(W3DRead_Shaders(bytes,decoded));
	BOOST_REQUIRE_EQUAL(decoded.size(), 2u);
	W3DShaderSettings single;
	BOOST_REQUIRE(W3DRead_Shader(W3DByteSpan(bytes).first(16), single));
	BOOST_CHECK_EQUAL(single.detail_color, decoded[0].detail_color);
	BOOST_CHECK(!W3DRead_Shader(bytes, single));
	BOOST_CHECK_EQUAL(single.detail_color, 0u);
	BOOST_CHECK(!W3DRead_Shader(W3DByteSpan(bytes).first(15), single));
	for (unsigned record = 0; record < 2; ++record) {
		const auto &shader = decoded[record];
		const std::array<std::uint8_t,15> fields{shader.depth_compare,shader.depth_mask,shader.color_mask,
			shader.destination_blend,shader.fog_function,shader.primary_gradient,shader.secondary_gradient,
			shader.source_blend,shader.texturing,shader.detail_color,shader.detail_alpha,shader.shader_preset,
			shader.alpha_test,shader.post_detail_color,shader.post_detail_alpha};
		for (unsigned field = 0; field < fields.size(); ++field)
			BOOST_CHECK_EQUAL(fields[field], record * 16 + field);
	}
	bytes.pop_back();
	BOOST_CHECK(!W3DRead_Shaders(bytes,decoded));
	BOOST_CHECK(decoded.empty());
}
