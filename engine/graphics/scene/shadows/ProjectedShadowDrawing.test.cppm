module;
#define BOOST_TEST_MODULE ProjectedShadowDrawingTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
export module Graphics.Scene.Shadows.Projected.Tests;
import Graphics.Backends.DX11;
import Graphics.Scene.Shadows.Projected;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(decal_blends_and_projected_receiver_cutout)
{
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const std::array<std::uint8_t,4> texel{128,64,32,128};
    const auto texture=device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(texel)),4});
    const auto target=device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,RHITextureFormat::D24_UNorm_S8,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<PropVertex,4> vertices{};
    vertices[0].position={-1,-1,0.5f}; vertices[1].position={1,-1,0.5f};
    vertices[2].position={1,1,0.5f}; vertices[3].position={-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh=renderer.Create_Mesh(vertices,indices);
    PropParameters parameters;
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    const auto check=[&](std::array<int,4> expected) {
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for (int c=0;c<4;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+c])-expected[c],2);
    };
    BOOST_REQUIRE(commands.Clear({0.5f,0.5f,0.5f,1},1));
    BOOST_REQUIRE(Draw_Decal(renderer,commands,mesh,parameters,texture,DecalBlend::Multiply));
    check({64,32,16,128});
    BOOST_REQUIRE(commands.Clear({0.5f,0.5f,0.5f,1},1));
    BOOST_REQUIRE(Draw_Decal(renderer,commands,mesh,parameters,texture,DecalBlend::Alpha));
    check({128,96,80,191});
    BOOST_REQUIRE(commands.Clear({0.25f,0.25f,0.25f,0},1));
    BOOST_REQUIRE(Draw_Decal(renderer,commands,mesh,parameters,texture,DecalBlend::Additive));
    check({192,128,96,128});
    // Receiver geometry behind the depth buffer must not mark or darken it.
    BOOST_REQUIRE(commands.Clear({1,1,1,1},0.25f));
    BOOST_REQUIRE(Draw_Projected_Shadow(renderer,commands,mesh,parameters,std::span(&texture,1)));
    check({255,255,255,255});
    BOOST_REQUIRE(commands.Clear({1,1,1,1},1));
    parameters.alpha_cutoff=0.75f;
    BOOST_REQUIRE(Draw_Projected_Shadow(renderer,commands,mesh,parameters,std::span(&texture,1)));
    check({255,255,255,255});
    parameters.alpha_cutoff=96.0f/255.0f;
    BOOST_REQUIRE(Draw_Projected_Shadow(renderer,commands,mesh,parameters,std::span(&texture,1)));
    check({128,64,32,128});
    // Perspective projection uses homogeneous division before sampling.
    const std::array<std::uint8_t,8> pattern{255,0,0,255,0,255,0,255};
    const auto projected=device.Create_Texture_Initialized({2,1},{std::as_bytes(std::span(pattern)),8});
    parameters.uv_sources={1,1,0,0};
    parameters.uv_transform[0]={0,0,0,0.75f,0,0,0,0.5f,0,0,0,0,0,0,0,2};
    BOOST_REQUIRE(commands.Clear({1,1,1,1},1));
    BOOST_REQUIRE(Draw_Projected_Shadow(renderer,commands,mesh,parameters,std::span(&projected,1)));
    check({191,64,0,255});
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    device.Destroy_Texture(projected); device.Destroy_Texture(texture);
    device.Destroy_Texture(target); device.Destroy_Texture(depth);
}

