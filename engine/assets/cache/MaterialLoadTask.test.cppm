module;

#define BOOST_TEST_MODULE GeneralsAssetsMaterialLoadTaskTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.MaterialLoadTask;

import Assets.Cache.MaterialLoadTask;
import Assets.Handles;
import Assets.Identity;

BOOST_AUTO_TEST_CASE(material_finalize_task_keeps_typed_texture_handles)
{
	const Assets::TextureAssetHandle primary(4, 1);
	const Assets::MaterialLoadResult result = Assets::Finalize_Material_Asset(
		{Assets::AssetType::Material, "materials/paint"},
		{"paint", "textures/paint.tga"},
		primary,
		Assets::TextureAssetHandle::Invalid());

	BOOST_REQUIRE(result.Succeeded());
	BOOST_REQUIRE(result.asset != nullptr);
	BOOST_CHECK(result.asset->Primary_Texture() == primary);
}
