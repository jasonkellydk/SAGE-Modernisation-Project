module;
#define BOOST_TEST_MODULE AniCursorTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Cursors.Ani.Tests;
import std;
import Graphics.Cursors.Ani;

namespace
{
void Put16(std::vector<std::byte>& bytes, std::uint16_t value)
{
    bytes.push_back(std::byte(value & 0xff));
    bytes.push_back(std::byte(value >> 8));
}
void Put32(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned b = 0; b < 4; ++b) bytes.push_back(std::byte((value >> (b * 8)) & 0xff));
}
void Tag(std::vector<std::byte>& bytes, std::string_view name)
{
    for (const char c : name) bytes.push_back(std::byte(c));
}

// A 1-bpp .CUR of `size`x`size`: palette {black, white}, every pixel `white`, the left half masked out.
std::vector<std::byte> Cur(int size, bool white, int hot_x, int hot_y)
{
    const std::size_t xor_stride = ((size + 31) / 32) * 4, mask_stride = xor_stride;
    std::vector<std::byte> dib;
    Put32(dib, 40); Put32(dib, static_cast<std::uint32_t>(size)); Put32(dib, static_cast<std::uint32_t>(size * 2));
    Put16(dib, 1); Put16(dib, 1); Put32(dib, 0); Put32(dib, 0); Put32(dib, 0); Put32(dib, 0); Put32(dib, 2); Put32(dib, 0);
    Put32(dib, 0x00000000); Put32(dib, 0x00ffffff);
    for (int y = 0; y < size; ++y)
        for (std::size_t x = 0; x < xor_stride; ++x) dib.push_back(white ? std::byte{0xff} : std::byte{0});
    for (int y = 0; y < size; ++y)
        for (std::size_t x = 0; x < mask_stride; ++x)
        {
            unsigned bits = 0;
            for (int bit = 0; bit < 8; ++bit)
                if (static_cast<int>(x) * 8 + bit < size / 2) bits |= 0x80u >> bit;
            dib.push_back(std::byte(bits));
        }
    std::vector<std::byte> cur;
    Put16(cur, 0); Put16(cur, 2); Put16(cur, 1);
    cur.push_back(std::byte(size)); cur.push_back(std::byte(size)); cur.push_back(std::byte{0}); cur.push_back(std::byte{0});
    Put16(cur, static_cast<std::uint16_t>(hot_x)); Put16(cur, static_cast<std::uint16_t>(hot_y));
    Put32(cur, static_cast<std::uint32_t>(dib.size())); Put32(cur, 22);
    cur.insert(cur.end(), dib.begin(), dib.end());
    return cur;
}

void Chunk(std::vector<std::byte>& bytes, std::string_view name, const std::vector<std::byte>& payload)
{
    Tag(bytes, name);
    Put32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    if (payload.size() & 1) bytes.push_back(std::byte{0});
}

std::vector<std::byte> Ani(const std::vector<std::vector<std::byte>>& icons, const std::vector<std::uint32_t>& rates,
    const std::vector<std::uint32_t>& sequence)
{
    std::vector<std::byte> body;
    Tag(body, "ACON");
    std::vector<std::byte> header(36, std::byte{0});
    Chunk(body, "anih", header);
    if (!rates.empty())
    {
        std::vector<std::byte> payload;
        for (const auto rate : rates) Put32(payload, rate);
        Chunk(body, "rate", payload);
    }
    if (!sequence.empty())
    {
        std::vector<std::byte> payload;
        for (const auto index : sequence) Put32(payload, index);
        Chunk(body, "seq ", payload);
    }
    std::vector<std::byte> list;
    Tag(list, "fram");
    for (const auto& icon : icons) Chunk(list, "icon", icon);
    Chunk(body, "LIST", list);
    std::vector<std::byte> riff;
    Tag(riff, "RIFF");
    Put32(riff, static_cast<std::uint32_t>(body.size()));
    riff.insert(riff.end(), body.begin(), body.end());
    return riff;
}

unsigned Pixel(const Assets::PreparedImage& image, unsigned x, unsigned y, unsigned channel)
{
    return std::to_integer<unsigned>(image.bytes[y * image.row_pitch + x * 4 + channel]);
}
}

