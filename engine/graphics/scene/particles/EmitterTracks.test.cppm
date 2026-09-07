module;
#define BOOST_TEST_MODULE EmitterTrackTests
#include <boost/test/included/unit_test.hpp>
#include <array>
export module Graphics.Scene.Particles.EmitterTracks.Tests;
import Graphics.Scene.Particles.EmitterTracks;
import Assets.Particles;

BOOST_AUTO_TEST_CASE(prepared_tracks_preserve_authored_times_values_and_random_ranges_after_source_release)
{
    Assets::EmitterFloatTrack source;
    source.start=3; source.random=.5f;
    source.keys={{.125f,7},{2.75f,-4}};
    Graphics::EmitterTrackValues<float> prepared(source,[](float value){return value;});
    source={};
    BOOST_CHECK_EQUAL(prepared.start,3);
    BOOST_CHECK_EQUAL(prepared.random,.5f);
    BOOST_REQUIRE_EQUAL(prepared.times.size(),2);
    BOOST_REQUIRE_EQUAL(prepared.values.size(),2);
    BOOST_CHECK_EQUAL(prepared.times[0],.125f);
    BOOST_CHECK_EQUAL(prepared.times[1],2.75f);
    BOOST_CHECK_EQUAL(prepared.values[0],7);
    BOOST_CHECK_EQUAL(prepared.values[1],-4);
    auto clone=prepared;
    prepared.values[0]=9;
    BOOST_CHECK_EQUAL(clone.values[0],7);
    Graphics::EmitterTrackValues<float> constant(source,[](float value){return value;});
    BOOST_CHECK(constant.times.empty());
    BOOST_CHECK(constant.values.empty());
}

BOOST_AUTO_TEST_CASE(color_projection_keeps_rgb_independent_of_opacity)
{
    Assets::EmitterColorTrack source;
    source.start={.25f,.5f,1,0}; source.random={.1f,.2f,.3f,1};
    source.keys={{1,{1,.5f,.25f,.75f}}};
    Graphics::EmitterTrackValues<std::array<float,3>> prepared(source,[](const auto& color){
        return std::array{color.r,color.g,color.b};
    });
    source={};
    BOOST_CHECK_EQUAL(prepared.start[0],.25f);
    BOOST_CHECK_EQUAL(prepared.start[2],1);
    BOOST_CHECK_EQUAL(prepared.random[2],.3f);
    BOOST_REQUIRE_EQUAL(prepared.values.size(),1);
    BOOST_CHECK_EQUAL(prepared.values[0][2],.25f);
}
