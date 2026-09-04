module;

#define BOOST_TEST_MODULE GeneralsAssetsModelTests

#include <boost/test/included/unit_test.hpp>

#include <utility>

export module Assets.Tests.ModelAsset;

import Assets.Identity;
import Assets.Models;

BOOST_AUTO_TEST_CASE(model_runtime_is_immutable_and_copies_generic_description)
{
	Assets::ModelAssetDesc description;
	description.name = "tank";
	description.source_format = "test";
	description.bounds = {{0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f}};
	description.vertices.push_back({{1.0f, 2.0f, 3.0f}});
	description.indices = {0, 0, 0};
	description.submeshes.push_back({0, 3, 0, "body"});
	description.materials.push_back({"paint", "Textures\\Paint.TGA"});
	description.dependencies.push_back({Assets::AssetType::Texture, "Textures\\Paint.TGA"});

	const Assets::ModelAsset runtime(
		{Assets::AssetType::Model, "models/tank.w3d"},
		std::move(description));

	BOOST_CHECK(runtime.Name() == "tank");
	BOOST_CHECK(runtime.Source_Format() == "test");
	BOOST_CHECK(runtime.Vertices().size() == 1);
	BOOST_CHECK(runtime.Indices().size() == 3);
	BOOST_CHECK(runtime.Submeshes()[0].material_index == 0);
	BOOST_CHECK(runtime.Materials()[0].primary_texture.canonical_name == "textures/paint.tga");
	BOOST_CHECK(runtime.Dependencies()[0].identity.canonical_name == "textures/paint.tga");
	BOOST_CHECK(runtime.Bounds().Is_Valid());
}
