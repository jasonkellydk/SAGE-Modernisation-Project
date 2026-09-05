module;
#define BOOST_TEST_MODULE PropDrawingTests
#include <boost/test/included/unit_test.hpp>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
export module Graphics.Scene.Props.Drawing.Tests;
import Graphics.Backends.DX11;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Props.Material;
import Graphics.Scene.Lighting.Environment;
using namespace Graphics;

import Graphics.Scene.Props.Lighting;
BOOST_AUTO_TEST_CASE(vertex_validation_preserves_binary32_finite_boundaries)
{
    PropGeometry geometry;
    const std::array<std::uint32_t,3> indices{0,1,2};
    std::array<PropVertex,3> vertices{};
    BOOST_REQUIRE(geometry.Assign(vertices,indices));
    for (const std::uint32_t bits : {0u,0x80000000u,1u,0x80000001u,
        0x007fffffu,0x00800000u,0x7f7fffffu,0xff7fffffu,
        0x7f800000u,0xff800000u,0x7fc00000u,0xffc00000u,0x7f800001u}) {
        const float value = std::bit_cast<float>(bits);
        const bool finite = bits != 0x7f800000u && bits != 0xff800000u
            && bits != 0x7fc00000u && bits != 0xffc00000u && bits != 0x7f800001u;
        const auto check = [&](auto member) {
            for (unsigned component=0;component<(vertices[0].*member).size();++component) {
                vertices = {};
                (vertices[0].*member)[component] = value;
                BOOST_CHECK_EQUAL(geometry.Assign(vertices,indices),finite);
            }
        };
        check(&PropVertex::position); check(&PropVertex::color);
        check(&PropVertex::uv); check(&PropVertex::secondary_uv);
        check(&PropVertex::normal); check(&PropVertex::secondary_color);
        check(&PropVertex::material_ambient); check(&PropVertex::material_diffuse);
        check(&PropVertex::material_emissive); check(&PropVertex::material_specular);
    }
}