// Legacy: Core/GameEngineDevice/Source/SDL3Device/GameClient/SDL3Mouse.cpp parseIconSurface, orderAniFrames,
// normalizeAniFrames, aniFrameDuration and the hotspot choice in loadAniCursor (standing in for Win32
// LoadCursorFromFile, which WinCursors mode uses).
BOOST_AUTO_TEST_CASE(an_ani_plays_its_frames_in_sequence_order_at_its_rates_with_the_first_hotspot)
{
    const auto bytes = Ani({Cur(32, true, 3, 5), Cur(16, false, 9, 9)}, {6, 12, 1}, {1, 0, 1});
    const auto cursor = Graphics::Read_Ani_Cursor(bytes, 0, 0, 0);
    BOOST_REQUIRE(cursor);
    BOOST_TEST(cursor->frames.size() == 3u);
    for (const auto& frame : cursor->frames)
    {
        BOOST_TEST(frame.width == 16u); // all at the first frame's size in sequence order (the 32x32 one scaled down)
        BOOST_TEST(frame.height == 16u);
    }
    // Sequence 1, 0, 1: black, white, black; the masked left half is transparent.
    BOOST_TEST(Pixel(cursor->frames[0], 12, 4, 0) == 0u);
    BOOST_TEST(Pixel(cursor->frames[2], 12, 4, 0) == 0u);
    BOOST_TEST(Pixel(cursor->frames[1], 12, 4, 0) == 255u);
    BOOST_TEST(Pixel(cursor->frames[1], 12, 4, 3) == 255u);
    BOOST_TEST(Pixel(cursor->frames[1], 4, 4, 3) == 0u);
    // Jiffies: 6 -> 100 ms, 12 -> 200 ms, 1 -> 17 ms (rounded).
    BOOST_TEST(cursor->durations_ms == (std::vector<std::uint32_t>{100, 200, 17}), boost::test_tools::per_element());
    // The first parsed frame's hotspot.
    BOOST_TEST(cursor->hotspot_x == 3);
    BOOST_TEST(cursor->hotspot_y == 5);
}

BOOST_AUTO_TEST_CASE(an_ani_without_rates_uses_the_ini_fps_or_fifty_ms)
{
    const auto bytes = Ani({Cur(32, true, 0, 0), Cur(32, false, 0, 0)}, {}, {});
    const auto at_ten = Graphics::Read_Ani_Cursor(bytes, 10, 0, 0);
    BOOST_REQUIRE(at_ten);
    BOOST_TEST(at_ten->durations_ms == (std::vector<std::uint32_t>{100, 100}), boost::test_tools::per_element());
    const auto without = Graphics::Read_Ani_Cursor(bytes, 0, 0, 0);
    BOOST_REQUIRE(without);
    BOOST_TEST(without->durations_ms == (std::vector<std::uint32_t>{50, 50}), boost::test_tools::per_element());
    const auto single = Graphics::Read_Ani_Cursor(Ani({Cur(32, true, 1, 2)}, {}, {}), 10, 0, 0);
    BOOST_REQUIRE(single);
    BOOST_TEST(single->durations_ms == (std::vector<std::uint32_t>{0}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(something_that_is_not_an_ani_is_refused)
{
    std::vector<std::byte> junk(64, std::byte{7});
    BOOST_TEST(!Graphics::Read_Ani_Cursor(junk, 0, 0, 0));
    auto truncated = Ani({Cur(32, true, 0, 0)}, {}, {});
    truncated.resize(truncated.size() - 40);
    BOOST_TEST(!Graphics::Read_Ani_Cursor(truncated, 0, 0, 0));
}

// Every cursor the game ships (loose Data/Cursors/*.ANI in an install) reads.
BOOST_AUTO_TEST_CASE(the_shipped_cursors_read)
{
    const char* install = std::getenv("GENERALSZH_INSTALL_DIR");
    if (install == nullptr || !std::filesystem::exists(std::filesystem::path(install) / "Data" / "Cursors"))
    {
        BOOST_TEST_MESSAGE("SKIPPED: set GENERALSZH_INSTALL_DIR to a Zero Hour install");
        return;
    }
    std::size_t read = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(install) / "Data" / "Cursors"))
    {
        std::ifstream file(entry.path(), std::ios::binary);
        std::vector<char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto bytes = std::as_bytes(std::span(raw));
        const auto cursor = Graphics::Read_Ani_Cursor(bytes, 20, 16, 16);
        BOOST_TEST(cursor.has_value(), entry.path().filename().string());
        if (cursor)
        {
            BOOST_TEST(cursor->frames.front().width > 0u);
            ++read;
        }
    }
    BOOST_TEST(read > 0u);
}
