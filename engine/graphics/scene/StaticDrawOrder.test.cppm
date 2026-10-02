module;
#define BOOST_TEST_MODULE StaticDrawOrderTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.StaticDrawOrder.Tests;
import std;
import Graphics.Scene.StaticDrawOrder;

BOOST_AUTO_TEST_CASE(static_layers_descend_and_equal_layers_keep_encounter_order)
{
    // Interleave ordinary submissions and several authored layers, including
    // repeat entries. Their identities are indices, not deduplicated objects.
    const std::array<std::int32_t,10> levels{0,3,1,4,3,0,5,4,1,3};
    std::vector<std::size_t> result;
    BOOST_REQUIRE(Graphics::Build_Static_Draw_Order(levels,result));
    const std::vector<std::size_t> expected{6,3,7,1,4,9,2,8};
    BOOST_CHECK(result==expected);
}

BOOST_AUTO_TEST_CASE(zero_bypasses_static_order_and_invalid_input_preserves_previous_result)
{
    std::vector<std::size_t> result{7,8};
    const std::array<std::int32_t,3> invalid{2,-1,4};
    BOOST_CHECK(!Graphics::Build_Static_Draw_Order(invalid,result));
    const std::vector<std::size_t> unchanged{7,8};
    BOOST_CHECK(result==unchanged);
    const std::array<std::int32_t,3> ordinary{0,0,0};
    BOOST_REQUIRE(Graphics::Build_Static_Draw_Order(ordinary,result));
    BOOST_CHECK(result.empty());
    const std::array<std::int32_t,2> sparse{1,std::numeric_limits<std::int32_t>::max()};
    BOOST_REQUIRE(Graphics::Build_Static_Draw_Order(sparse,result));
    const std::vector<std::size_t> descending{1,0};
    BOOST_CHECK(result==descending);
    BOOST_REQUIRE(Graphics::Build_Static_Draw_Order({},result));
    BOOST_CHECK(result.empty());
}