BOOST_AUTO_TEST_CASE(cloud_projection_uses_height_and_preserves_ambient_emissive_and_local_lights)
{
    struct ResetEnvironment final {
        ~ResetEnvironment() { Get_Environment_Lighting() = {}; }
    } reset;
    Get_Environment_Lighting() = {};
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const std::array<std::uint8_t,8> pattern{0,0,0,255,255,255,255,255};
    const auto cloud = device.Create_Texture_Initialized({2,1},{std::as_bytes(std::span(pattern)),8});
    auto& environment = Get_Environment_Lighting();
    environment.cloud_texture = cloud;
    environment.parameters.cloud_multiplier = {0,0,0.5f,0};
    environment.parameters.cloud_offset_strength = {0.5f,0.5f,1,1};
    const auto target = device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<PropVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    for (auto& vertex : vertices) {
        vertex.normal = {0,0,1};
        vertex.material_ambient = {1,1,1,1};
        vertex.material_diffuse = {1,1,1,1};
        vertex.material_emissive = {0.1f,0.1f,0.1f,0};
    }
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    PropParameters parameters;
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,0,0.5f,0,0,0,1};
    parameters.textured = 0;
    parameters.camera_position = {0,0,10,1};
    parameters.scene_ambient = {0.2f,0.2f,0.2f,0};
    parameters.light_direction[0] = {0,0,1,1};
    parameters.light_diffuse[0] = {0.6f,0.6f,0.6f,0};
    PropStyle style;
    style.blend = RHIBlendMode::Disabled;
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    const auto draw_and_check = [&](int expected) {
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
        BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for (int channel=0;channel<3;++channel)
            BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+channel])-expected,2);
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(8*16+8)*4+3]),255);
    };
    draw_and_check(77); // Black cloud: ambient and emissive remain.
    for (auto& vertex : vertices) vertex.position[2] = 1.5f;
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    draw_and_check(230); // Same XY, different height: the white cloud texel.
    environment.parameters.cloud_multiplier = {};
    environment.parameters.cloud_offset_strength[0] = 0.25f;
    parameters.light_position[0] = {0,0,1000000,1};
    parameters.light_attenuation[0] = {1,0,0,0};
    draw_and_check(230); // Local lighting does not receive sunlight's clouds.
    environment.parameters.clip_plane = {0,0,1,-2};
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    std::array<std::byte,16*16*4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(8*16+8)*4+2]),255);
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(8*16+8)*4]),0);
    Get_Environment_Lighting() = {};
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (const auto texture : {cloud,target,depth}) device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(shadow_depth_silhouette_attenuates_directional_light_on_receivers)
{
    struct ResetEnvironment final {
        ~ResetEnvironment() { Get_Environment_Lighting() = {}; }
    } reset;
    Get_Environment_Lighting() = {};
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target = device.Create_Texture({32,32,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({32,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    const auto shadow = device.Create_Texture({32,32,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil) |
        static_cast<std::uint32_t>(RHITextureUsage::ShaderResource)});
    std::array<PropVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.25f}; vertices[1].position = {0,-1,0.25f};
    vertices[2].position = {0,1,0.25f}; vertices[3].position = {-1,1,0.25f};
    for (auto& vertex : vertices) {
        vertex.normal = {0,0,1};
        vertex.material_ambient = {1,1,1,1};
        vertex.material_diffuse = {1,1,1,1};
        vertex.material_emissive = {0.1f,0.1f,0.1f,0};
    }
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    PropParameters parameters;
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.textured = 0;
    parameters.camera_position = {0,0,1000000,1};
    parameters.scene_ambient = {0.2f,0.2f,0.2f,0};
    parameters.light_direction[0] = {0,0,1,1};
    parameters.light_diffuse[0] = {0.6f,0.6f,0.6f,0};
    PropStyle style;
    style.blend = RHIBlendMode::Disabled;
    style.color_write_mask = 0;
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Depth_Target(shadow));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,32,32}));
    BOOST_REQUIRE(commands.Clear_Depth(1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));

    // Draw a full receiver behind the left-half caster into another target.
    for (auto& vertex : vertices) vertex.position[2] = 0.75f;
    vertices[1].position[0] = vertices[2].position[0] = 1;
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    auto& environment = Get_Environment_Lighting();
    environment.shadow_textures[0] = shadow;
    environment.parameters.shadow_view_projection[0] = parameters.view_projection;
    environment.parameters.shadow_splits = {10,10,10,10};
    environment.parameters.shadow_view_depth = {0,0,1,0};
    environment.parameters.shadow_options = {1,0.0001f,0,0.1f};
    style.color_write_mask = 15;
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    std::array<std::byte,32*32*4> pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
    for (int channel=0;channel<3;++channel) {
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(16*32+8)*4+channel])-77,2);
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(16*32+24)*4+channel])-230,2);
        const int edge = std::to_integer<int>(pixels[(16*32+15)*4+channel]);
        BOOST_CHECK_GT(edge,77);
        BOOST_CHECK_LT(edge,230);
    }
    Get_Environment_Lighting() = {};
    renderer.Destroy_Mesh(mesh);
    renderer.Shutdown();
    for (const auto texture : {target,depth,shadow}) device.Destroy_Texture(texture);
}

