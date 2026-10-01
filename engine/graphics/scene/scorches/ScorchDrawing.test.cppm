module;
#define BOOST_TEST_MODULE ScorchDrawingTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Scorches.Drawing.Tests;
import std;
import Graphics.Scene.Scorches.Geometry;
import Graphics.Scene.Surfaces.Renderer;
import Graphics.Tests.Device;
using namespace Graphics;
BOOST_AUTO_TEST_CASE(terrain_diagonals_atlas_border_and_capacity)
{
    ScorchGeometry geometry;
    const ScorchGrid grid{3,3,1,10,0.0625f};
    const auto height = [](int x, int y) { return float(x + 3*y); };
    BOOST_REQUIRE(geometry.Append({{0,0},10,8}, grid, {1,1,1,1}, height,
        [](int x, int y) { return x == y; }));
    BOOST_REQUIRE_EQUAL(geometry.vertices.size(), 9);
    BOOST_REQUIRE_EQUAL(geometry.indices.size(), 24);
    BOOST_CHECK_EQUAL(geometry.vertices[0].position[0], -10);
    BOOST_CHECK_EQUAL(geometry.vertices[4].position[2], 4.0625f);
    BOOST_CHECK_EQUAL(geometry.vertices[4].uv[0], 0.875f);
    BOOST_CHECK_EQUAL(geometry.vertices[4].uv[1], 0.875f);
    const std::array<std::uint32_t,12> expected{1,3,0,1,4,3,1,5,4,1,2,5};
    BOOST_CHECK_EQUAL_COLLECTIONS(geometry.indices.begin(), geometry.indices.begin()+12, expected.begin(), expected.end());
    BOOST_CHECK(!geometry.Append({{0,0},10,0}, grid, {1,1,1,1}, height,
        [](int,int) { return false; }, 12, 48));
    BOOST_CHECK_EQUAL(geometry.vertices.size(), 9);
    BOOST_CHECK_EQUAL(geometry.indices.size(), 24);
    BOOST_CHECK(geometry.Append({{100,100},1,0}, grid, {1,1,1,1}, height, [](int,int) { return false; }));
    BOOST_CHECK_EQUAL(geometry.indices.size(), 24);
    BOOST_CHECK(!geometry.Append({{0,0},0,0}, grid, {1,1,1,1}, height, [](int,int) { return false; }));
}
BOOST_AUTO_TEST_CASE(decal_blends_over_ground_preserving_destination_alpha)
{
    GraphicsTestDevice device({true});
    BOOST_REQUIRE(device.Is_Valid());
    SurfaceRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device, Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    ScorchGeometry geometry;
    BOOST_REQUIRE(geometry.Append({{0,0},1,0}, {3,3,1,1,0.01f}, {1,1,1,1},
        [](int,int) { return 0.5f; }, [](int,int) { return false; }));
    const auto mesh = renderer.Create_Mesh(geometry.vertices, geometry.indices);
    const std::array<std::uint8_t,4> texel{0,0,0,128};
    const auto texture = device.Create_Texture_Initialized({1,1}, {std::as_bytes(std::span(texel)),4});
    const auto target = device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target, depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    BOOST_REQUIRE(commands.Clear({0.8f,0.4f,0.2f,0.75f},1));
    SurfaceParameters parameters;
    parameters.view_projection = {1,0,0,0, 0,-1,0,0, 0,0,1,0, 0,0,0,1};
    SurfaceStyle style;
    style.cull = RHICullMode::Back;
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,std::array<RHITextureHandle,4>{texture,{},{},{}}));
    std::array<std::byte,16*16*4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
    const std::array<int,4> expected{102,51,25,191};
    for (int c=0;c<4;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+c])-expected[c],2);
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (auto handle : {texture,target,depth}) device.Destroy_Texture(handle);
}
BOOST_AUTO_TEST_CASE(a_whole_texture_decal_spans_its_square)
{
    ScorchGeometry geometry;
    const ScorchGrid grid{3,3,1,10,0};
    ScorchDescription decal{{0,0},10,0};
    decal.whole_texture = true;
    BOOST_REQUIRE(geometry.Append(decal, grid, {1,0,0,0.5f}, [](int,int) { return 0.0f; }, [](int,int) { return false; }));
    BOOST_REQUIRE_EQUAL(geometry.vertices.size(), 9);
    BOOST_CHECK_EQUAL(geometry.vertices[0].uv[0], 0.0f);
    BOOST_CHECK_EQUAL(geometry.vertices[0].uv[1], 0.0f);
    BOOST_CHECK_EQUAL(geometry.vertices[4].uv[0], 0.5f);
    BOOST_CHECK_EQUAL(geometry.vertices[8].uv[1], 1.0f);
    BOOST_CHECK_EQUAL(geometry.vertices[4].color[3], 0.5f);
}

// Its triangles face up counter-clockwise seen from above (as the tracks' strips): a pass culling back faces must take
// counter-clockwise faces as front (SurfaceStyle::front_counter_clockwise), or every scorch and radius decal is culled.
BOOST_AUTO_TEST_CASE(scorch_triangles_wind_counter_clockwise_seen_from_above)
{
    ScorchGeometry geometry;
    const ScorchGrid grid{5,5,1,10,0};
    ScorchDescription decal{{20,20},15,0};
    decal.whole_texture = true;
    BOOST_REQUIRE(geometry.Append(decal, grid, {1,1,1,1}, [](int,int) { return 0.0f; }, [](int x,int y) { return (x+y)%2==0; }));
    BOOST_REQUIRE(!geometry.indices.empty());
    for (std::size_t i = 0; i + 2 < geometry.indices.size(); i += 3)
    {
        const auto &a = geometry.vertices[geometry.indices[i]].position, &b = geometry.vertices[geometry.indices[i+1]].position,
            &c = geometry.vertices[geometry.indices[i+2]].position;
        const float z = (b[0]-a[0])*(c[1]-a[1]) - (b[1]-a[1])*(c[0]-a[0]);
        BOOST_CHECK_GT(z, 0.0f);
    }
}
