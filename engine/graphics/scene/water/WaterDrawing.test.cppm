module;
#define BOOST_TEST_MODULE WaterDrawingTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
export module Graphics.Scene.Water.Drawing.Tests;
import Graphics.Backends.DX11;
import Graphics.Scene.Water.Renderer;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(reflected_camera_preserves_water_plane_projection_and_camera_side)
{
    // Tilted RTS camera away from the origin; row 3 contains homogeneous
    // values, not the eye position. This is also the game adapter's contract.
    const std::array<float,16> camera{
        1,0,0,300, 0,0.8f,-0.6f,400, 0,0.6f,0.8f,500, 0,0,0,1};
    const WaterView view(camera,7);
    BOOST_CHECK(!view.underwater);
    BOOST_CHECK_EQUAL(view.camera_position[0],300);
    BOOST_CHECK_EQUAL(view.camera_position[1],400);
    BOOST_CHECK_EQUAL(view.camera_position[2],500);
    const auto project = [](const auto& transform,std::array<float,3> point) {
        std::array<float,3> local{};
        for (unsigned column=0;column<3;++column)
            for (unsigned row=0;row<3;++row)
                local[column] += transform[row*4+column] * (point[row]-transform[row*4+3]);
        return std::array<float,2>{local[0]/local[2],local[1]/local[2]};
    };
    for (const std::array<float,3> point : {
        std::array<float,3>{0,0,7}, {100,200,7}, {500,300,7}}) {
        const auto main = project(camera,point);
        const auto reflection = project(view.reflected_camera,point);
        BOOST_CHECK_SMALL(main[0]-reflection[0],0.00001f);
        BOOST_CHECK_SMALL(main[1]-reflection[1],0.00001f);
    }
    BOOST_CHECK_GT(view.reflection_clip_plane[2]*8+view.reflection_clip_plane[3],0);
    BOOST_CHECK_LT(view.reflection_clip_plane[2]*6+view.reflection_clip_plane[3],0);
    const WaterView reflected(view.reflected_camera,7);
    BOOST_CHECK(reflected.underwater);
    BOOST_CHECK_GT(reflected.reflection_clip_plane[2]*6+reflected.reflection_clip_plane[3],0);
    BOOST_CHECK_EQUAL_COLLECTIONS(camera.begin(),camera.end(),
        reflected.reflected_camera.begin(),reflected.reflected_camera.end());
}