BOOST_AUTO_TEST_CASE(detail_materials_shroud_depth_and_background_drawing)
{
    DX11Device device({true});
    BOOST_REQUIRE(device.Is_Valid());
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto texture = [&](std::array<std::uint8_t,4> color) {
        return device.Create_Texture_Initialized({1,1},{std::as_bytes(std::span(color)),4});
    };
    const auto base = texture({128,64,32,255});
    const auto detail = texture({128,128,128,128});
    const auto shroud = texture({128,128,128,255});
    const auto target = device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth = device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    std::array<PropVertex,4> vertices{};
    vertices[0].position = {-1,-1,0.5f}; vertices[1].position = {1,-1,0.5f};
    vertices[2].position = {1,1,0.5f}; vertices[3].position = {-1,1,0.5f};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh = renderer.Create_Mesh(vertices,indices);
    PropParameters parameters;
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.secondary_texture = 1;
    PropStyle style;
    style.cull = RHICullMode::None;
    const std::array<RHITextureHandle,4> textures{base,detail,{},shroud};
    auto& commands = device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    const auto check = [&](std::array<int,4> expected) {
        std::array<std::byte,16*16*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        for(int c=0;c<4;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(8*16+8)*4+c])-expected[c],2);
    };
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    parameters.detail_color = 2;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({64,32,16,255});
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,true));
    check({32,16,8,255});
    parameters.detail_color = 4;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({255,192,160,255});
    parameters.detail_color = 6;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({0,64,96,255});
    parameters.detail_color = 11;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({128,64,32,255});
    parameters.detail_alpha = 2;
    parameters.alpha_cutoff = 0.75f;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,true));
    check({0,0,255,255});
    parameters.alpha_cutoff = 0;
    style.depth_test = false; style.depth_write = false;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},0));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({128,64,32,128});
    // Interpolate normals before lighting. A center normal between opposite
    // diagonal directions must still face the light with full intensity.
    for (unsigned i=0;i<vertices.size();++i) {
        vertices[i].normal = {i==0 || i==3 ? -0.8f : 0.8f,0,0.6f};
        vertices[i].material_ambient = {1,1,1,1};
        vertices[i].material_diffuse = {1,1,1,0.4f};
    }
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    parameters = {};
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.camera_position = {0,0,10,1};
    parameters.textured = 0;
    parameters.light_direction[0] = {0,0,1,1};
    parameters.light_diffuse[0] = {0.5f,0.5f,0.5f,0};
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({127,127,127,102});
    // Emissive effects use material opacity even when their color channel
    // carries an alpha-vector mask. Zero diffuse/ambient must not blacken RGB.
    const auto lit_vertices=vertices;
    for (auto& vertex : vertices) {
        vertex.color={0.1f,0.8f,0.3f,0.8f};
        vertex.material_diffuse={0,0,0,0.25f};
        vertex.material_ambient={0,0,0,1};
        vertex.material_emissive={0.4f,0.2f,0.1f,0};
        vertex.material_specular={0,0,0,0};
    }
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({102,51,26,64});
    style.source_blend=RHIBlendFactor::SourceAlpha;
    style.destination_blend=RHIBlendFactor::InverseSourceAlpha;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({26,13,198,207});
    style.source_blend=RHIBlendFactor::One;
    style.destination_blend=RHIBlendFactor::One;
    BOOST_REQUIRE(commands.Clear({0.1f,0.1f,0.1f,0},1));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,textures,false));
    check({128,77,51,64});
    style.destination_blend=RHIBlendFactor::Zero;
    vertices=lit_vertices;
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    // A translated texture matrix must choose the other texel without changing
    // geometry. This covers scrolling/stepped material mapper uniforms.
    const std::array<std::uint8_t,8> texels{255,0,0,255,0,255,0,255};
    const auto mapped_texture = device.Create_Texture_Initialized({2,1},{std::as_bytes(std::span(texels)),8});
    BOOST_REQUIRE(mapped_texture.Is_Valid());
    parameters = {};
    parameters.view_projection = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.primary_gradient = 0;
    parameters.uv_transform[0][3] = 0.25f;
    style.samplers[0].linear_filter = false;
    auto mapped_textures = textures;
    mapped_textures[0] = mapped_texture;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,mapped_textures,false));
    check({255,0,0,255});
    parameters.uv_transform[0][3] = 0.75f;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,mapped_textures,false));
    check({0,255,0,255});
    const std::array<std::uint8_t,16> grid{
        255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    const auto grid_texture=device.Create_Texture_Initialized({2,2},{std::as_bytes(std::span(grid)),8});
    BOOST_REQUIRE(grid_texture.Is_Valid());
    mapped_textures[0]=grid_texture;
    parameters.uv_transform[0][3]=1.25f;
    parameters.uv_transform[0][7]=1.25f;
    style.samplers[0].address[1]=RHISamplerAddress::Clamp;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,mapped_textures,false));
    check({0,0,255,255});
    style.samplers[0].address[0]=RHISamplerAddress::Clamp;
    style.samplers[0].address[1]=RHISamplerAddress::Wrap;
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,mapped_textures,false));
    check({0,255,0,255});
    for(auto& vertex:vertices) {
        const auto position=vertex.position;
        vertex=PropVertex{}; vertex.position=position;
    }
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    parameters=PropParameters{}; parameters.textured=0;
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    style.cull=RHICullMode::Back; style.front_counter_clockwise=true;
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,{},false));
    check({255,255,255,255});
    parameters.view_projection[0]=-1;
    style.front_counter_clockwise=false;
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
    BOOST_REQUIRE(Draw_Prop(renderer,commands,mesh,style,parameters,{},false));
    check({255,255,255,255});
    // Screen material luminance ignores RGB tint but retains modulated alpha.
    for (auto& vertex : vertices) vertex.color={0.2f,0.7f,0.1f,0.5f};
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    parameters=PropParameters{};
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.texture_luminance=1;
    style=PropStyle{};
    BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,std::span(textures).first(1)));
    check({80,80,80,128});
    // Decal passes inherit a negative depth bias so coplanar geometry survives
    // a strict depth comparison. A positive bias must remain occluded.
    for (auto& vertex : vertices) vertex.color={1,0,0,1};
    BOOST_REQUIRE(renderer.Update_Mesh(mesh,vertices,indices));
    parameters=PropParameters{}; parameters.textured=0;
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    style=PropStyle{}; style.cull=RHICullMode::None;
    style.depth_comparison=RHIComparison::Less;
    BOOST_REQUIRE(commands.Clear({0,0,1,1},0.5f));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    check({0,0,255,255});
    style.depth_bias=-16;
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    check({255,0,0,255});
    BOOST_REQUIRE(commands.Clear({0,0,1,1},0.5f));
    style.depth_bias=16;
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    check({0,0,255,255});
    // Wireframe must leave triangle interiors untouched while drawing edges.
    style.depth_bias=0; style.depth_test=false; style.depth_write=false;
    style.wireframe=true;
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
    BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
    std::array<std::byte,16*16*4> wire_pixels{};
    BOOST_REQUIRE(device.Readback_Texture(target,wire_pixels,16*4));
    BOOST_CHECK_EQUAL(std::to_integer<int>(wire_pixels[(4*16+8)*4]),0);
    unsigned edge_pixels=0;
    for (unsigned i=0;i<16*16;++i)
        if (std::to_integer<int>(wire_pixels[i*4])>0) ++edge_pixels;
    BOOST_CHECK_GT(edge_pixels,0u);
    BOOST_CHECK_LT(edge_pixels,128u);
    device.Destroy_Texture(grid_texture);
    device.Destroy_Texture(mapped_texture);
    renderer.Destroy_Mesh(mesh); renderer.Shutdown();
    for (auto handle : {base,detail,shroud,target,depth}) device.Destroy_Texture(handle);
}

