module;
#define BOOST_TEST_MODULE W3DPassBindingsTests
#include <boost/test/included/unit_test.hpp>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>
export module Assets.Adapters.W3D.PassBindings.Tests;
import Assets.Adapters.W3D.PassBindings;
namespace
{
using Bytes = std::vector<std::byte>;
void U32(Bytes& bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift != 32; shift += 8)
        bytes.push_back(std::byte((value >> shift) & 255));
}
Bytes Ids(std::initializer_list<std::uint32_t> values)
{
    Bytes bytes;
    for (const auto value : values) U32(bytes, value);
    return bytes;
}
void Chunk(Bytes& bytes, std::uint32_t kind, const Bytes& payload, bool children = false)
{
    U32(bytes, kind); U32(bytes, static_cast<std::uint32_t>(payload.size()) | (children ? 0x80000000u : 0));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}
}

BOOST_AUTO_TEST_CASE(retains_per_vertex_per_face_and_multiple_stage_bindings)
{
    Bytes bytes, stage;
    Chunk(bytes, 0x39, Ids({3, 7, 3, 9}));
    Chunk(bytes, 0x3A, Ids({2, 5}));
    Chunk(bytes, 0x3B, Ids({0x80402010, 0xFFFFFFFF, 0x10203040, 0xFF000000}));
    Chunk(bytes, 0x3C, Ids({0x00402010, 0, 0, 0}));
    Chunk(bytes, 0x3E, Ids({0x00402010, 0, 0, 0}));
    Chunk(stage, 0x49, Ids({6, 8}));
    Bytes coords;
    for (const float value : {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f})
        U32(coords, std::bit_cast<std::uint32_t>(value));
    Chunk(stage, 0x4A, coords);
    Chunk(stage, 0x4B, Ids({0, 1, 2, 2, 1, 0}));
    Chunk(bytes, 0x48, stage, true);
    Bytes second;
    Chunk(second, 0x49, Ids({12}));
    Chunk(bytes, 0x48, second, true);
    Assets::W3D::W3DPassBindings bindings;
    BOOST_REQUIRE(Assets::W3D::W3DRead_Pass_Bindings(bytes, 4, 2, bindings));
    BOOST_REQUIRE_EQUAL(bindings.vertex_material_ids.size(), 4);
    BOOST_CHECK_EQUAL(bindings.vertex_material_ids[3], 9);
    BOOST_CHECK_EQUAL(bindings.shader_ids[1], 5);
    BOOST_REQUIRE_EQUAL(bindings.stages.size(), 2);
    BOOST_CHECK_EQUAL(bindings.stages[0].texture_ids[1], 8);
    BOOST_CHECK_EQUAL(bindings.stages[1].texture_ids[0], 12);
    BOOST_CHECK_EQUAL(bindings.stages[0].face_texcoord_ids[1][0], 2);
    BOOST_CHECK_EQUAL(bindings.stages[0].texcoords[0].y, 0.0f);
    BOOST_CHECK_CLOSE(bindings.diffuse_colors[0].a, 128.0f / 255, 0.001f);
    BOOST_CHECK_EQUAL(bindings.diffuse_illumination[0].a, 1.0f);
    BOOST_CHECK_CLOSE(bindings.specular_colors[0].r, 16.0f / 255, 0.001f);
}

BOOST_AUTO_TEST_CASE(rejects_bad_counts_and_uv_indices_without_partial_publication)
{
    Assets::W3D::W3DPassBindings bindings;
    Bytes bytes;
    Chunk(bytes, 0x39, Ids({1, 2}));
    BOOST_CHECK(!Assets::W3D::W3DRead_Pass_Bindings(bytes, 4, 1, bindings));
    BOOST_CHECK(bindings.vertex_material_ids.empty());
    bytes.clear();
    Bytes stage;
    Chunk(stage, 0x4A, Ids({0, 0}));
    Chunk(stage, 0x4B, Ids({0, 1, 0}));
    Chunk(bytes, 0x48, stage, true);
    BOOST_CHECK(!Assets::W3D::W3DRead_Pass_Bindings(bytes, 4, 1, bindings));
    BOOST_CHECK(bindings.stages.empty());
    bytes.clear();
    Chunk(bytes, 0x39, Ids({1}));
    Chunk(bytes, 0x39, Ids({2}));
    BOOST_CHECK(!Assets::W3D::W3DRead_Pass_Bindings(bytes, 4, 1, bindings));
}

BOOST_AUTO_TEST_CASE(normalizes_nonfinite_uv_entries_found_in_shipped_models)
{
    Bytes stage, bytes;
    Chunk(stage, 0x4A, Ids({0x7FC00000, 0x7F800000}));
    Chunk(bytes, 0x48, stage, true);
    Assets::W3D::W3DPassBindings bindings;
    BOOST_REQUIRE(Assets::W3D::W3DRead_Pass_Bindings(bytes, 1, 1, bindings));
    BOOST_CHECK_EQUAL(bindings.stages[0].texcoords[0].x, 0.0f);
    BOOST_CHECK_EQUAL(bindings.stages[0].texcoords[0].y, 0.0f);
}