BOOST_AUTO_TEST_CASE(ocean_preserves_refracted_shroud_and_concentrates_foam_on_crests)
{
    DX11Device device({true});
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device, std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto solid = [&](std::array<std::uint8_t,4> pixel) {
        return device.Create_Texture_Initialized({1,1}, {std::as_bytes(std::span(pixel)),4});
    };
    const auto black = solid({0,0,0,255});
    const auto white = solid({255,255,255,255});
    const auto normal = solid({128,128,255,255});
    const auto shroud = solid({128,128,128,255});
    const auto scene = solid({0,0,64,255});
    const auto reflection = solid({128,0,0,255});
    const auto environment = solid({16,0,0,128});
    // Floating-point displacement retains signed horizontal movement and
    // height in world units. A 0.01 height sample raises the crest by 3 units.
    const std::array<float,4> crest_sample{0,0,0.01f,0};
    const auto crest = device.Create_Texture_Initialized(
        {1,1,1,RHITextureFormat::RGBA32_Float},
        {std::as_bytes(std::span(crest_sample)),sizeof(crest_sample)});
    BOOST_REQUIRE(crest.Is_Valid());
    const auto target = device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection =
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.projection[10] = 0.01f;
    parameters.camera_position = {0,0,1000000,1};
    parameters.effects = {1,1,1,0};
    WaterStyle style;
    style.pass = WaterPass::Ocean;
    style.blend = RHIBlendMode::Disabled;
    std::array<RHITextureHandle,9> textures{
        black,black,normal,black,reflection,scene,black,shroud,white};
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    const auto check = [&](std::array<int,3> expected) {
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
        BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for (unsigned channel=0;channel<3;++channel)
            BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+channel])-expected[channel],2);
    };
    // Ocean.fx adds half the planar reflection, shrouded once. The captured
    // underwater scene is already darkened and must retain its blue value.
    check({32,0,64});
    style.pass = WaterPass::Surface;
    textures = {black,normal,black,white,reflection,scene,black,shroud,white};
    check({26,0,27});
    style.pass = WaterPass::Ocean;
    textures = {black,black,normal,black,reflection,scene,black,shroud,white};
    parameters.effects = {};
    textures[3] = white;
    check({6,6,6});
    textures[1] = crest;
    check({102,102,102});
    textures[1] = black;
    textures[3] = black;
    textures[6] = environment;
    parameters.camera_position = {1000000,0,0.5f,1};
    // The environment image stores linear RGBM radiance. Alpha scales RGB;
    // treating it as opacity or ignoring it changes reflected light energy.
    check({64,0,0});
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (auto texture : {black,white,normal,shroud,scene,reflection,environment,crest,target,depth})
        device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(planar_reflection_preserves_orientation_across_target_aspect_ratios)
{
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device, std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    // Four different quadrants expose flipped UVs and sampling a single texel.
    const std::array<std::uint8_t,16> pattern{
        255,0,0,255, 0,255,0,255,
        0,0,255,255, 255,255,0,255};
    const auto reflection = device.Create_Texture_Initialized({2,2},
        {std::as_bytes(std::span(pattern)),8});
    const auto solid = [&](std::array<std::uint8_t,4> color) {
        return device.Create_Texture_Initialized({1,1},
            {std::as_bytes(std::span(color)),4});
    };
    const auto black = solid({0,0,0,255});
    const auto white = solid({255,255,255,255});
    const auto normal = solid({128,128,255,255});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f};
    vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f};
    vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection =
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,1000000,1};
    parameters.effects = {1,0,0,0};
    const std::array<RHITextureHandle,9> textures{
        black,normal,black,white,reflection,black,black,white,white};
    WaterStyle style;
    style.blend = RHIBlendMode::Disabled;
    auto& commands = device.Immediate_Command_List();
    for (const std::uint32_t width : {32u,64u}) {
        constexpr std::uint32_t height = 24;
        const auto target = device.Create_Texture({width,height,1,RHITextureFormat::RGBA8_UNorm,
            static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
        const auto depth = device.Create_Texture({width,height,1,RHITextureFormat::D32_Float,
            static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
        BOOST_REQUIRE(commands.Set_Viewport({0,0,width,height}));
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
        BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
        std::array<std::byte,64*height*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,width*4));
        for (std::uint32_t quadrant=0;quadrant<4;++quadrant) {
            const auto x = (quadrant%2 == 0 ? width/8 : width*7/8);
            const auto y = (quadrant/2 == 0 ? height/8 : height*7/8);
            for (std::uint32_t channel=0;channel<3;++channel) {
                const int expected = pattern[quadrant*4+channel] == 0 ? 0 : 102;
                BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(y*width+x)*4+channel])-expected,2);
            }
        }
        device.Destroy_Texture(target);
        device.Destroy_Texture(depth);
    }
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (const auto texture : {reflection,black,white,normal}) device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(refraction_rejects_displacement_across_object_edges)
{
    DX11Device device({true});
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto solid = [&](std::array<std::uint8_t,4> pixel) {
        return device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(pixel)),4});
    };
    const auto black = solid({0,0,0,255});
    const auto white = solid({255,255,255,255});
    const auto normal = solid({255,128,255,255});
    std::array<std::uint8_t,64*4> scene{};
    for (unsigned x=0;x<64;++x) {
        scene[x*4+(x<32 ? 2 : 0)] = 255;
        scene[x*4+3] = 255;
    }
    const auto refraction = device.Create_Texture_Initialized({64,1},
        {std::as_bytes(std::span(scene)),64*4});
    const auto target = device.Create_Texture({64,32,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({64,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection =
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,1000000,1};
    parameters.effects = {0,0,1,0};
    const std::array<RHITextureHandle,9> textures{
        black,normal,black,white,black,refraction,black,white,white};
    WaterStyle style;
    style.blend = RHIBlendMode::Disabled;
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,64,32}));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    std::array<std::byte,64*32*4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    // The distorted coordinate crosses from underwater blue into red geometry.
    // RA3 rejects that sample rather than pulling the object's edge into water.
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(16*64+30)*4]),0);
    BOOST_CHECK_GT(std::to_integer<int>(pixels[(16*64+30)*4+2]),90);
    style.pass = WaterPass::Ocean;
    const std::array<RHITextureHandle,9> ocean_textures{
        black,black,normal,black,black,refraction,black,white,white};
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,ocean_textures));
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(16*64+31)*4]),0);
    BOOST_CHECK_GT(std::to_integer<int>(pixels[(16*64+31)*4+2]),30);
    // With scene depth available, a contrasting underwater texel is valid
    // refraction. Only foreground geometry should force the original sample.
    std::array<float,64> depth_samples;
    depth_samples.fill(0.8f);
    const auto scene_depth = device.Create_Texture_Initialized(
        {64,1,1,RHITextureFormat::R32_Float},
        {std::as_bytes(std::span(depth_samples)),64*sizeof(float)});
    BOOST_REQUIRE(scene_depth.Is_Valid());
    parameters.surface_options = {0,1,1,1};
    for (const auto pass : {WaterPass::Surface,WaterPass::Ocean}) {
        style.pass = pass;
        auto depth_textures = pass == WaterPass::Surface ? textures : ocean_textures;
        depth_textures[8] = scene_depth;
        for (const bool foreground : {false,true}) {
            for (unsigned x=0;x<64;++x)
                depth_samples[x] = foreground && x>=32 ? 0.2f : 0.8f;
            BOOST_REQUIRE(device.Update_Texture(scene_depth,
                {std::as_bytes(std::span(depth_samples)),64*sizeof(float)}));
            BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
            BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,depth_textures));
            BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
            const auto pixel = (16*64+31)*4;
            BOOST_CHECK_GT(std::to_integer<int>(pixels[pixel+(foreground ? 2 : 0)]),30);
            BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[pixel+(foreground ? 0 : 2)]),0);
        }
    }
    device.Destroy_Texture(scene_depth);
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (auto texture : {black,white,normal,refraction,target,depth}) device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(degenerate_strip_connectors_preserve_winding)
{
    const std::array<std::uint32_t,10> strip{2,0,3,1,1,4,4,2,5,3};
    const auto triangles = Expand_Water_Strip(strip);
    const std::array<std::uint32_t,12> expected{2,0,3,3,0,1,4,2,5,5,2,3};
    BOOST_CHECK_EQUAL_COLLECTIONS(triangles.begin(),triangles.end(),expected.begin(),expected.end());
}

