module;
#define BOOST_TEST_MODULE VideoMemorySource
#include <boost/test/included/unit_test.hpp>
export module Video.Adapters.MemorySource.Tests;
import std;
import Video.Adapters.MemorySource;
import Video.Decoder;
BOOST_AUTO_TEST_CASE(owned_video_source_reads_partial_blocks_seeks_all_origins_and_preserves_position_on_failure) {
    using namespace Engine::Video;
    MemorySource source({std::byte{1},std::byte{2},std::byte{3},std::byte{4}});
    BOOST_TEST(source.Size()==4);std::array<std::byte,3> output{};
    BOOST_TEST(source.Read(output)==3u);BOOST_TEST(std::to_integer<int>(output[2])==3);BOOST_TEST(source.Tell()==3);
    BOOST_TEST(source.Read(output)==1u);BOOST_TEST(std::to_integer<int>(output[0])==4);BOOST_TEST(source.Read(output)==0u);
    BOOST_REQUIRE(source.Seek(-2,SourceSeekOrigin::End));BOOST_TEST(source.Tell()==2);
    BOOST_REQUIRE(source.Seek(-1,SourceSeekOrigin::Current));BOOST_TEST(source.Tell()==1);
    BOOST_TEST(source.Read(output)==3u);BOOST_TEST(std::to_integer<int>(output[0])==2);
    BOOST_TEST(!source.Seek((std::numeric_limits<std::int64_t>::min)(),SourceSeekOrigin::Current));BOOST_TEST(source.Tell()==4);
    BOOST_TEST(!source.Seek((std::numeric_limits<std::int64_t>::max)(),SourceSeekOrigin::End));BOOST_TEST(source.Tell()==4);
    BOOST_REQUIRE(source.Seek(0,SourceSeekOrigin::Begin));BOOST_TEST(source.Read({})==0u);BOOST_TEST(source.Tell()==0);
    MemorySource empty({});BOOST_TEST(empty.Read(output)==0u);BOOST_REQUIRE(empty.Seek(0,SourceSeekOrigin::End));
    BOOST_TEST(!empty.Seek(1,SourceSeekOrigin::Begin));
}
