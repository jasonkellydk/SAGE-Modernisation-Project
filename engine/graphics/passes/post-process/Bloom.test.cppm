module;
#define BOOST_TEST_MODULE BloomTests
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
export module Graphics.Passes.Bloom.Tests;
import Graphics.Passes.Bloom;
import Graphics.RHI;
import Graphics.FrameTargets;
import Graphics.Backends.DX11;
import Graphics.Capture.FrameCapture;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(bloom_curve_disabled_alpha_and_target_recreation)
{
    for (const bool warp : {true,false}) {
        DX11Device device({warp});
        BOOST_REQUIRE(device.Is_Valid());
        BloomRenderer bloom;
        FrameCapture capture;
        auto& commands = device.Immediate_Command_List();
        BOOST_CHECK(bloom.Render(commands,{},RHITextureFormat::RGBA8_UNorm,false));
        BOOST_REQUIRE(bloom.Initialize(device,std::filesystem::path(GRAPHICS_BLOOM_SHADER_DIRECTORY)));
        for (const auto width : {32u,37u,1u,128u}) {
            if (width == 128) {
                bloom.Shutdown();
                BOOST_REQUIRE(bloom.Initialize(device,std::filesystem::path(GRAPHICS_BLOOM_SHADER_DIRECTORY)));
            }
            const unsigned height = width == 1 ? 1 : width - 3;
            for (const auto format : {RHITextureFormat::RGBA8_UNorm,RHITextureFormat::BGRA8_UNorm}) {
                for (const unsigned value : {16u,128u}) {
                    std::vector<std::byte> pixels(width*height*4);
                    for (unsigned i=0;i<width*height;++i) {
                        pixels[i*4] = pixels[i*4+1] = pixels[i*4+2] = std::byte(value);
                        pixels[i*4+3] = std::byte{73};
                    }
                    const auto color = device.Create_Texture_Initialized({width,height,1,format,
                        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)}, {pixels,width*4});
                    const auto depth = device.Create_Texture({width,height,1,RHITextureFormat::D24_UNorm_S8,
                        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
                    BOOST_REQUIRE(color.Is_Valid()); BOOST_REQUIRE(depth.Is_Valid());
                    const FrameTargets targets{{color,width,height},{depth,width,height}};
                    BOOST_REQUIRE(bloom.Render(commands,targets,format,false));
                    auto frame = capture.Read(device,color,width,height,format);
                    BOOST_REQUIRE(frame.Is_Valid());
                    BOOST_CHECK(std::equal(pixels.begin(),pixels.end(),frame.pixels.begin(),frame.pixels.end()));
                    BOOST_REQUIRE(bloom.Render(commands,targets,format,true));
                    frame = capture.Read(device,color,width,height,format);
                    BOOST_REQUIRE(frame.Is_Valid());
                    // Independent reference: gray 128/255 -> luminance 0.4517647,
                    // squared excess 0.116803 -> final 144.54/255; dark 16 is below threshold.
                    const int expected = value == 16 ? 16 : 145;
                    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
                        const auto offset = y*frame.row_pitch+x*4;
                        for (unsigned channel=0;channel<3;++channel)
                            BOOST_CHECK_SMALL(static_cast<int>(std::to_integer<unsigned>(frame.pixels[offset+channel]))-expected,2);
                        BOOST_CHECK_EQUAL(std::to_integer<unsigned>(frame.pixels[offset+3]),73u);
                    }
                    // Restored attachments allow the following UI pass to draw normally.
                    BOOST_REQUIRE(commands.Clear({0,1,0,1},1));
                    frame = capture.Read(device,color,width,height,format);
                    BOOST_REQUIRE(frame.Is_Valid());
                    BOOST_CHECK_EQUAL(std::to_integer<unsigned>(frame.pixels[1]),255u);
                    device.Destroy_Texture(color); device.Destroy_Texture(depth);
                }
            }
        }
        bloom.Shutdown();
        BOOST_REQUIRE(bloom.Initialize(device,std::filesystem::path(GRAPHICS_BLOOM_SHADER_DIRECTORY)));
        BOOST_CHECK(!bloom.Render(commands,{},RHITextureFormat::RGBA8_UNorm,true));
    }
}

BOOST_AUTO_TEST_CASE(bright_patch_spreads_symmetrically_without_tint_or_alpha_leak)
{
    for (const bool warp : {true,false}) {
        DX11Device device({warp});
        BloomRenderer bloom;
        FrameCapture capture;
        BOOST_REQUIRE(bloom.Initialize(device,std::filesystem::path(GRAPHICS_BLOOM_SHADER_DIRECTORY)));
        constexpr unsigned size = 128;
        std::vector<std::byte> pixels(size*size*4);
        for (unsigned y=0;y<size;++y) for (unsigned x=0;x<size;++x) {
            const auto offset = (y*size+x)*4;
            // Yellow square over black with spatially varying destination alpha.
            if (x>=48 && x<80 && y>=48 && y<80)
                pixels[offset] = pixels[offset+1] = std::byte{255};
            pixels[offset+3] = std::byte(x<64 ? 51 : 153);
        }
        const auto color = device.Create_Texture_Initialized({size,size,1,RHITextureFormat::RGBA8_UNorm,
            static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)}, {pixels,size*4});
        const auto depth = device.Create_Texture({size,size,1,RHITextureFormat::D24_UNorm_S8,
            static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
        BOOST_REQUIRE(color.Is_Valid()); BOOST_REQUIRE(depth.Is_Valid());
        BOOST_REQUIRE(bloom.Render(device.Immediate_Command_List(),{{color,size,size},{depth,size,size}},
            RHITextureFormat::RGBA8_UNorm,true));
        const auto frame = capture.Read(device,color,size,size,RHITextureFormat::RGBA8_UNorm);
        BOOST_REQUIRE(frame.Is_Valid());
        const auto pixel = [&](unsigned x,unsigned y,unsigned c=0) {
            return std::to_integer<unsigned>(frame.pixels[y*frame.row_pitch+x*4+c]);
        };
        BOOST_CHECK_EQUAL(pixel(64,64),255u);
        BOOST_CHECK_EQUAL(pixel(8,64),0u);
        BOOST_CHECK_GT(pixel(43,64),2u);
        BOOST_CHECK_LT(pixel(43,64),40u);
        BOOST_CHECK_EQUAL(pixel(43,64),pixel(84,64));
        BOOST_CHECK_EQUAL(pixel(64,43),pixel(64,84));
        BOOST_CHECK_EQUAL(pixel(43,64),pixel(64,43));
        BOOST_CHECK_EQUAL(pixel(43,64),pixel(43,64,1));
        BOOST_CHECK_EQUAL(pixel(43,64,2),0u);
        BOOST_CHECK_EQUAL(pixel(43,64,3),51u);
        BOOST_CHECK_EQUAL(pixel(84,64,3),153u);
        BOOST_CHECK_EQUAL(pixel(63,64,3),51u);
        BOOST_CHECK_EQUAL(pixel(64,64,3),153u);
        device.Destroy_Texture(color); device.Destroy_Texture(depth);
    }
}
