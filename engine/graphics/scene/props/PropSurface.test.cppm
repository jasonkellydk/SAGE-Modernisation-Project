module;
#define BOOST_TEST_MODULE PropSurfaceTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
#include <limits>
export module Graphics.Scene.Props.Surface.Tests;
import Graphics.Scene.Props.Surface;
import Graphics.Scene.Props.Submission;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Graphics.Scene.Lighting.Environment;
import Graphics.Tests.Device;
import Assets.Materials;
using namespace Graphics;

namespace {
struct Drawing {
    GraphicsTestDevice device{{true}};
    PropRenderer renderer;
    DirectionalShadowRenderer shadows;
    PropSubmission submission;
    std::vector<RHITextureHandle> owned;
    std::array<RHITextureHandle,PropTextureCount> textures{};
    RHITextureHandle target,depth;
    PropMeshHandle mesh;
    PropParameters parameters;
    PropStyle style;
    Drawing() {
        BOOST_REQUIRE(device.Is_Valid());
        const auto directory=Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
        BOOST_REQUIRE(renderer.Initialize(device,directory));
        BOOST_REQUIRE(shadows.Initialize(device,directory));
        submission.Initialize(device,renderer,shadows);
        target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
        depth=device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
        owned.push_back(target); owned.push_back(depth);
        std::array<PropVertex,4> vertices{};
        vertices[0].position={-1,-1,.5f}; vertices[1].position={1,-1,.5f};
        vertices[2].position={1,1,.5f}; vertices[3].position={-1,1,.5f};
        vertices[0].uv={0,0}; vertices[1].uv={1,0}; vertices[2].uv={1,1}; vertices[3].uv={0,1};
        for(auto& vertex:vertices) {
            vertex.material_ambient={1,1,1,1};
            vertex.material_specular={1,1,1,16};
            vertex.tangent={1,0,0,1};
        }
        const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
        mesh=renderer.Create_Mesh(vertices,indices);
        BOOST_REQUIRE(mesh.Is_Valid());
        parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        parameters.camera_position={0,0,100,1};
        parameters.surface.shading_model=1;
        parameters.light_direction[0]={0,0,1,1};
        parameters.light_diffuse[0]={1,1,1,1};
        style.depth_test=style.depth_write=false;
        style.blend=RHIBlendMode::Disabled;
        textures[0]=Texture({255,255,255,255});
    }
    ~Drawing() {
        submission.Shutdown(); shadows.Shutdown(); renderer.Destroy_Mesh(mesh); renderer.Shutdown();
        for(auto texture:owned) device.Destroy_Texture(texture);
    }
    RHITextureHandle Texture(std::array<std::uint8_t,4> color) {
        const auto texture=device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(color)),4});
        BOOST_REQUIRE(texture.Is_Valid()); owned.push_back(texture); return texture;
    }
    void Clear() {
        auto& commands=device.Immediate_Command_List();
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
        BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    }
    std::array<int,4> Pixel() {
        std::array<std::byte,8*8*4> bytes{};
        BOOST_REQUIRE(device.Readback_Texture(target,bytes,8*4));
        const auto offset=(4*8+4)*4;
        return {std::to_integer<int>(bytes[offset]),std::to_integer<int>(bytes[offset+1]),
            std::to_integer<int>(bytes[offset+2]),std::to_integer<int>(bytes[offset+3])};
    }
    std::array<int,4> Draw() {
        Clear();
        BOOST_REQUIRE(renderer.Draw(device.Immediate_Command_List(),mesh,style,parameters,textures));
        return Pixel();
    }
};
}

