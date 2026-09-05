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

BOOST_AUTO_TEST_CASE(material_runtime_retains_independent_lighting_colors)
{
	Assets::MaterialAssetDesc description;
	description.name = "paint";
	description.base_color = {0.1f, 0.2f, 0.3f, 1};
	description.ambient_color = {0.2f, 0.3f, 0.4f, 1};
	description.specular_color = {0.3f, 0.4f, 0.5f, 1};
	description.emissive_color = {0.4f, 0.5f, 0.6f, 1};
	const Assets::MaterialAsset material({Assets::AssetType::Material, "paint"}, description, {}, {});
	BOOST_CHECK_EQUAL(material.Base_Color().r, 0.1f);
	BOOST_CHECK_EQUAL(material.Ambient_Color().g, 0.3f);
	BOOST_CHECK_EQUAL(material.Specular_Color().b, 0.5f);
	BOOST_CHECK_EQUAL(material.Emissive_Color().r, 0.4f);
}
