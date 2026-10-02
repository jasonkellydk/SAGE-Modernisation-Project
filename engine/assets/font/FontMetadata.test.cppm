module;
#define BOOST_TEST_MODULE FontMetadataTests
#include <boost/test/included/unit_test.hpp>
export module Assets.Fonts.Metadata.Tests;
import std;
import Assets.Fonts.Metadata;
namespace {
std::vector<std::byte> Fixture() {
    std::vector<std::byte> bytes(28+18+8);
    const auto u16=[&](unsigned at,unsigned value) {bytes[at]=std::byte(value>>8);bytes[at+1]=std::byte(value&255);};
    const auto u32=[&](unsigned at,unsigned value) {u16(at,value>>16);u16(at+2,value&65535);};
    u32(0,0x00010000);u16(4,1);u32(12,0x6e616d65);u32(20,28);u32(24,26);
    u16(30,1);u16(32,18);u16(34,3);u16(36,1);u16(38,0x409);u16(40,1);u16(42,8);
    u16(46,'T');u16(48,'e');u16(50,'s');u16(52,'t');return bytes;
}
}
BOOST_AUTO_TEST_CASE(font_family_is_read_without_native_registration_and_all_truncations_fail) {
    const auto bytes=Fixture();const auto families=Assets::Font_Families(bytes);BOOST_REQUIRE(families);
    BOOST_TEST(families->size()==1);BOOST_TEST(families->front()=="Test");
    for(unsigned size=0;size<bytes.size();++size) BOOST_CHECK(!Assets::Font_Families(std::span(bytes).first(size)));
    auto damaged=bytes;damaged[44]=std::byte{0xff};BOOST_CHECK(!Assets::Font_Families(damaged));
    damaged=bytes;damaged[46]=std::byte{0xdc};BOOST_CHECK(!Assets::Font_Families(damaged));
}