BOOST_AUTO_TEST_CASE(configuration_preserves_legacy_defaults_and_rejects_invalid_surface_data)
{
    Assets::MaterialSurfaceParameters source;
    PropSurfaceParameters output;
    BOOST_REQUIRE(Configure_Prop_Surface(source,0,output));
    BOOST_TEST(output.shading_model==0);
    source.shading_model=Assets::MaterialShadingModel::MetallicRoughness;
    source.specular_channel=Assets::MaterialTextureChannel::Red;
    source.team_color_channel=Assets::MaterialTextureChannel::Blue;
    BOOST_REQUIRE(Configure_Prop_Surface(source,65,output));
    BOOST_TEST(output.shading_model==2); BOOST_TEST(output.maps==65u);
    BOOST_TEST(output.specular_channel==0u); BOOST_TEST(output.team_color_channel==2u);
    source.roughness=std::numeric_limits<float>::quiet_NaN();
    BOOST_TEST(!Configure_Prop_Surface(source,0,output));
    BOOST_TEST(output.maps==65u);
    source={}; BOOST_TEST(!Configure_Prop_Surface(source,512,output));
}

BOOST_AUTO_TEST_CASE(normal_maps_follow_tangents_green_convention_and_instance_rotation)
{
    Drawing draw;
    draw.parameters.light_direction[0]={1,0,0,1};
    draw.parameters.surface.maps=1;
    draw.textures[4]=draw.Texture({218,128,218,255});
    const auto tilted=draw.Draw();
    draw.parameters.surface.normal_scale=0;
    const auto flat=draw.Draw();
    BOOST_TEST(tilted[0]>flat[0]+100);
    draw.parameters.surface.normal_scale=1;
    draw.parameters.world={0,-1,0,0,1,0,0,0,0,0,1,0,0,0,0,1};
    draw.parameters.light_direction[0]={0,1,0,1};
    const auto rotated=draw.Draw();
    BOOST_CHECK_SMALL(rotated[0]-tilted[0],3);
    draw.parameters.world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    draw.textures[4]=draw.Texture({128,218,218,255});
    const auto green=draw.Draw();
    draw.parameters.surface.normal_flip_green=1;
    const auto flipped=draw.Draw();
    BOOST_TEST(green[0]>flipped[0]+100);
    draw.textures[4]={}; draw.Clear();
    BOOST_TEST(!draw.renderer.Draw(draw.device.Immediate_Command_List(),draw.mesh,draw.style,draw.parameters,draw.textures));
}

BOOST_AUTO_TEST_CASE(vertex_alpha_texture_offset_moves_sampling_without_fading_the_surface)
{
    Drawing draw;
    const std::array<std::uint8_t,8> texels{255,0,0,255,0,255,0,255};
    const auto texture=draw.device.Create_Texture_Initialized({2,1},{std::as_bytes(std::span(texels)),8});
    BOOST_REQUIRE(texture.Is_Valid());draw.owned.push_back(texture);draw.textures[0]=texture;
    std::array<PropVertex,4> vertices{};
    vertices[0].position={-1,-1,.5f};vertices[1].position={1,-1,.5f};
    vertices[2].position={1,1,.5f};vertices[3].position={-1,1,.5f};
    for(auto& vertex:vertices) {vertex.uv={.25f,.5f};vertex.color={1,1,1,.5f};vertex.tangent={1,0,0,1};}
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    BOOST_REQUIRE(draw.renderer.Update_Mesh(draw.mesh,vertices,indices));
    draw.parameters.textured=1;
    const auto ordinary=draw.Draw();
    Assets::MaterialSurfaceParameters source;source.shading_model=Assets::MaterialShadingModel::SpecularGlossiness;
    source.uv_offset_from_vertex_alpha=true;
    BOOST_REQUIRE(Configure_Prop_Surface(source,0,draw.parameters.surface));
    const auto shifted=draw.Draw();
    BOOST_TEST(ordinary[0]>200);BOOST_TEST(ordinary[1]<10);
    BOOST_CHECK_SMALL(ordinary[3]-128,2);
    BOOST_TEST(shifted[0]<10);BOOST_TEST(shifted[1]>200);BOOST_TEST(shifted[3]==255);
}

