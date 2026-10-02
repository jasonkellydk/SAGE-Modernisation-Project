module;
#define BOOST_TEST_MODULE ScreenFilterDrawingTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Screen.Filters.Tests;
import std;
import Graphics.Tests.Device;
import Graphics.Scene.Screen.Filters;
using namespace Graphics;
BOOST_AUTO_TEST_CASE(color_transfer_frame_reuses_resources_updates_curves_and_resizes_without_sampling_its_output) {
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());ScreenFilterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    ColorTransferFrame frame;ColorTransferFrame::Curve curve{};
    for(unsigned i=0;i<256;++i) curve[i]={i/255.f,i/255.f,i/255.f,1};
    BOOST_REQUIRE(frame.Prepare(device,16,16,curve));const auto first=frame.Source_Target();
    BOOST_REQUIRE(frame.Prepare(device,16,16,curve));BOOST_CHECK(frame.Source_Target()==first);
    BOOST_TEST(!frame.Prepare(device,0,16,curve));BOOST_CHECK(frame.Source_Target()==first);
    auto invalid=curve;invalid[5][2]=std::numeric_limits<float>::quiet_NaN();BOOST_TEST(!frame.Prepare(device,16,16,invalid));
    const std::array<std::uint8_t,4> pixel{7,23,171,128};
    const auto texture=device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(pixel)),4});
    const auto target=device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    BOOST_REQUIRE(texture.Is_Valid());BOOST_REQUIRE(target.Is_Valid());BOOST_REQUIRE(depth.Is_Valid());
    std::array<ScreenFilterVertex,4> vertices{};
    vertices[0].position={1,-1,0};vertices[1].position={1,1,0};vertices[2].position={-1,-1,0};vertices[3].position={-1,1,0};
    auto& commands=device.Immediate_Command_List();
    BOOST_TEST(!frame.Draw_Output(renderer,commands,frame.Source_Target(),depth));
    for(unsigned step=0;step<3;++step) {
        if(step==1) for(auto& entry:curve) for(unsigned c=0;c<3;++c) entry[c]=1-entry[c];
        const unsigned extent=step==2 ? 8 : 16;
        BOOST_REQUIRE(frame.Prepare(device,extent,extent,curve));
        if(step==2) BOOST_CHECK(frame.Source_Target()!=first);
        BOOST_REQUIRE(commands.Set_Render_Targets(frame.Source_Target(),depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,extent,extent}));
        BOOST_REQUIRE(renderer.Draw(commands,vertices,{}, {},texture));
        BOOST_REQUIRE(frame.Draw_Output(renderer,commands,target,depth));
        std::array<std::byte,16*16*4> output{};BOOST_REQUIRE(device.Readback_Texture(target,output,16*4));
        for(unsigned c=0;c<4;++c) {
            const int expected=step && c<3 ? 255-pixel[c] : pixel[c];
            BOOST_CHECK_SMALL(std::to_integer<int>(output[(4*16+4)*4+c])-expected,2);
        }
    }
    frame.Shutdown();BOOST_TEST(!frame.Source_Target().Is_Valid());renderer.Shutdown();
    for(const auto resource:{texture,target,depth}) device.Destroy_Texture(resource);
}
BOOST_AUTO_TEST_CASE(color_transfer_covers_every_byte_and_each_channel_without_changing_alpha) {
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());ScreenFilterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    std::array<std::uint8_t,256*4> gradient{};std::array<std::array<float,4>,256> curve{};
    for(unsigned i=0;i<256;++i) {
        gradient[i*4]=i;gradient[i*4+1]=255-i;gradient[i*4+2]=i/2;gradient[i*4+3]=97;
        const float x=i/255.f;curve[i]={x*x,1-x,0.2f+0.5f*x,0};
    }
    const auto source=device.Create_Texture_Initialized({256,1},{std::as_bytes(std::span(gradient)),256*4});BOOST_REQUIRE(source.Is_Valid());
    const auto transfer=device.Create_Texture_Initialized({256,1,1,RHITextureFormat::RGBA32_Float},
        {std::as_bytes(std::span(curve)),256*16});BOOST_REQUIRE(transfer.Is_Valid());
    const auto target=device.Create_Texture({256,4,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({256,4,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    BOOST_REQUIRE(target.Is_Valid());BOOST_REQUIRE(depth.Is_Valid());
    std::array<ScreenFilterVertex,4> vertices{};
    vertices[0].position={1,-1,0};vertices[0].uv={1,1};vertices[1].position={1,1,0};vertices[1].uv={1,0};
    vertices[2].position={-1,-1,0};vertices[2].uv={0,1};vertices[3].position={-1,1,0};vertices[3].uv={0,0};
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,256,4}));
    ScreenFilterParameters parameters;parameters.operation=4;
    BOOST_TEST(!renderer.Draw(commands,vertices,parameters,{},source));
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,{},source,transfer));
    std::array<std::byte,256*4*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,256*4));
    for(unsigned x=0;x<256;++x) {
        const auto at=(2*256+x)*4;
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[at])-int(std::lround(x*x/255.0)),2);
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[at+1])-int(x),2);
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[at+2])-int(std::lround(51+(x/2)*0.5)),2);
        BOOST_TEST(std::to_integer<int>(pixels[at+3])==97);
    }
    renderer.Shutdown();for(const auto texture:{source,transfer,target,depth}) device.Destroy_Texture(texture);
}
BOOST_AUTO_TEST_CASE(copy_tint_crossfade_motion_accumulation_and_capture)
{
    GraphicsTestDevice device({true});
    BOOST_REQUIRE(device.Is_Valid());
    ScreenFilterRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const std::array<std::uint8_t,4> pixel{128,64,32,128},mask_pixel{128,255,255,128};
    const auto texture=device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(pixel)),4});
    const auto mask=device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(mask_pixel)),4});
    const auto target=device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<ScreenFilterVertex,4> vertices{};
    vertices[0].position={1,-1,0}; vertices[1].position={1,1,0};
    vertices[2].position={-1,-1,0}; vertices[3].position={-1,1,0};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    ScreenFilterParameters parameters;
    ScreenFilterStyle style;
    const auto check=[&](std::array<int,4> expected) {
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for(int c=0;c<4;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+c])-expected[c],2);
    };
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture));
    check({128,64,32,128});
    parameters.operation=1;
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture));
    check({80,80,80,80});
    parameters.tint={1,0,0,1}; parameters.fade=0.5f;
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture));
    check({104,32,16,80});
    parameters.operation=2; style.blend=true;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture,mask));
    check({16,16,199,207});
    parameters.operation=0; parameters.vertex_alpha=1;
    for(auto& vertex:vertices) vertex.color[3]=0.5f;
    style.destination=RHIBlendFactor::One;
    BOOST_REQUIRE(commands.Clear({0.25f,0.25f,0.25f,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture));
    check({128,96,80,64});
    style.blend=false; parameters.vertex_alpha=0; parameters.operation=3;
    for(auto& vertex:vertices) vertex.color[3]=1;
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,texture));
    check({32,64,128,128});
    parameters.textured=0; parameters.operation=0; style.color_write_mask=8;
    for(auto& vertex:vertices) vertex.color[3]=0.25f;
    BOOST_REQUIRE(renderer.Draw(commands,vertices,parameters,style,{}));
    check({32,64,128,64});
    renderer.Shutdown();
    for(auto handle:{texture,mask,target,depth}) device.Destroy_Texture(handle);
}

