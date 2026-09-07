module;
#define BOOST_TEST_MODULE PropSubmissionTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
export module Graphics.Scene.Props.Submission.Tests;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Graphics.Backends.DX11;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(decal_groups_materials_and_transparency_preserve_order_and_ownership)
{
    DX11Device device({true});
    PropRenderer renderer;
    DirectionalShadowRenderer shadows;
    PropSubmission submission;
    const auto shaders = std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
    BOOST_REQUIRE(renderer.Initialize(device,shaders));
    BOOST_REQUIRE(shadows.Initialize(device,shaders));
    submission.Initialize(device,renderer,shadows);
    const auto target = device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,
        static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    std::array<PropVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    PropParameters parameters;
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    PropStyle style;
    style.depth_write = false;
    style.depth_test = false;
    style.source_blend = RHIBlendFactor::SourceAlpha;
    style.destination_blend = RHIBlendFactor::InverseSourceAlpha;
    const std::array<std::array<std::uint8_t,4>,5> colors{{
        {255,0,0,128},{255,255,0,128},{0,255,0,128},{0,0,255,128},{255,255,255,128}}};
    std::array<RHITextureHandle,5> textures{};
    for (unsigned index=0;index<colors.size();++index)
        textures[index] = device.Create_Texture_Initialized({1,1},
            {std::as_bytes(std::span(colors[index])),4});
    submission.Begin_Decal_Group();
    BOOST_REQUIRE(submission.Submit(mesh,style,parameters,std::array{textures[0]},PropDrawPhase::Decal));
    BOOST_REQUIRE(submission.Submit(mesh,style,parameters,std::array{textures[1]},PropDrawPhase::Decal));
    submission.Begin_Decal_Group();
    BOOST_REQUIRE(submission.Submit(mesh,style,parameters,std::array{textures[2]},PropDrawPhase::Decal));
    BOOST_REQUIRE(submission.Submit(mesh,style,parameters,std::array{textures[3]},PropDrawPhase::Material));
    BOOST_REQUIRE(submission.Submit(mesh,style,parameters,std::array{textures[4]},PropDrawPhase::Transparent));
    BOOST_REQUIRE(renderer.Destroy_Mesh(mesh));
    BOOST_REQUIRE(submission.Flush_Materials());
    std::array<float,4> expected{};
    const auto blend = [&](unsigned index) {
        const float alpha = colors[index][3]/255.0f;
        for (unsigned channel=0;channel<4;++channel)
            expected[channel] = colors[index][channel]*alpha+expected[channel]*(1-alpha);
    };
    // Last decal object first; authored material order within each object.
    for (unsigned index : {2u,0u,1u,3u}) blend(index);
    std::array<std::byte,8*8*4> pixels{};
    const auto check = [&] {
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,32));
        for (unsigned channel=0;channel<4;++channel)
            BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(4*8+4)*4+channel])-expected[channel],2.0f);
    };
    check();
    BOOST_REQUIRE(submission.Flush_Transparent());
    blend(4);
    check();
    BOOST_CHECK(renderer.Mesh_Geometry(mesh) == nullptr);
    std::array<std::byte,4> sample{};
    for (const auto texture : textures) BOOST_CHECK(!device.Readback_Texture(texture,sample,4));
    // Cancellation also releases transferred textures and retained geometry.
    const auto cancelled = renderer.Create_Mesh(vertices,indices);
    const auto texture = device.Create_Texture_Initialized({1,1},
        {std::as_bytes(std::span(colors[0])),4});
    BOOST_REQUIRE(submission.Submit(cancelled,style,parameters,std::array{texture},PropDrawPhase::Material));
    BOOST_REQUIRE(renderer.Destroy_Mesh(cancelled));
    submission.Shutdown();
    BOOST_CHECK(renderer.Mesh_Geometry(cancelled) == nullptr);
    BOOST_CHECK(!device.Readback_Texture(texture,sample,4));
    shadows.Shutdown(); renderer.Shutdown();
    device.Destroy_Texture(target); device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(shared_texture_handles_survive_source_release_and_multiple_deferred_consumers)
{
    DX11Device device({true});
    PropRenderer renderer;
    DirectionalShadowRenderer shadows;
    PropSubmission submission;
    const auto shaders = std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
    BOOST_REQUIRE(renderer.Initialize(device, shaders));
    BOOST_REQUIRE(shadows.Initialize(device, shaders));
    submission.Initialize(device, renderer, shadows);
    const auto target = device.Create_Texture({8, 8, 1, RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({8, 8, 1, RHITextureFormat::D32_Float,
        static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target, depth));
    BOOST_REQUIRE(commands.Set_Viewport({0, 0, 8, 8}));
    BOOST_REQUIRE(commands.Clear({0, 0, 1, 1}, 1));
    std::array<PropVertex, 4> vertices{};
    vertices[0].position = {-1, -1, 0.5f}; vertices[1].position = {1, -1, 0.5f};
    vertices[2].position = {1, 1, 0.5f}; vertices[3].position = {-1, 1, 0.5f};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const auto mesh = renderer.Create_Mesh(vertices, indices);
    PropParameters parameters;
    parameters.view_projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    PropStyle style;
    style.depth_write = style.depth_test = false;
    style.source_blend = RHIBlendFactor::SourceAlpha;
    style.destination_blend = RHIBlendFactor::InverseSourceAlpha;
    const std::array<std::byte, 4> red{std::byte{255}, {}, {}, std::byte{128}};
    const auto texture = device.Create_Texture_Initialized({1, 1}, {red, 4});
    for (auto phase : {PropDrawPhase::Material, PropDrawPhase::Transparent}) {
        BOOST_REQUIRE(device.Retain_Texture(texture));
        BOOST_REQUIRE(submission.Submit(mesh, style, parameters, std::array{texture}, phase));
    }
    BOOST_REQUIRE(device.Destroy_Texture(texture));
    BOOST_REQUIRE(renderer.Destroy_Mesh(mesh));
    BOOST_REQUIRE(submission.Flush_Materials());
    std::array<std::byte, 4> sample{};
    BOOST_REQUIRE(device.Readback_Texture(texture, sample, 4));
    BOOST_REQUIRE(submission.Flush_Transparent());
    std::array<std::byte, 8 * 8 * 4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target, pixels, 32));
    const auto center = (4 * 8 + 4) * 4;
    BOOST_CHECK_SMALL(std::to_integer<int>(pixels[center]) - 192, 2);
    BOOST_CHECK_SMALL(std::to_integer<int>(pixels[center + 2]) - 63, 2);
    BOOST_CHECK(!device.Readback_Texture(texture, sample, 4));
    submission.Shutdown(); shadows.Shutdown(); renderer.Shutdown();
    device.Destroy_Texture(target); device.Destroy_Texture(depth);
}
