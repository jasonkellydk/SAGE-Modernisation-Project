module;

#define BOOST_TEST_MODULE GeneralsAssetsW3DAdapterTests

#include <boost/test/included/unit_test.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

export module Assets.Tests.W3DAdapter;

import Assets.Adapters.W3D;
import Assets.Identity;
import Assets.Models;

namespace
{

using Byte = std::byte;

std::vector<Byte> Read_File(const std::filesystem::path &path)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return {};
	const std::vector<char> chars((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	std::vector<Byte> bytes;
	bytes.reserve(chars.size());
	for (const char character : chars)
		bytes.push_back(static_cast<Byte>(character));
	return bytes;
}

std::filesystem::path Integration_Asset_Path()
{
	if (const char *environment_path = std::getenv("GENERALS_W3D_INTEGRATION_ASSET"); environment_path != nullptr)
		return environment_path;
#ifdef ASSETS_W3D_INTEGRATION_ASSET
	if (std::filesystem::exists(ASSETS_W3D_INTEGRATION_ASSET))
		return ASSETS_W3D_INTEGRATION_ASSET;
#endif
#ifdef ASSETS_W3D_STAGED_ASSET
	if (std::filesystem::exists(ASSETS_W3D_STAGED_ASSET))
		return ASSETS_W3D_STAGED_ASSET;
#endif
	return {};
}

void Check_Deterministic(const Assets::ModelAssetDesc &left, const Assets::ModelAssetDesc &right)
{
	BOOST_CHECK(left.name == right.name);
	BOOST_CHECK(left.container_name == right.container_name);
	BOOST_CHECK(left.source_format == right.source_format);
	BOOST_CHECK(left.vertices.size() == right.vertices.size());
	BOOST_CHECK(left.indices == right.indices);
	BOOST_CHECK(left.submeshes.size() == right.submeshes.size());
	BOOST_CHECK(left.materials.size() == right.materials.size());
	BOOST_CHECK(left.dependencies.size() == right.dependencies.size());
	for (std::size_t index = 0; index < left.vertices.size() && index < right.vertices.size(); ++index) {
		BOOST_CHECK(left.vertices[index].position.x == right.vertices[index].position.x);
		BOOST_CHECK(left.vertices[index].position.y == right.vertices[index].position.y);
		BOOST_CHECK(left.vertices[index].position.z == right.vertices[index].position.z);
	}
}

}

BOOST_AUTO_TEST_CASE(real_generals_w3d_model_loads_and_is_deterministic)
{
	const std::filesystem::path path = Integration_Asset_Path();
	if (path.empty() || !std::filesystem::exists(path)) {
		BOOST_TEST_MESSAGE(
			"Skipping real-file integration check: set GENERALS_W3D_INTEGRATION_ASSET or "
			"RTS_W3D_INTEGRATION_ASSET. The default staged asset is "
			"Art/W3D/ABArFrcCmd_A2.W3D.");
		return;
	}

	const std::vector<Byte> source = Read_File(path);
	BOOST_REQUIRE(!source.empty());
	const Assets::AssetIdentity identity{
		Assets::AssetType::Model,
		Assets::Canonicalize_Asset_Name(path.string())};
	Assets::W3DAdapter adapter;
	BOOST_CHECK(adapter.Can_Import(identity, source));
	const auto first = adapter.Import(identity, source);
	const auto second = adapter.Import(identity, source);
	BOOST_REQUIRE(first.description != nullptr);
	BOOST_REQUIRE(second.description != nullptr);
	BOOST_REQUIRE(!first.description->vertices.empty());
	BOOST_REQUIRE(!first.description->indices.empty());
	BOOST_REQUIRE(!first.description->submeshes.empty());
	BOOST_REQUIRE(!first.description->materials.empty());
	BOOST_CHECK(first.description->bounds.Is_Valid());
	for (const auto &submesh : first.description->submeshes) {
		BOOST_CHECK(submesh.index_count != 0);
		BOOST_CHECK(submesh.first_index + submesh.index_count <= first.description->indices.size());
		BOOST_CHECK(submesh.material_index < first.description->materials.size());
	}
	for (const auto &material : first.description->materials)
		BOOST_CHECK(!material.name.empty());
	Check_Deterministic(*first.description, *second.description);
}