BOOST_AUTO_TEST_CASE(specular_and_team_channels_do_not_tint_each_other)
{
    Drawing draw;
    draw.parameters.surface.maps=2;
    draw.parameters.surface.specular_channel=0;
    draw.parameters.light_diffuse[0]={};
    draw.parameters.light_specular[0]={.25f,.25f,.25f,1};
    draw.textures[5]=draw.Texture({255,0,0,255});
    const auto highlight=draw.Draw();
    BOOST_TEST(highlight[0]>100);
    BOOST_CHECK_SMALL(highlight[0]-highlight[2],2);
    draw.textures[5]=draw.Texture({0,0,255,255});
    const auto no_highlight=draw.Draw();
    BOOST_TEST(no_highlight[0]<3);
    draw.parameters.light_diffuse[0]={1,1,1,1}; draw.parameters.light_specular[0]={};
    draw.parameters.surface.maps=64;
    draw.parameters.surface.team_color_channel=2;
    draw.parameters.surface.team_color_multiplier=2;
    draw.parameters.surface.team_color={1,0,0,1};
    draw.textures[10]=draw.textures[5];
    const auto red=draw.Draw();
    BOOST_TEST(red[0]>250); BOOST_TEST(red[1]<3); BOOST_TEST(red[2]<3);
    draw.parameters.surface.team_color[3]=0;
    const auto untinted=draw.Draw(); BOOST_TEST(untinted[1]>250);
}

BOOST_AUTO_TEST_CASE(emission_occlusion_alpha_and_pbr_roughness_have_visible_effects)
{
    Drawing draw;
    draw.parameters.light_direction[0][3]=0;
    draw.parameters.surface.maps=4;
    draw.textures[6]=draw.Texture({0,128,0,255});
    const auto emissive=draw.Draw(); BOOST_CHECK_SMALL(emissive[1]-128,2); BOOST_TEST(emissive[0]<3);
    draw.parameters.surface.maps=32;
    draw.parameters.scene_ambient={1,1,1,1};
    draw.textures[9]=draw.Texture({0,0,0,255});
    const auto occluded=draw.Draw(); BOOST_TEST(occluded[0]<3);
    draw.parameters.surface.occlusion_strength=0;
    const auto ambient=draw.Draw(); BOOST_TEST(ambient[0]>250);
    draw.textures[0]=draw.Texture({255,255,255,96});
    draw.parameters.alpha_cutoff=96.f/255;
    BOOST_CHECK_SMALL(draw.Draw()[3]-96,2);
    draw.parameters.alpha_cutoff=97.f/255;
    BOOST_TEST(draw.Draw()[3]==0);
    draw.parameters.alpha_cutoff=0; draw.parameters.scene_ambient={};
    draw.parameters.light_direction[0][3]=1;
    draw.parameters.surface.shading_model=2; draw.parameters.surface.maps=8;
    draw.parameters.surface.metallic=1; draw.parameters.surface.roughness=1;
    draw.textures[7]=draw.Texture({32,32,32,255});
    const auto smooth=draw.Draw();
    draw.textures[7]=draw.Texture({255,255,255,255});
    const auto rough=draw.Draw();
    BOOST_TEST(smooth[0]>rough[0]+50);
}

BOOST_AUTO_TEST_CASE(deferred_phases_keep_surface_slots_and_snapshot_instance_parameters)
{
    for(auto phase:{PropDrawPhase::Material,PropDrawPhase::Transparent}) {
        Drawing draw;
        draw.parameters.surface.maps=64;
        draw.parameters.surface.team_color_channel=2;
        draw.parameters.surface.team_color={0,1,0,1};
        draw.textures[10]=draw.Texture({0,0,255,255});
        draw.Clear();
        for(auto texture:draw.textures) if(texture.Is_Valid()) BOOST_REQUIRE(draw.device.Retain_Texture(texture));
        BOOST_REQUIRE(draw.submission.Submit(draw.mesh,draw.style,draw.parameters,draw.textures,phase));
        draw.parameters.surface.team_color={1,0,0,1};
        for(auto texture:draw.owned) if(texture!=draw.target && texture!=draw.depth) BOOST_REQUIRE(draw.device.Destroy_Texture(texture));
        draw.owned={draw.target,draw.depth};
        BOOST_REQUIRE(draw.submission.Flush_Transparent());
        const auto pixel=draw.Pixel();
        BOOST_TEST(pixel[0]<3); BOOST_TEST(pixel[1]>250); BOOST_TEST(pixel[2]<3);
        BOOST_TEST(!draw.device.Retain_Texture(draw.textures[10]));
    }
}

