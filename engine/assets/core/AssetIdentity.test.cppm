module;

#define BOOST_TEST_MODULE GeneralsAssetsIdentityTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.AssetIdentity;

import Assets.Identity;

BOOST_AUTO_TEST_CASE(asset_identity_is_canonical_and_typed)
{
	BOOST_CHECK(Assets::Canonicalize_Asset_Name("Models\\./UNIT/../Tank.W3D") == "models/tank.w3d");
	BOOST_CHECK(Assets::Canonicalize_Asset_Name("//Models//Tank.W3D//") == "models/tank.w3d");

	const Assets::AssetIdentity model{Assets::AssetType::Model, "models/tank.w3d"};
	const Assets::AssetIdentity texture{Assets::AssetType::Texture, "models/tank.w3d"};
	BOOST_CHECK(model != texture);
}
