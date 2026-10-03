module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DChunkTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.W3DChunkReader;
import std;

import Assets.Adapters.W3D.Chunks;

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

}

BOOST_AUTO_TEST_CASE(chunk_reader_rejects_truncated_headers_and_payloads)
{
	const std::vector<Byte> truncated_header(7, Byte{0});
	BOOST_CHECK(!Assets::W3D::W3DValidate_Chunk_Tree(truncated_header));

	std::vector<Byte> truncated_payload;
	Append_U32(truncated_payload, Assets::W3D::W3DChunkMesh);
	Append_U32(truncated_payload, 16);
	truncated_payload.push_back(Byte{0});
	BOOST_CHECK(!Assets::W3D::W3DValidate_Chunk_Tree(truncated_payload));
}

BOOST_AUTO_TEST_CASE(microchunk_envelope_preserves_repeated_fields_and_rejects_truncation)
{
	using namespace Assets::W3D;
	std::vector<Byte> bytes{Byte{1}, Byte{0}, Byte{1}, Byte{255}};
	bytes.insert(bytes.end(), 255, Byte{42});
	std::vector<std::pair<unsigned, std::size_t>> visits;
	BOOST_REQUIRE(W3DVisit_MicroChunks(bytes, [&](const W3DMicroChunkView &chunk) {
		visits.emplace_back(chunk.id, chunk.payload.size()); return true;
	}));
	BOOST_REQUIRE_EQUAL(visits.size(), 2u); BOOST_TEST(visits[0].second == 0u);
	BOOST_TEST(visits[1].first == 1u); BOOST_TEST(visits[1].second == 255u);
	bytes.pop_back(); BOOST_TEST(!W3DVisit_MicroChunks(bytes, [](const auto &) { return true; }));
	BOOST_TEST(!W3DVisit_MicroChunks(std::array{Byte{1}}, [](const auto &) { return true; }));
	BOOST_TEST(!W3DVisit_MicroChunks(std::array{Byte{1}, Byte{0}}, [](const auto &) { return false; }));
}

BOOST_AUTO_TEST_CASE(chunk_reader_visits_bounded_nested_chunks)
{
	std::vector<Byte> nested;
	Append_U32(nested, 7);
	Append_U32(nested, 1);
	nested.push_back(Byte{0x2A});

	std::vector<Byte> root;
	Append_U32(root, Assets::W3D::W3DChunkMesh);
	Append_U32(root, static_cast<std::uint32_t>(nested.size()) | Assets::W3D::W3DChunkContainsChildren);
	root.insert(root.end(), nested.begin(), nested.end());

	std::uint32_t visited_id = 0;
	BOOST_CHECK(Assets::W3D::W3DValidate_Chunk_Tree(root));
	BOOST_CHECK(Assets::W3D::W3DVisit_Chunks(root, [&visited_id](const Assets::W3D::W3DChunkView &chunk) {
		visited_id = chunk.id;
		return true;
	}));
	BOOST_CHECK(visited_id == Assets::W3D::W3DChunkMesh);
}
