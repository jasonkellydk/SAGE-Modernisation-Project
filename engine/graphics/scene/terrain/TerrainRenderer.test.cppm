module;
#define BOOST_TEST_MODULE TerrainRendererTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
export module Graphics.Scene.Terrain.Renderer.Tests;
import Graphics.Backends.DX11;
import Graphics.Scene.Terrain.Renderer;
using namespace Graphics;

namespace
{
RHITextureHandle Texture(Device &device, std::array<std::uint8_t, 4> pixel)
{
    return device.Create_Texture_Initialized({1, 1}, {std::as_bytes(std::span(pixel)), 4});
}

BOOST_AUTO_TEST_CASE(loaded_surface_draws_on_first_frame_and_survives_device_resource_recreation)
{
    DX11Device device({true});
    TerrainRenderer renderer;
    TerrainCell cell;
    cell.origin = {-1,-1};
    cell.spacing = {2,2};
    cell.heights.fill(0.5f);
    for (auto& vertex : cell.colors) vertex = {1,0,0,0};
    // Map preparation does not depend on a live frame or swap-chain resources.
    BOOST_CHECK(renderer.Set_Cells(std::span(&cell,1)));
    const auto directory = std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
    BOOST_REQUIRE(renderer.Initialize(device,directory));
    const auto color = device.Create_Texture({32,32,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({32,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    auto& commands = device.Immediate_Command_List();
    TerrainDrawParameters parameters;
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const auto white = Texture(device,{255,255,255,255});
    const std::array<RHITextureHandle,2> map_textures{white,white};
    const auto draw = [&] {
        BOOST_REQUIRE(commands.Set_Render_Targets(color,depth));
        BOOST_REQUIRE(commands.Set_Viewport({0,0,32,32}));
        BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
        BOOST_REQUIRE(renderer.Render(commands,TerrainSurfacePass::Surface,parameters,map_textures));
        std::array<std::byte,32*32*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(color,pixels,32*4));
        return std::to_integer<int>(pixels[(16*32+16)*4]);
    };
    BOOST_CHECK_EQUAL(draw(),255);
    // Establish a resident surface even on the broken implementation, so the
    // reset assertion independently tests retention rather than initial load.
    BOOST_REQUIRE(renderer.Set_Cells(std::span(&cell,1)));
    renderer.Shutdown();
    BOOST_REQUIRE(renderer.Initialize(device,directory));
    BOOST_CHECK_EQUAL(draw(),255);
    renderer.Release_Surface();
    BOOST_CHECK_EQUAL(draw(),0);
    renderer.Shutdown();
    device.Destroy_Texture(color);
    device.Destroy_Texture(depth);
    device.Destroy_Texture(white);
}

std::array<unsigned, 4> Center(Device &device, RHITextureHandle target)
{
    std::array<std::byte, 32 * 32 * 4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target, pixels, 32 * 4));
    const std::size_t index = (16 * 32 + 16) * 4;
    return {std::to_integer<unsigned>(pixels[index]), std::to_integer<unsigned>(pixels[index + 1]),
        std::to_integer<unsigned>(pixels[index + 2]), std::to_integer<unsigned>(pixels[index + 3])};
}

void Check_Color(const std::array<unsigned, 4> &actual, std::array<int, 4> expected)
{
    for (std::size_t component = 0; component < 4; ++component)
        BOOST_CHECK_SMALL(static_cast<int>(actual[component]) - expected[component], 2);
}
}

BOOST_AUTO_TEST_CASE(surface_blend_shroud_overlay_shoreline_and_depth_have_independent_contracts)
{
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    TerrainRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device, std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const RHITextureHandle color = device.Create_Texture({32, 32, 1, RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const RHITextureHandle depth = device.Create_Texture({32, 32, 1, RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    const RHITextureHandle red = Texture(device, {255, 0, 0, 255});
    const RHITextureHandle blue = Texture(device, {0, 0, 255, 255});
    const RHITextureHandle green = Texture(device, {0, 255, 0, 255});
    const RHITextureHandle shroud = Texture(device, {128, 128, 128, 255});
    const RHITextureHandle coverage = Texture(device, {255, 255, 255, 128});
    BOOST_REQUIRE(color.Is_Valid() && depth.Is_Valid() && red.Is_Valid() && blue.Is_Valid()
        && green.Is_Valid() && shroud.Is_Valid() && coverage.Is_Valid());
    CommandList &commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(color, depth));
    BOOST_REQUIRE(commands.Set_Viewport({0, 0, 32, 32}));
    BOOST_REQUIRE(commands.Clear({0, 0, 0, 0.75f}, 1));
    TerrainCell cell;
    cell.origin = {-1, -1};
    cell.spacing = {2, 2};
    cell.heights.fill(0.5f);
    for (auto &vertex : cell.colors) vertex = {1, 1, 1, 0.25f};
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    TerrainDrawParameters parameters;
    parameters.view_projection = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const std::array<RHITextureHandle, 5> textures{red, blue, {}, {}, shroud};
    BOOST_CHECK(!renderer.Render(commands, TerrainSurfacePass::Surface, parameters, {}));
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, textures));
    Check_Color(Center(device, color), {191, 0, 64, 191});
    parameters.options[1] = 1;
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, textures));
    Check_Color(Center(device, color), {96, 0, 32, 191});
    for (auto &vertex : cell.colors) vertex[3] = 0.5f;
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    parameters.features[3] = 3;
    const std::array<RHITextureHandle, 5> overlay{green, {}, {}, {}, shroud};
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Overlay, parameters, overlay));
    Check_Color(Center(device, color), {48, 64, 16, 191});
    parameters.features[3] = 4;
    const std::array<RHITextureHandle, 1> shoreline{coverage};
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Shoreline, parameters, shoreline));
    Check_Color(Center(device, color), {48, 64, 16, 128});
    cell.heights.fill(0.8f);
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    parameters.features[3] = 0;
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, textures));
    Check_Color(Center(device, color), {48, 64, 16, 128});
    BOOST_REQUIRE(commands.Clear({0, 0, 0, 0.75f}, 1));
    cell.heights.fill(0.5f);
    for (auto &vertex : cell.colors) vertex = {0.5f, 0.5f, 0.5f, 0};
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    parameters.options[1] = 0;
    parameters.light_options[0] = 1;
    parameters.lights[0].ambient_kind[3] = 1;
    parameters.lights[0].direction = {0, 0, -1, 0};
    parameters.lights[0].diffuse_inner = {0.25f, 0, 0, 0};
    const std::array<RHITextureHandle, 2> lit_textures{coverage, coverage};
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, lit_textures));
    Check_Color(Center(device, color), {191, 127, 127, 191});
    const std::array<std::uint8_t, 8> texels{255, 0, 0, 255, 0, 0, 255, 255};
    const RHITextureHandle split = device.Create_Texture_Initialized({2, 1},
        {std::as_bytes(std::span(texels)), 8});
    BOOST_REQUIRE(split.Is_Valid());
    for (auto &vertex : cell.colors) vertex = {1, 1, 1, 0};
    for (auto &uv : cell.base_uv) uv = {0.5f, 0.5f};
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    parameters.light_options[0] = 0;
    const std::array<RHITextureHandle, 2> filtered_textures{split, split};
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, filtered_textures, true));
    Check_Color(Center(device, color), {128, 0, 128, 191});
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, filtered_textures, false));
    Check_Color(Center(device, color), {0, 0, 255, 191});
    // Transition masks sample projected alpha without requiring terrain or
    // shroud textures, preserve scene RGB, and establish surface depth.
    const std::array<std::uint8_t, 8> mask_texels{255, 0, 0, 64, 0, 255, 0, 192};
    const RHITextureHandle mask = device.Create_Texture_Initialized({2, 1},
        {std::as_bytes(std::span(mask_texels)), 8});
    BOOST_REQUIRE(mask.Is_Valid());
    BOOST_REQUIRE(commands.Clear({0, 0, 1, 1}, 1));
    parameters.features = {0, 0, 0, 5};
    parameters.shroud_projection = {0, 0, 0.25f, 0.5f};
    std::array<RHITextureHandle, 5> mask_textures{};
    mask_textures[4] = mask;
    BOOST_CHECK(!renderer.Render(commands, TerrainSurfacePass::Mask, parameters, {}));
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Mask, parameters, mask_textures));
    Check_Color(Center(device, color), {0, 0, 255, 64});
    parameters.shroud_projection[2] = 0.75f;
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Mask, parameters, mask_textures));
    Check_Color(Center(device, color), {0, 0, 255, 192});
    cell.heights.fill(0.8f);
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell, 1)));
    parameters.features[3] = 0;
    BOOST_REQUIRE(renderer.Render(commands, TerrainSurfacePass::Surface, parameters, textures));
    Check_Color(Center(device, color), {0, 0, 255, 192});
    // The editor's wireframe surface retains cell edges without filling the
    // interior, including point-filtered terrain.
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
    parameters.features = {0,0,1,0};
    for (auto& vertex : cell.colors) vertex = {1,0,0,1};
    BOOST_REQUIRE(renderer.Set_Cells(std::span<const TerrainCell>(&cell,1)));
    BOOST_REQUIRE(renderer.Render(commands,TerrainSurfacePass::Surface,parameters,{},false,true));
    std::array<std::byte,32*32*4> wire_pixels{};
    BOOST_REQUIRE(device.Readback_Texture(color,wire_pixels,32*4));
    unsigned edge_pixels = 0;
    for (unsigned i=0;i<32*32;++i)
        if (std::to_integer<unsigned>(wire_pixels[i*4])>200) ++edge_pixels;
    BOOST_CHECK_GT(edge_pixels,0);
    BOOST_CHECK_LT(edge_pixels,160);
    BOOST_CHECK_EQUAL(std::to_integer<unsigned>(wire_pixels[(8*32+8)*4]),0);
    renderer.Shutdown();
    for (RHITextureHandle texture : {mask, split, coverage, shroud, green, blue, red, depth, color})
        BOOST_CHECK(device.Destroy_Texture(texture));
}