BOOST_AUTO_TEST_CASE(refraction_samples_the_scene_pixel_when_the_viewport_has_an_offset)
{
    DX11Device device({true});
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto solid = [&](std::array<std::uint8_t,4> pixel) {
        return device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(pixel)),4});
    };
    const auto black = solid({0,0,0,255});
    const auto white = solid({255,255,255,255});
    const auto normal = solid({128,128,255,255});
    std::array<std::uint8_t,64*32*4> capture{};
    for (unsigned y=0;y<32;++y) for (unsigned x=0;x<64;++x) {
        capture[(y*64+x)*4+(x<32 ? 2 : 0)] = 255;
        capture[(y*64+x)*4+3] = 255;
    }
    const auto scene = device.Create_Texture_Initialized({64,32},
        {std::as_bytes(std::span(capture)),64*4});
    const auto target = device.Create_Texture({64,32,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({64,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection =
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,1000000,1};
    parameters.effects = {0,0,1,0};
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({32,0,32,32}));
    for (const auto pass : {WaterPass::Surface,WaterPass::Ocean}) {
        WaterStyle style;
        style.pass = pass;
        style.blend = RHIBlendMode::Disabled;
        const std::array<RHITextureHandle,9> textures = pass == WaterPass::Surface
            ? std::array<RHITextureHandle,9>{black,normal,black,white,black,scene,black,white,white}
            : std::array<RHITextureHandle,9>{black,black,normal,black,black,scene,black,white,white};
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
        BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
        std::array<std::byte,64*32*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
        // This pixel belongs to the red half of the captured scene. Mapping
        // the full camera image onto this viewport incorrectly samples blue.
        BOOST_CHECK_GT(std::to_integer<int>(pixels[(16*64+36)*4]),80);
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(16*64+36)*4+2]),0);
    }
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (auto texture : {black,white,normal,scene,target,depth}) device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(surface_normal_octaves_repeat_beyond_the_first_texture_tile)
{
    DX11Device device({true});
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const std::array<std::uint8_t,8> normal_pixels{255,128,255,255, 0,128,255,255};
    const std::array<std::uint8_t,8> reflection_pixels{255,0,0,255, 0,0,255,255};
    const std::array<std::uint8_t,4> black_pixel{0,0,0,255};
    const auto normal = device.Create_Texture_Initialized({2,1},
        {std::as_bytes(std::span(normal_pixels)),8});
    const auto reflection = device.Create_Texture_Initialized({2,1},
        {std::as_bytes(std::span(reflection_pixels)),8});
    const auto black = device.Create_Texture_Initialized({1,1},
        {std::as_bytes(std::span(black_pixel)),4});
    const auto target = device.Create_Texture({32,32,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({32,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    for (auto& vertex : vertices) vertex.secondary_uv = {0.25f,0.25f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection =
        {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,1000000,1};
    parameters.effects = {1,0,0,0};
    WaterStyle style;
    style.blend = RHIBlendMode::Disabled;
    const std::array<RHITextureHandle,9> textures{
        black,normal,black,black,reflection,black,black,black,black};
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,32,32}));
    std::array<std::byte,32*32*4> first{}, repeated{};
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    BOOST_REQUIRE(device.Readback_Texture(target,first,32*4));
    for (auto& vertex : vertices) vertex.secondary_uv[0] += 1;
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    BOOST_REQUIRE(device.Readback_Texture(target,repeated,32*4));
    BOOST_CHECK_GT(std::to_integer<int>(first[(16*32+16)*4+2]),20);
    for (unsigned channel=0;channel<3;++channel)
        BOOST_CHECK_SMALL(std::to_integer<int>(first[(16*32+16)*4+channel]) -
            std::to_integer<int>(repeated[(16*32+16)*4+channel]),1);
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (auto texture : {normal,reflection,black,target,depth}) device.Destroy_Texture(texture);
}
BOOST_AUTO_TEST_CASE(water_shader_passes_preserve_sampling_blending_and_displacement)
{
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    WaterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto texture = [&](std::array<std::uint8_t,4> color) {
        return device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(color)),4});
    };
    const auto base = texture({128,64,32,255});
    const auto wave = texture({255,0,0,128});
    const auto normal = texture({128,128,255,255});
    const auto black = texture({0,0,0,0});
    const auto white = texture({255,255,255,255});
    const auto grey = texture({128,128,128,255});
    const auto displacement = texture({255,0,0,0});
    const auto target = device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<WaterVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f};
    vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f};
    vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    WaterParameters parameters;
    parameters.world = parameters.view = parameters.projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,10,1};
    parameters.displacement_domain = {0,0,1,1};
    parameters.effects = {0,1,0,0};
    std::array<RHITextureHandle,9> textures{base,normal,black,white,black,black,black,grey,white};
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    const auto check = [&](std::array<int,4> expected) {
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for(int c=0;c<4;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+c])-expected[c],2);
    };
    WaterStyle style;
    style.blend = RHIBlendMode::Disabled;
    style.wireframe = true;
    BOOST_REQUIRE(commands.Clear({0,0,1,0.75f},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    std::array<std::byte,16*16*4> wire_pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,wire_pixels,16*4));
    BOOST_CHECK_EQUAL(std::to_integer<int>(wire_pixels[(8*16+4)*4+2]),255);
    style.wireframe = false;
    BOOST_REQUIRE(commands.Clear({0,0,1,0.75f},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({80,40,20,255});
    // The ninth sampled texture drives deep-water opacity.
    parameters.surface_options = {0,1,0.5f,1};
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({80,40,20,128});
    style.pass = WaterPass::Sky;
    style.blend = RHIBlendMode::Alpha;
    textures[0] = wave;
    BOOST_REQUIRE(commands.Clear({0,0,1,0.75f},0));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({128,0,127,160});
    style.pass = WaterPass::Track;
    BOOST_REQUIRE(commands.Clear({0,0,1,0.75f},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({128,0,127,160});
    style.pass = WaterPass::Ocean;
    style.blend = RHIBlendMode::Disabled;
    parameters.fog_state = {0,0.1f,1,0};
    parameters.fog_color = {1,0,0,1};
    textures = {base,black,normal,black,black,black,black,white,white};
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({255,0,0,128});
    // A displacement texture sampled by the vertex stage moves this patch
    // outside the viewport; fragment-only texture binding cannot pass this.
    textures[1] = displacement;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    check({0,0,255,255});
    style.pass = WaterPass::Displacement;
    textures[0] = grey;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,textures));
    std::array<std::byte,16*16*4> produced{};
    BOOST_REQUIRE(device.Readback_Texture(target,produced,16*4));
    BOOST_CHECK_EQUAL(std::to_integer<int>(produced[(8*16+8)*4+3]),0);
    BOOST_CHECK_NE(std::to_integer<int>(produced[(8*16+8)*4+2]),255);
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for(auto handle : {base,wave,normal,black,white,grey,displacement,target,depth}) device.Destroy_Texture(handle);
}