BOOST_AUTO_TEST_CASE(surface_casters_keep_extended_maps_and_alpha_cutout_in_shadow_depth)
{
    struct Reset { ~Reset() { Get_Environment_Lighting()={}; } } reset;
    Get_Environment_Lighting()={};
    Drawing draw;
    const std::array<std::uint8_t,8> pixels{255,255,255,255,255,255,255,0};
    const auto cutout=draw.device.Create_Texture_Initialized({2,1},{std::as_bytes(std::span(pixels)),8});
    draw.owned.push_back(cutout); draw.textures[0]=cutout;
    draw.textures[10]=draw.Texture({0,0,255,255});
    draw.parameters.surface.maps=64;
    draw.parameters.surface.team_color={0,1,0,1};
    draw.parameters.alpha_cutoff=.5f;
    draw.parameters.world[11]=-4.5f;
    for(auto texture:draw.textures) if(texture.Is_Valid()) BOOST_REQUIRE(draw.device.Retain_Texture(texture));
    BOOST_REQUIRE(draw.submission.Submit(draw.mesh,draw.style,draw.parameters,draw.textures,PropDrawPhase::Shadow));
    for(auto texture:draw.owned) if(texture!=draw.target && texture!=draw.depth) draw.device.Destroy_Texture(texture);
    draw.owned={draw.target,draw.depth};
    auto projection=Matrix4x4::Identity(); projection.values[10]=-1.f/9; projection.values[11]=-1.f/9;
    const View view{Matrix4x4::Identity(),projection,{}, {0,0,8,8,0,1}};
    RenderLight light; light.type=RenderLightType::Directional; light.flags=RenderLightFlags::Enabled; light.direction={0,0,-1};
    auto& commands=draw.device.Immediate_Command_List();
    BOOST_REQUIRE(draw.shadows.Render(commands,view,light,ShadowSettings{2,1,10,.5f,2,128},draw.target,draw.depth,{0,0,8,8}));
    draw.parameters.view_projection=projection.values;
    draw.parameters.world[11]=-5.5f;
    draw.parameters.surface.maps=0; draw.parameters.textured=0; draw.parameters.alpha_cutoff=0;
    draw.parameters.scene_ambient={.2f,.2f,.2f,1}; draw.parameters.light_diffuse[0]={.8f,.8f,.8f,1};
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(draw.renderer.Draw(commands,draw.mesh,draw.style,draw.parameters,{}));
    std::array<std::byte,8*8*4> result{}; BOOST_REQUIRE(draw.device.Readback_Texture(draw.target,result,8*4));
    BOOST_TEST(std::to_integer<int>(result[(4*8+1)*4])+60<std::to_integer<int>(result[(4*8+6)*4]));
    draw.submission.Clear_Shadows();
    BOOST_TEST(!draw.device.Retain_Texture(draw.textures[10]));
}

