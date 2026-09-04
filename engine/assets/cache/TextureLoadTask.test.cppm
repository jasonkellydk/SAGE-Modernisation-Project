module;

#define BOOST_TEST_MODULE GeneralsAssetsTextureLoadTaskTests

#include <boost/test/included/unit_test.hpp>

#include <cstddef>
#include <vector>

export module Assets.Tests.TextureLoadTask;

import Assets.Cache.TextureLoadTask;
import Assets.Identity;
import Assets.States;

BOOST_AUTO_TEST_CASE(texture_load_task_builds_immutable_runtime_metadata)
{
	const Assets::AssetIdentity identity{Assets::AssetType::Texture, "textures/paint.tga"};
	const Assets::TextureLoadResult result = Assets::Load_Texture_Asset(
		identity,
		[](const Assets::AssetIdentity &) { return std::vector<std::byte>{std::byte{1}, std::byte{2}}; });

	BOOST_REQUIRE(result.Succeeded());
	BOOST_REQUIRE(result.asset != nullptr);
	BOOST_CHECK(result.asset->Identity() == identity);
	BOOST_CHECK(result.asset->Source_Format() == "tga");
	BOOST_CHECK(result.asset->Source_Size() == 2);
}