BOOST_AUTO_TEST_CASE(indexed_batches_preserve_seams_material_changes_and_blended_pixels)
{
    DX11Device device({true});
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({16,16,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,16,16}));
    std::array<PropVertex,6> source{};
    source[0].position={-1,-1,0.5f}; source[1].position={1,-1,0.5f};
    source[2].position={1,1,0.5f}; source[3].position={-1,1,0.5f};
    source[4]=source[0]; source[5]=source[2];
    source[4].color={0,0,1,0.5f}; source[5].color={0,1,0,0.5f};
    source[4].uv={1,0}; source[5].uv={0,1};
    // Shared indices and distinct vertices at the same position coexist.
    const std::array<std::uint32_t,12> triangles{0,1,2,0,2,3,4,5,3,0,1,2};
    PropBatchBuilder batch;
    PropParameters parameters;
    parameters.textured=0;
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    PropStyle style;
    style.depth_write=false;
    for (unsigned material=0;material<2;++material) {
        BOOST_REQUIRE(batch.Begin(source.size()));
        std::array<unsigned,6> extractions{};
        const auto extract=[&](unsigned index) {
            ++extractions[index];
            auto vertex=source[index];
            vertex.material_diffuse={1,material==0 ? 0.3f : 0.8f,0.5f,0.4f};
            return vertex;
        };
        for (const auto index : triangles) BOOST_REQUIRE(batch.Append(index,extract));
        BOOST_CHECK_EQUAL(batch.Vertices().size(),6u);
        BOOST_CHECK_EQUAL(batch.Indices().size(),triangles.size());
        for (const auto count : extractions) BOOST_CHECK_EQUAL(count,1u);
        BOOST_CHECK(!batch.Append(6,extract));
        std::array<PropVertex,12> expanded{};
        std::array<std::uint32_t,12> sequential{};
        for (unsigned i=0;i<triangles.size();++i) {
            expanded[i]=extract(triangles[i]); sequential[i]=i;
            BOOST_CHECK(batch.Vertices()[batch.Indices()[i]].uv == expanded[i].uv);
            BOOST_CHECK(batch.Vertices()[batch.Indices()[i]].color == expanded[i].color);
        }
        const auto compact=renderer.Create_Mesh(batch.Vertices(),batch.Indices());
        const auto reference=renderer.Create_Mesh(expanded,sequential);
        std::array<std::byte,16*16*4> actual{},expected{};
        BOOST_REQUIRE(commands.Clear({0.2f,0.1f,0.3f,0.2f},1));
        BOOST_REQUIRE(renderer.Draw(commands,reference,style,parameters,{}));
        BOOST_REQUIRE(device.Readback_Texture(target,expected,16*4));
        BOOST_REQUIRE(commands.Clear({0.2f,0.1f,0.3f,0.2f},1));
        BOOST_REQUIRE(renderer.Draw(commands,compact,style,parameters,{}));
        BOOST_REQUIRE(device.Readback_Texture(target,actual,16*4));
        BOOST_CHECK(actual == expected);
        renderer.Destroy_Mesh(compact); renderer.Destroy_Mesh(reference);
    }
    renderer.Shutdown(); device.Destroy_Texture(target); device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(material_color_sources_preserve_lit_and_prelit_drawing)
{
    DX11Device device({true});
    PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,std::filesystem::path(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,
        static_cast<std::uint32_t>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,
        static_cast<std::uint32_t>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    PropParameters parameters;
    parameters.textured=0;
    parameters.scene_ambient={1,1,1,0};
    parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    PropStyle style; style.blend=RHIBlendMode::Disabled;
    std::array<PropVertex,4> source{};
    source[0].position={-1,-1,0.5f}; source[1].position={1,-1,0.5f};
    source[2].position={1,1,0.5f}; source[3].position={-1,1,0.5f};
    for (auto& vertex : source) {
        vertex.color={0.1f,0.2f,0.3f,0.4f};
        vertex.secondary_color={0.3f,0.1f,0.2f,0.7f};
    }
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const std::array sources{PropColorSource::Material,PropColorSource::PrimaryColor,PropColorSource::SecondaryColor};
    for (bool lit : {false,true}) for (auto diffuse : sources)
        for (auto ambient : sources) for (auto emissive : sources) {
        PropMaterial material;
        material.lighting=lit; material.diffuse_source=diffuse;
        material.ambient_source=ambient; material.emissive_source=emissive;
        material.diffuse={0.6f,0.5f,0.4f}; material.ambient={0.05f,0.07f,0.09f};
        material.emissive={0.08f,0.06f,0.04f}; material.opacity=0.6f;
        material.specular={0.2f,0.3f,0.4f}; material.shininess=19;
        auto vertices=source;
        for (auto& vertex : vertices) Apply_Prop_Material(vertex,material);
        BOOST_CHECK(vertices[0].color == source[0].color);
        BOOST_CHECK(vertices[0].position == source[0].position);
        BOOST_CHECK_EQUAL(vertices[0].material_specular[3],19);
        const auto select=[&](PropColorSource choice,const std::array<float,3>& value,unsigned channel) {
            return choice==PropColorSource::Material ? value[channel]
                : choice==PropColorSource::PrimaryColor ? source[0].color[channel] : source[0].secondary_color[channel];
        };
        std::array<float,4> expected{};
        for (unsigned channel=0;channel<3;++channel) {
            BOOST_CHECK_EQUAL(vertices[0].material_diffuse[channel],lit ? select(diffuse,material.diffuse,channel) : material.diffuse[channel]);
            expected[channel]=lit ? select(ambient,material.ambient,channel)+select(emissive,material.emissive,channel)
                : source[0].color[channel]*material.diffuse[channel];
        }
        expected[3]=lit ? diffuse==PropColorSource::PrimaryColor ? 0.4f
            : diffuse==PropColorSource::SecondaryColor ? 0.7f : 0.6f : 0.4f*0.6f;
        const auto mesh=renderer.Create_Mesh(vertices,indices);
        BOOST_REQUIRE(commands.Clear({0,0,0,0},1));
        BOOST_REQUIRE(renderer.Draw(commands,mesh,style,parameters,{}));
        std::array<std::byte,8*8*4> pixels{};
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        for (unsigned channel=0;channel<4;++channel)
            BOOST_CHECK_SMALL(float(std::to_integer<int>(pixels[(4*8+4)*4+channel]))-expected[channel]*255,1.1f);
        renderer.Destroy_Mesh(mesh);
    }
    renderer.Shutdown(); device.Destroy_Texture(target); device.Destroy_Texture(depth);
}