BOOST_AUTO_TEST_CASE(height_map_changes_surface_shading_and_uses_the_spare_texture_slot)
{
    Drawing draw;
    draw.parameters.surface.shading_model=2;
    draw.parameters.surface.maps=128;
    draw.parameters.surface.height[0]=4;
    draw.parameters.surface.roughness=.8f;
    draw.parameters.light_diffuse[0]={.5f,.5f,.5f,1};
    std::array<std::uint8_t,4*4*4> pixels{};
    for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x) {
        const unsigned offset=(y*4+x)*4;
        pixels[offset]=pixels[offset+1]=pixels[offset+2]=static_cast<std::uint8_t>(x*85);
        pixels[offset+3]=255;
    }
    const auto height=draw.device.Create_Texture_Initialized({4,4},{std::as_bytes(std::span(pixels)),16});
    BOOST_REQUIRE(height.Is_Valid()); draw.owned.push_back(height);
    draw.textures[Prop_Surface_Texture_Slot(7)]=height;
    const auto bumped=draw.Draw();
    draw.parameters.surface.height[0]=0;
    const auto flat=draw.Draw();
    BOOST_TEST(flat[0]>bumped[0]+10);
    BOOST_TEST(flat[3]==bumped[3]);
}

BOOST_AUTO_TEST_CASE(modern_metals_reflect_the_environment_without_direct_lights)
{
    struct Reset { ~Reset() { Get_Environment_Lighting()={}; } } reset;
    Get_Environment_Lighting()={};
    auto& environment=Get_Environment_Lighting().parameters;
    environment.pbr_options[0]=1;
    environment.sky_radiance={.05f,.2f,1,0}; environment.ground_radiance={};
    Drawing draw;
    draw.parameters.textured=1;
    draw.textures[0]=draw.Texture({180,180,180,255});
    draw.parameters.surface.shading_model=2; draw.parameters.surface.metallic=1;
    draw.parameters.surface.roughness=.35f; draw.parameters.light_direction[0]={};
    const auto sky=draw.Draw();
    BOOST_TEST(sky[2]>sky[0]+20);
    environment.sky_radiance={};
    const auto dark=draw.Draw();
    BOOST_TEST(dark[0]<3); BOOST_TEST(dark[1]<3); BOOST_TEST(dark[2]<3);
}

BOOST_AUTO_TEST_CASE(parallax_moves_texture_detail_with_view_direction_and_preserves_alpha)
{
    Drawing draw;
    std::array<PropVertex,4> vertices{};
    constexpr float x[]{-1,1,1,-1}, y[]{-1,-1,1,1};
    for(unsigned i=0;i<4;++i) {
        vertices[i].position={x[i],y[i],.5f};
        vertices[i].uv={.45f+x[i]*.05f,.45f+y[i]*.05f};
        vertices[i].material_ambient={1,1,1,1};
        vertices[i].tangent={1,0,0,1};
    }
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    BOOST_REQUIRE(draw.renderer.Update_Mesh(draw.mesh,vertices,indices));
    std::array<std::uint8_t,64*4> pixels{};
    for(unsigned x=0;x<64;++x) {
        pixels[x*4]=pixels[x*4+1]=pixels[x*4+2]=static_cast<std::uint8_t>(x*4);
        pixels[x*4+3]=180;
    }
    const auto base=draw.device.Create_Texture_Initialized({64,1},{std::as_bytes(std::span(pixels)),256});
    BOOST_REQUIRE(base.Is_Valid()); draw.owned.push_back(base);draw.textures[0]=base;
    draw.textures[2]=draw.Texture({0,0,0,255});
    draw.parameters.textured=1; draw.parameters.surface.maps=128;
    draw.parameters.surface.height={4,0,0,0};
    draw.parameters.light_direction[0]={}; draw.parameters.scene_ambient={1,1,1,1};
    draw.parameters.camera_position={100,0,10,1};
    const auto flat=draw.Draw();
    draw.parameters.surface.height[2]=1;
    const auto right=draw.Draw();
    draw.parameters.camera_position[0]=-100;
    const auto left=draw.Draw();
    BOOST_TEST(right[0]+5<flat[0]); BOOST_TEST(left[0]>flat[0]+5);
    BOOST_TEST(right[3]==flat[3]); BOOST_TEST(left[3]==flat[3]);
}

