module;

#define BOOST_TEST_MODULE GeneralsAssetsMaterialTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.MaterialAsset;

import Assets.Handles;
import Assets.Identity;
import Assets.Materials;

BOOST_AUTO_TEST_CASE(material_runtime_data_keeps_typed_texture_dependencies)
{
	const Assets::TextureAssetHandle primary(2, 1);
	const Assets::TextureAssetHandle secondary(3, 1);
	const Assets::MaterialAsset material(
		{Assets::AssetType::Material, "materials/paint"},
		{"paint", "textures/paint.tga", "textures/mask.tga"},
		primary,
		secondary);

	BOOST_CHECK(material.Name() == "paint");
	BOOST_CHECK(material.Primary_Texture() == primary);
	BOOST_CHECK(material.Secondary_Texture() == secondary);
}
