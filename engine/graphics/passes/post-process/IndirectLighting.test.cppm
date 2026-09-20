module;
#define BOOST_TEST_MODULE IndirectLightingTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <vector>
#include <span>
#include <cmath>
export module Graphics.Passes.IndirectLighting.Tests;
import Graphics.Passes.IndirectLighting;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Lighting.Environment;
import Graphics.Tests.Device;
import Graphics.FrameTargets;
using namespace Graphics;

BOOST_AUTO_TEST_CASE(red_wall_bounces_light_to_nearby_neutral_floor)
{
    struct Reset {~Reset(){Get_Environment_Lighting()={};}} reset;
    Get_Environment_Lighting()={};
    auto& env=Get_Environment_Lighting().parameters;
    env.pbr_options={1,1,1,1};env.sky_radiance={.15f,.15f,.15f,0};env.ground_radiance=env.sky_radiance;
    GraphicsTestDevice device;BOOST_REQUIRE(device.Is_Valid());
    const auto directory=Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
    IndirectLightingRenderer indirect;PropRenderer props;
    BOOST_REQUIRE(indirect.Initialize(device,directory));BOOST_REQUIRE(props.Initialize(device,directory));
    constexpr unsigned size=64;
    const auto color=device.Create_Texture({size,size,1,RHITextureFormat::RGBA32_Float,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({size,size,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    const FrameTargets targets{{color,size,size},{depth,size,size}};
    const auto material=indirect.Begin_Frame(size,size);
    BOOST_REQUIRE(material[2].Is_Valid());
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Color_Targets(std::array{color,material[0],material[1],material[2]},depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,size,size}));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    IndirectLightingInput input;
    input.radius=12;input.occlusion_radius=3;input.bias=.05f;
    input.projection={.1f,0,0,0,0,.1f,0,0,0,0,-.02f,0,0,0,0,1};
    input.inverse_projection={10,0,0,0,0,10,0,0,0,0,-50,0,0,0,0,1};
    constexpr float c=.70710678f;
    input.view={c,0,c,0,0,1,0,0,-c,0,c,-20,0,0,0,1};
    PropParameters parameters;
    for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)
        for(unsigned k=0;k<4;++k)parameters.view_projection[r*4+col]+=input.projection[r*4+k]*input.view[k*4+col];
    parameters.camera_position={-20*c,0,20*c,1};
    parameters.textured=0;parameters.surface.shading_model=2;parameters.surface.roughness=.8f;
    PropStyle style;style.blend=RHIBlendMode::Disabled;
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    std::array<PropVertex,4> floor{};
    const std::array<std::array<float,3>,4> corners{{{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0}}};
    for(unsigned i=0;i<4;++i) {
        floor[i].position=corners[i];floor[i].normal={0,0,1};
        floor[i].material_ambient={1,1,1,1};floor[i].material_diffuse={.7f,.7f,.7f,1};
    }
    const auto floor_mesh=props.Create_Mesh(floor,indices);
    BOOST_REQUIRE(props.Draw(commands,floor_mesh,style,parameters,{}));
    std::array<PropVertex,4> wall=floor;
    const std::array<std::array<float,3>,4> wall_corners{{{2,-8,0},{2,8,0},{2,8,6},{2,-8,6}}};
    for(unsigned i=0;i<4;++i) {
        wall[i].position=wall_corners[i];wall[i].normal={-1,0,0};
        wall[i].material_diffuse={1,0,0,1};wall[i].material_emissive={2,0,0,0};
    }
    const auto wall_mesh=props.Create_Mesh(wall,indices);
    BOOST_REQUIRE(props.Draw(commands,wall_mesh,style,parameters,{}));
    std::vector<float> before(size*size*4),after(before.size());
    BOOST_REQUIRE(device.Readback_Texture(color,std::as_writable_bytes(std::span(before)),size*16));
    BOOST_REQUIRE(indirect.Render(commands,targets,input,RHITextureFormat::D32_Float,RHITextureFormat::RGBA32_Float));
    BOOST_REQUIRE(device.Readback_Texture(color,std::as_writable_bytes(std::span(after)),size*16));
    unsigned receiving=0;
    for(unsigned pixel=0;pixel<size*size;++pixel) {
        const unsigned i=pixel*4;
        BOOST_CHECK(std::isfinite(after[i]));
        // Only inspect originally neutral floor pixels, never the red wall.
        if(before[i]>.01f && std::abs(before[i]-before[i+1])<.001f
            && after[i]-after[i+1]>.005f) ++receiving;
    }
    BOOST_TEST(receiving>10u);
    // Resize invalidates all surface buffers and starts with cleared data.
    const auto resized=indirect.Begin_Frame(32,32);
    BOOST_REQUIRE(resized[0].Is_Valid());BOOST_CHECK(resized[0]!=material[0]);
    props.Destroy_Mesh(floor_mesh);props.Destroy_Mesh(wall_mesh);props.Shutdown();indirect.Shutdown();
    device.Destroy_Texture(color);device.Destroy_Texture(depth);
}