BOOST_AUTO_TEST_CASE(shared_point_and_spot_lights_preserve_animated_color)
{
    struct Reset { ~Reset() { Get_Environment_Lighting()={}; } } reset;
    Get_Environment_Lighting()={};
    auto& env=Get_Environment_Lighting().parameters;
    env.pbr_options[0]=1;env.sky_radiance={};env.ground_radiance={};
    env.local_light_options[0]=1;
    env.local_positions[0]={0,0,3,10};env.local_diffuse[0]={3,0,0,5};
    env.local_direction[0]={0,0,-1,-1};
    Drawing draw;draw.parameters.surface.shading_model=2;
    const auto red=draw.Draw();
    BOOST_TEST(red[0]>80);BOOST_TEST(red[1]<3);BOOST_TEST(red[2]<3);
    env.local_diffuse[0]={0,0,3,5};
    const auto blue=draw.Draw();
    BOOST_TEST(blue[2]>80);BOOST_TEST(blue[0]<3);BOOST_TEST(blue[1]<3);
    env.local_direction[0]={0,0,1,.8f};env.local_spot[0]={.95f,1,0,0};
    const auto outside_cone=draw.Draw();BOOST_TEST(outside_cone[2]<3);
    env.local_direction[0][2]=-1;
    const auto inside_cone=draw.Draw();BOOST_TEST(inside_cone[2]>80);
}

BOOST_AUTO_TEST_CASE(material_attachments_preserve_reflectance_normal_and_indirect_radiance)
{
    struct Reset {~Reset(){Get_Environment_Lighting()={};}} reset;
    Get_Environment_Lighting()={};
    auto& env=Get_Environment_Lighting().parameters;
    env.pbr_options={1,1,1,1};env.sky_radiance={.2f,.2f,.2f,0};env.ground_radiance=env.sky_radiance;
    env.sun_direction={0,0,1,0};env.sun_radiance={1,1,1,0};
    Drawing draw;draw.parameters.surface.shading_model=2;draw.parameters.surface.metallic=0;
    std::array<RHITextureHandle,4> targets{draw.target};
    for(unsigned i=1;i<4;++i) {
        targets[i]=draw.device.Create_Texture({8,8,1,RHITextureFormat::RGBA32_Float,
            static_cast<unsigned>(RHITextureUsage::RenderTarget)});
        BOOST_REQUIRE(targets[i].Is_Valid());draw.owned.push_back(targets[i]);
    }
    draw.Clear();auto& commands=draw.device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Color_Targets(targets,draw.depth));
    // A failed attachment bind must leave the previous, valid set usable.
    const std::array duplicate{targets[0],targets[0]};
    BOOST_CHECK(!commands.Set_Color_Targets(duplicate,draw.depth));
    BOOST_REQUIRE(draw.renderer.Draw(commands,draw.mesh,draw.style,draw.parameters,draw.textures));
    std::array<float,8*8*4> pixels{};
    const unsigned offset=(4*8+4)*4;
    BOOST_REQUIRE(draw.device.Readback_Texture(targets[1],std::as_writable_bytes(std::span(pixels)),8*16));
    BOOST_CHECK_CLOSE(pixels[offset],.96f,.5f);
    BOOST_REQUIRE(draw.device.Readback_Texture(targets[2],std::as_writable_bytes(std::span(pixels)),8*16));
    BOOST_TEST(pixels[offset+2]>=.5f);BOOST_CHECK_SMALL(pixels[offset],.001f);
    BOOST_REQUIRE(draw.device.Readback_Texture(targets[3],std::as_writable_bytes(std::span(pixels)),8*16));
    BOOST_TEST(pixels[offset]>.01f);
    const float indirect=pixels[offset];
    env.sun_radiance={10,0,0,0};
    BOOST_REQUIRE(commands.Set_Color_Targets(targets,draw.depth));
    BOOST_REQUIRE(draw.renderer.Draw(commands,draw.mesh,draw.style,draw.parameters,draw.textures));
    BOOST_REQUIRE(draw.device.Readback_Texture(targets[3],std::as_writable_bytes(std::span(pixels)),8*16));
    BOOST_CHECK_CLOSE(pixels[offset],indirect,.01f);
}

