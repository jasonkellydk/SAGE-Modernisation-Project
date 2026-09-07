module;
#define BOOST_TEST_MODULE MaterialPassDrawingTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
export module Graphics.Scene.Props.MaterialPassDrawing.Tests;
import Graphics.Scene.Props.MaterialPassQueue;
import Graphics.Backends.DX11;
using namespace Graphics;
BOOST_AUTO_TEST_CASE(deferred_pass_observes_opaque_depth_and_keeps_submission_order)
{
    DX11Device device({true}); BOOST_REQUIRE(device.Is_Valid());
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    std::array<PropVertex,4> vertices{};
    vertices[0].position={-1,-1,0.6f}; vertices[1].position={1,-1,0.6f};
    vertices[2].position={1,1,0.6f}; vertices[3].position={-1,1,0.6f};
    for (auto& vertex : vertices) vertex.color={0,1,0,1};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    PropParameters parameters;
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; parameters.textured=0;
    PropStyle style; style.depth_write=false;
    MaterialPassQueue queue;
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,{}));
    // Later opaque geometry occludes the left half. Submission copied the
    // green vertices, so this mutation must not recolor the deferred pass.
    for (auto& vertex : vertices) {
        vertex.position[0]=(vertex.position[0]-1)*0.5f;
        vertex.position[2]=0.4f; vertex.color={1,0,0,1};
    }
    const auto occluder=renderer.Create_Mesh(vertices,indices);
    style.depth_write=true;
    BOOST_REQUIRE(renderer.Draw(commands,occluder,style,parameters,{}));
    BOOST_REQUIRE(queue.Flush(commands)); BOOST_CHECK(queue.Empty());
    const auto check=[&](unsigned x,std::array<int,4> expected) {
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for (unsigned c=0;c<4;++c)
            BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+x)*4+c])-expected[c],2);
    };
    check(3,{255,0,0,255}); check(12,{0,255,0,255});
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    style.depth_test=false; style.depth_write=false;
    style.source_blend=RHIBlendFactor::SourceAlpha;
    style.destination_blend=RHIBlendFactor::InverseSourceAlpha;
    for (auto& vertex : vertices) vertex.color={1,0,0,0.5f};
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,{}));
    for (auto& vertex : vertices) vertex.color={0,1,0,0.5f};
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,{}));
    BOOST_REQUIRE(queue.Flush(commands)); check(3,{64,128,0,96});
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,{}));
    queue.Clear(); BOOST_CHECK(queue.Empty());
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(queue.Flush(commands)); check(3,{0,0,255,255});
    // A projected transition mask writes sampled alpha without replacing RGB.
    // A following ordinary pass must restore its own channel mask.
    const std::array<std::uint8_t,8> mask_pixels{255,0,0,64,0,255,0,192};
    const auto mask=device.Create_Texture_Initialized({2,1},
        {std::as_bytes(std::span(mask_pixels)),8});
    BOOST_REQUIRE(mask.Is_Valid());
    for (auto& vertex : vertices) vertex.uv={vertex.position[0]+1,0.5f};
    parameters.textured=1; parameters.primary_gradient=0;
    style=PropStyle{}; style.color_write_mask=8;
    style.samplers[0].Set_Filter(Graphics::RHISamplerFilter::Point);
    const std::array mask_textures{mask};
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,mask_textures));
    BOOST_REQUIRE(queue.Flush(commands));
    check(3,{0,0,255,64}); check(6,{0,0,255,192}); check(12,{0,0,255,255});
    parameters.textured=0; parameters.primary_gradient=1;
    style.color_write_mask=15;
    BOOST_REQUIRE(queue.Submit(renderer,vertices,indices,style,parameters,{}));
    BOOST_REQUIRE(queue.Flush(commands)); check(3,{0,255,0,128});
    device.Destroy_Texture(mask);
    renderer.Destroy_Mesh(occluder); renderer.Shutdown();
    device.Destroy_Texture(target); device.Destroy_Texture(depth);
}
