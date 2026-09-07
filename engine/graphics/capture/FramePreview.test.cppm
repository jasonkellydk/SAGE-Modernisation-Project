module;
#define BOOST_TEST_MODULE FramePreviewTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
export module Graphics.Capture.FramePreview.Tests;
import Graphics.Capture.FramePreview;
import Graphics.FrameTargets;
import Graphics.Backends.DX11;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(preview_preserves_channels_orientation_throttle_and_resize)
{
    DX11Device device({true});
    FramePreview preview;
    BOOST_REQUIRE(preview.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    auto& commands = device.Immediate_Command_List();
    std::uint32_t time = 100;
    for (const auto format : {RHITextureFormat::BGRA8_UNorm,RHITextureFormat::RGBA8_UNorm}) {
        for (const auto height : {8u,11u,32u}) {
            constexpr std::uint32_t width = 16;
            std::vector<std::byte> pixels(width*height*4);
            for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
                const unsigned red = format == RHITextureFormat::BGRA8_UNorm ? 2 : 0;
                const unsigned blue = 2-red;
                const unsigned offset = (y*width+x)*4;
                pixels[offset + (y<height/2 ? red : blue)] = std::byte{255};
                pixels[offset + 3] = std::byte{x<width/2 ? std::uint8_t{64} : std::uint8_t{192}};
            }
            const auto color = device.Create_Texture_Initialized({width,height,1,format,
                static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)}, {pixels,width*4});
            const auto depth = device.Create_Texture({width,height,1,RHITextureFormat::D24_UNorm_S8,
                static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
            BOOST_REQUIRE(color.Is_Valid()); BOOST_REQUIRE(depth.Is_Valid());
            const FrameTargets targets{{color,width,height},{depth,width,height}};
            auto frame = preview.Read(targets,format,10,time,500);
            BOOST_REQUIRE(frame.Is_Valid());
            BOOST_CHECK_EQUAL(frame.width,8u);
            BOOST_CHECK_EQUAL(frame.height,height==32 ? 8u : 4u);
            const auto corner = [&](unsigned x,unsigned y,unsigned channel) {
                return std::to_integer<unsigned>(frame.pixels[y*frame.row_pitch+x*4+channel]);
            };
            BOOST_CHECK_EQUAL(corner(0,0,0),255u);
            BOOST_CHECK_EQUAL(corner(0,0,2),0u);
            BOOST_CHECK_EQUAL(corner(7,frame.height-1,2),255u);
            BOOST_CHECK_EQUAL(corner(7,frame.height-1,0),0u);
            BOOST_CHECK_SMALL(static_cast<int>(corner(0,0,3))-64,2);
            BOOST_CHECK_SMALL(static_cast<int>(corner(7,0,3))-192,2);
            const std::vector<std::byte> first(frame.pixels.begin(),frame.pixels.end());
            // Read restores the frame attachments. A following clear must target
            // the source, leaving the returned preview untouched until refresh.
            BOOST_REQUIRE(commands.Clear({0,1,0,1},1));
            frame = preview.Read(targets,format,8,time+499,500);
            BOOST_REQUIRE(frame.Is_Valid());
            BOOST_CHECK(std::equal(frame.pixels.begin(),frame.pixels.end(),first.begin(),first.end()));
            frame = preview.Read(targets,format,8,time+500,500);
            BOOST_REQUIRE(frame.Is_Valid());
            BOOST_CHECK_EQUAL(corner(0,0,0),0u);
            BOOST_CHECK_EQUAL(corner(0,0,1),255u);
            BOOST_CHECK_EQUAL(corner(0,0,3),255u);
            device.Destroy_Texture(color); device.Destroy_Texture(depth);
            time += 501;
        }
    }
    preview.Shutdown();
    BOOST_CHECK(!preview.Read({},RHITextureFormat::RGBA8_UNorm,8,time,500).Is_Valid());
}