BOOST_AUTO_TEST_CASE(legacy_emissive_lamps_preserve_base_texture_color)
{
    struct Reset { ~Reset(){Get_Environment_Lighting()={};} } reset;
    Get_Environment_Lighting()={};
    auto& env=Get_Environment_Lighting().parameters;
    env.pbr_options={1,1,1,1};env.sky_radiance={};env.ground_radiance={};
    Drawing draw;
    std::array<PropVertex,4> vertices{};
    vertices[0].position={-1,-1,.5f};vertices[1].position={1,-1,.5f};
    vertices[2].position={1,1,.5f};vertices[3].position={-1,1,.5f};
    for(auto& vertex:vertices) {
        vertex.material_ambient={1,1,1,1};vertex.material_emissive={1,1,1,1};
        vertex.normal={0,0,1};vertex.tangent={1,0,0,1};
    }
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    BOOST_REQUIRE(draw.renderer.Update_Mesh(draw.mesh,vertices,indices));
    const auto material=Assets::Upgrade_Legacy_Surface("colored lamp");
    BOOST_REQUIRE(Configure_Prop_Surface(material,0,draw.parameters.surface));
    for(unsigned channel:{0u,2u}) {
        std::array<std::uint8_t,4> color{0,0,0,255};color[channel]=255;
        draw.textures[0]=draw.Texture(color);
        const auto pixel=draw.Draw();
        BOOST_TEST(pixel[channel]>245);BOOST_TEST(pixel[1]<3);BOOST_TEST(pixel[2-channel]<3);
    }
    // Explicit PBR emission retains its independent color contract.
    draw.parameters.surface.maps=0;
    const auto independent=draw.Draw();
    BOOST_TEST(independent[0]>245);BOOST_TEST(independent[1]>245);BOOST_TEST(independent[2]>245);
}

BOOST_AUTO_TEST_CASE(shared_local_shadows_block_point_and_spot_lights_and_clear_removed_casters)
{
    struct Reset { ~Reset(){Get_Environment_Lighting()={};} } reset;
    Get_Environment_Lighting()={};
    auto& env=Get_Environment_Lighting().parameters;
    env.pbr_options={1,0,1,1};env.sky_radiance={};env.ground_radiance={};
    env.local_light_options[0]=1;env.local_positions[0]={0,0,3,10};
    env.local_diffuse[0]={0,0,3,0};env.local_direction[0]={0,0,-1,-1};
    Drawing draw;draw.parameters.surface.shading_model=2;
    std::array<PropVertex,4> blocker{};
    blocker[0].position={-.6f,-.6f,1.5f};blocker[1].position={.6f,-.6f,1.5f};
    blocker[2].position={.6f,.6f,1.5f};blocker[3].position={-.6f,.6f,1.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    PropParameters caster;caster.textured=0;
    PropStyle caster_style;caster_style.cull=RHICullMode::None;
    auto& commands=draw.device.Immediate_Command_List();
    for(bool spot:{false,true}) {
        env.local_direction[0][3]=spot ? .8f : -1.f;
        env.local_spot[0]={.95f,1,0,0};
        draw.shadows.Clear_Casters();
        BOOST_REQUIRE(draw.shadows.Render_Local(commands,draw.target,draw.depth,{0,0,8,8}));
        BOOST_TEST(draw.Draw()[2]>80);
        BOOST_REQUIRE(draw.shadows.Add_Caster(blocker,indices,caster,{},caster_style));
        BOOST_REQUIRE(draw.shadows.Render_Local(commands,draw.target,draw.depth,{0,0,8,8}));
        BOOST_TEST(draw.Draw()[2]<3);
        draw.shadows.Clear_Casters();
        BOOST_REQUIRE(draw.shadows.Render_Local(commands,draw.target,draw.depth,{0,0,8,8}));
        BOOST_TEST(draw.Draw()[2]>80);
    }
}
