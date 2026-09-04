module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DMeshTests

#include <boost/test/included/unit_test.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Assets.Tests.W3DMeshParser;

import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Mesh;

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

BOOST_AUTO_TEST_CASE(mesh_parser_rejects_out_of_range_indices)
{
	Assets::W3D::W3DParsedMesh mesh;
	std::string error;
	BOOST_CHECK(!Assets::W3D::W3DParse_Mesh(Make_Mesh(true), mesh, error));
	BOOST_CHECK(!error.empty());
}
