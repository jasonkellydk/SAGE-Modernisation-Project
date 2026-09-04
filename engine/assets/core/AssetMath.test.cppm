module;

#define BOOST_TEST_MODULE GeneralsAssetsMathTests

#include <boost/test/included/unit_test.hpp>

export module Assets.Tests.AssetMath;

import Assets.Math;

BOOST_AUTO_TEST_CASE(bounds_validate_finite_ordered_extents)
{
	const Assets::Bounds3f valid{{0.0f, 1.0f, 2.0f}, {3.0f, 4.0f, 5.0f}};
	BOOST_CHECK(valid.Is_Valid());

	const Assets::Bounds3f reversed{{3.0f, 1.0f, 2.0f}, {0.0f, 4.0f, 5.0f}};
	BOOST_CHECK(!reversed.Is_Valid());
}
