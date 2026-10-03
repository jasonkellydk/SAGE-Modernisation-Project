module;
#define BOOST_TEST_MODULE PropAssetBindingTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Props.AssetBinding.Tests;
import std;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.StaticDrawOrder;
import Graphics.Resources.MipChain;
import Graphics.Tests.Device;
import Graphics.Scene.Shadows.DirectionalRenderer;
import Assets.Adapters.W3D;
import Assets.Adapters.W3D.Mesh;
import Assets.Adapters.W3D.Model;
import Assets.Adapters.W3D.Materials;
import Assets.Cache;
import Assets.Identity;
import Assets.Models;
import Assets.Math;
import Assets.Importers.Models;
import Assets.Materials;
import Video.Capture.ImageWriter;
using namespace Graphics;

namespace {
std::vector<std::byte> Read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) return {};
    const auto size=input.tellg(); input.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if(!input.read(reinterpret_cast<char*>(bytes.data()),size)) return {};
    return bytes;
}
class MappingAdapter final : public Assets::IModelAdapter {
public:
    bool Can_Import(const Assets::AssetIdentity&,std::span<const std::byte>) const noexcept override { return true; }
    Assets::ModelImportResult Import(const Assets::AssetIdentity& identity,std::span<const std::byte>) const override {
        auto model=std::make_unique<Assets::ModelAssetDesc>();model->name=identity.canonical_name;
        model->bounds={{-1,-1,0.5f},{1,1,0.5f}};
        for(const auto point:std::array<Assets::Vector3f,4>{{{-1,-1,0.5f},{1,-1,0.5f},{1,1,0.5f},{-1,1,0.5f}}}) {
            Assets::ModelVertexDesc vertex;vertex.position=point;vertex.normal={0,0,1};vertex.texcoord={0.125f,0.125f};model->vertices.push_back(vertex);
        }
        model->indices={0,1,2,0,2,3};model->submeshes={{0,6,0,"quad"}};
        if(identity.canonical_name=="prelit.w3d") {
            Assets::W3D::W3DParsedMesh mesh;mesh.header.name="solved";mesh.header.bounds=model->bounds;mesh.prelit_chunk=0x24;
            for(const auto& vertex:model->vertices) {mesh.positions.push_back(vertex.position);mesh.normals.push_back(vertex.normal);mesh.stage_texcoords.push_back(vertex.texcoord);mesh.colors.push_back({1,1,1,1});}
            mesh.triangles={{0,1,2},{0,2,3}};
            Assets::W3D::W3DVertexMaterialData material;material.material.name="paint";material.material.base_color={0,0,0,1};material.material.ambient_color={0,0,0,1};material.material.emissive_color={1,1,1,1};
            mesh.materials.vertex_materials.push_back(material);Assets::W3D::W3DTextureData texture;texture.name="paint.tga";mesh.materials.textures.push_back(texture);
            Assets::W3D::W3DMaterialPass pass;pass.vertex_material_index=0;pass.texture_index=0;pass.colors.assign(4,{.2f,.3f,.4f,1});mesh.materials.passes.push_back(pass);
            model->vertices.clear();model->indices.clear();model->submeshes.clear();Assets::W3D::W3DAppend_Mesh(*model,mesh);
            return {std::move(model),{}};
        }
        Assets::ModelMaterialDesc material;material.name="paint";material.scope=Assets::MaterialScope::Model;material.primary_texture="paint.tga";material.ambient_color={1,1,1,1};
        if(identity.canonical_name=="mapped.w3d") material.texture_mappings[0]=Assets::TextureEnvironmentMapping{Assets::TextureEnvironmentSource::Normal};
        if(identity.canonical_name=="screen.w3d" || identity.canonical_name=="screen-always.w3d") {
            model->submeshes[0].sort_level=1;
            // Explicit factors take precedence even when the fallback mode differs.
            material.render_mode=Assets::MaterialRenderMode::AlphaBlend;material.depth_write=false;
            material.draw_state=Assets::MaterialDrawState{Assets::MaterialBlendFactor::One,
                Assets::MaterialBlendFactor::InverseSourceColor,
                identity.canonical_name=="screen-always.w3d" ? Assets::MaterialDepthComparison::Always : Assets::MaterialDepthComparison::LessEqual};
        }
        if(identity.canonical_name=="alpha.w3d") material.render_mode=Assets::MaterialRenderMode::AlphaBlend;
        if(identity.canonical_name=="invalid-draw.w3d")
            material.draw_state=Assets::MaterialDrawState{static_cast<Assets::MaterialBlendFactor>(255)};
        model->materials.push_back(material);model->dependencies={{Assets::AssetType::Texture,"paint.tga"}};
        return {std::move(model),{}};
    }
};
}

BOOST_AUTO_TEST_CASE(authored_screen_blending_preserves_rgb_equation_depth_and_legacy_fallback)
{
    // Independent RGB oracle: screen = source + background * (1-source).
    // Deliberately fractional alpha distinguishes this from alpha blending.
    std::vector<std::byte> texture(18+4);texture[2]=std::byte{2};texture[12]=texture[14]=std::byte{1};
    texture[16]=std::byte{32};texture[17]=std::byte{0x28};
    texture[18]=std::byte{192};texture[19]=std::byte{128};texture[20]=std::byte{64};texture[21]=std::byte{64};
    Assets::AssetCache assets([&](const auto& identity) { return identity.type==Assets::AssetType::Texture ? texture : std::vector<std::byte>{std::byte{1}}; });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<MappingAdapter>()));
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    BOOST_REQUIRE(target.Is_Valid());BOOST_REQUIRE(depth.Is_Valid());
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    PropParameters parameters;parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};parameters.scene_ambient={1,1,1,1};
    const std::array<float,3> background{0.2f,0.4f,0.6f},source{64/255.0f,128/255.0f,192/255.0f};
    const auto check=[&](const std::array<float,3>& expected) {
        std::array<std::byte,8*8*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        for(unsigned channel=0;channel<3;++channel)
            BOOST_CHECK_SMALL(static_cast<int>(std::to_integer<unsigned>(pixels[(4*8+4)*4+channel]))-
                static_cast<int>(std::lround(expected[channel]*255)),3);
    };
    const auto load=[&](PropAssetBinding& binding,const char* name) {
        const auto handle=assets.Request_Model(name);assets.Wait(handle);BOOST_REQUIRE_MESSAGE(assets.Try_Get_Model(handle),assets.Get_Error(handle));
        std::string error;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    };
    PropAssetBinding screen,alpha,opaque,always;
    load(screen,"screen.w3d");load(alpha,"alpha.w3d");load(opaque,"opaque.w3d");load(always,"screen-always.w3d");
    BOOST_TEST(screen.Part_Sort_Level(0)==1);BOOST_TEST(opaque.Part_Sort_Level(0)==0);
    BOOST_TEST(!screen.Part_Requires_Transparency_Sorting(0));
    BOOST_TEST(alpha.Part_Requires_Transparency_Sorting(0));
    BOOST_TEST(!opaque.Part_Requires_Transparency_Sorting(0));
    BOOST_TEST(!opaque.Part_Requires_Transparency_Sorting(opaque.Part_Count()));
    BOOST_TEST(screen.Part_Sort_Level(screen.Part_Count())==0);
    std::array<float,3> screenExpected{},alphaExpected{};
    for(unsigned channel=0;channel<3;++channel) {
        screenExpected[channel]=source[channel]+background[channel]*(1-source[channel]);
        alphaExpected[channel]=source[channel]*(64/255.0f)+background[channel]*(1-64/255.0f);
    }
    for(const auto* binding:{&screen,&alpha,&opaque}) {
        BOOST_REQUIRE(commands.Clear({background[0],background[1],background[2],1},1));
        BOOST_REQUIRE(binding->Draw_Part(commands,0,parameters));
        check(binding==&screen ? screenExpected : binding==&alpha ? alphaExpected : source);
    }
    // LEQUAL fails behind stored depth; explicit ALWAYS bypasses comparison.
    PropAssetBinding prelit;load(prelit,"prelit.w3d");
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));BOOST_REQUIRE(prelit.Draw_Part(commands,0,parameters));
    check({source[0]*.2f,source[1]*.3f,source[2]*.4f});
    BOOST_REQUIRE(commands.Clear({background[0],background[1],background[2],1},0.25f));
    BOOST_REQUIRE(screen.Draw_Part(commands,0,parameters));check(background);
    BOOST_REQUIRE(always.Draw_Part(commands,0,parameters));check(screenExpected);
    // Screen passes at .5 but writes no depth: a later opaque quad at .75 passes.
    BOOST_REQUIRE(commands.Clear({background[0],background[1],background[2],1},1));
    BOOST_REQUIRE(screen.Draw_Part(commands,0,parameters));
    auto behind=parameters;behind.world[11]=0.25f;
    BOOST_REQUIRE(opaque.Draw_Part(commands,0,behind));check(source);
    // Ordinary transparency is submitted first but drawn after the static
    // background/effect bins. Geometry keeps its authored no-depthwrite state.
    DirectionalShadowRenderer shadows;PropSubmission submission;
    submission.Initialize(device,renderer,shadows);
    BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
    auto in_front=parameters;in_front.world[11]=-0.1f;
    BOOST_REQUIRE(alpha.Submit_Part(submission,0,in_front,PropDrawPhase::Transparent,{0,0,-1,0}));
    const std::array<std::int32_t,3> levels{0,1,5};
    std::vector<std::size_t> order;BOOST_REQUIRE(Build_Static_Draw_Order(levels,order));
    const std::vector<std::size_t> expected_order{2,1};BOOST_CHECK(order==expected_order);
    for(const auto index:order) BOOST_REQUIRE((index==2 ? opaque.Draw_Part(commands,0,behind) : screen.Draw_Part(commands,0,parameters)));
    std::array<float,3> combined{},final{};
    for(unsigned channel=0;channel<3;++channel) {
        combined[channel]=source[channel]+source[channel]*(1-source[channel]);
        final[channel]=source[channel]*(64/255.0f)+combined[channel]*(1-64/255.0f);
    }
    check(combined);BOOST_REQUIRE(submission.Flush_Transparent());check(final);submission.Shutdown();
    const auto invalid=assets.Request_Model("invalid-draw.w3d");assets.Wait(invalid);
    PropAssetBinding rejected;std::string error;
    BOOST_TEST(!rejected.Load(device,renderer,assets,invalid,error));
    BOOST_TEST(error=="invalid authored material draw state");
    screen.Clear();alpha.Clear();opaque.Clear();always.Clear();renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(typed_asset_binding_keeps_environment_mapping_across_material_changes)
{
    std::vector<std::byte> texture(18+8*8*4);texture[2]=std::byte{2};texture[12]=std::byte{8};texture[14]=std::byte{8};texture[16]=std::byte{32};texture[17]=std::byte{0x28};
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        const auto offset=18+(y*8+x)*4;const bool red=x<2 && y<2;
        texture[offset]=std::byte(red ? 0 : 255);texture[offset+2]=std::byte(red ? 255 : 0);texture[offset+3]=std::byte{255};
    }
    Assets::AssetCache assets([&](const auto& identity) { return identity.type==Assets::AssetType::Texture ? texture : std::vector<std::byte>{std::byte{1}}; });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<MappingAdapter>()));
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    PropParameters parameters;parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};parameters.scene_ambient={1,1,1,1};
    for(const auto name:{"unmapped.w3d","mapped.w3d","unmapped.w3d","mapped.w3d"}) {
        const auto handle=assets.Request_Model(name);assets.Wait(handle);BOOST_REQUIRE_MESSAGE(assets.Try_Get_Model(handle),assets.Get_Error(handle));
        PropAssetBinding binding;std::string error;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
        BOOST_REQUIRE(commands.Clear({0,1,0,1},1));BOOST_REQUIRE(binding.Draw_Part(commands,0,parameters));
        std::array<std::byte,8*8*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        const auto center=(4*8+4)*4;const bool mapped=std::string_view(name)=="mapped.w3d";
        BOOST_TEST(std::to_integer<unsigned>(pixels[center+(mapped ? 2 : 0)])>250u);
        BOOST_TEST(std::to_integer<unsigned>(pixels[center+(mapped ? 0 : 2)])<3u);
    }
    renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(explicit_model_texture_quality_replaces_owned_resources_without_changing_other_bindings)
{
    std::vector<std::byte> texture(18+8*8*4);texture[2]=std::byte{2};texture[12]=std::byte{8};texture[14]=std::byte{8};texture[16]=std::byte{32};texture[17]=std::byte{0x28};
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        const auto offset=18+(y*8+x)*4;const bool red=x<2 && y<2;
        texture[offset]=std::byte(red ? 0 : 255);texture[offset+2]=std::byte(red ? 255 : 0);texture[offset+3]=std::byte{255};
    }
    Assets::AssetCache assets([&](const auto& identity) {return identity.type==Assets::AssetType::Texture ? texture : std::vector<std::byte>{std::byte{1}};});
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<MappingAdapter>()));
    const auto model=assets.Request_Model("unmapped.w3d");assets.Wait(model);
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({8,8,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    PropParameters parameters;parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};parameters.scene_ambient={1,1,1,1};
    PropAssetBinding original,reduced;std::string error;
    BOOST_REQUIRE_MESSAGE(original.Load(device,renderer,assets,model,error),error);
    const auto check=[&](PropAssetBinding& binding,unsigned red) {
        BOOST_REQUIRE(commands.Clear({0,1,0,1},1));BOOST_REQUIRE(binding.Draw_Part(commands,0,parameters));
        std::array<std::byte,8*8*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        const auto center=(4*8+4)*4;
        BOOST_CHECK_SMALL(int(std::to_integer<unsigned>(pixels[center]))-int(red),2);
        BOOST_CHECK_SMALL(int(std::to_integer<unsigned>(pixels[center+2]))-int(255-red),2);
    };
    // The 2x2 mip contains one 64-red pixel. At UV .125, wrap/linear sampling
    // gives it .75*.75 coverage: 36 red. The final 1x1 mip is uniformly 16.
    for(const auto [reduction,minimum,red]:std::array<std::array<unsigned,3>,4>{{{2,1,36},{999,1,16},{999,4,255},{0,1,255}}}) {
        BOOST_REQUIRE_MESSAGE(reduced.Load(device,renderer,assets,model,error,{},{},reduction,minimum),error);
        BOOST_TEST(reduced.Texture_Count()==1u);check(reduced,red);check(original,255);
        BOOST_TEST(!reduced.Load(device,renderer,assets,{},error));check(reduced,red);
    }
    // Padded input, one-mip assets and malformed work use the same uploader.
    std::array<std::byte,24> padded{};padded.fill(std::byte{255});
    auto single=Create_Mipped_Texture(device,2,2,padded,12,1,999);BOOST_REQUIRE(single.Is_Valid());
    std::array<std::byte,16> read{};BOOST_REQUIRE(device.Readback_Texture(single,read,8));BOOST_CHECK(read[0]==std::byte{255});
    device.Destroy_Texture(single);
    BOOST_TEST(!Create_Mipped_Texture(device,2,2,std::span(padded).first(19),12).Is_Valid());
    BOOST_TEST(!Create_Mipped_Texture(device,(std::numeric_limits<unsigned>::max)(),1,padded,8).Is_Valid());
    reduced.Clear();original.Clear();renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(automatically_adapted_native_models_render_with_original_rigs)
{
    const char* directory=std::getenv("GENERALS_W3D_ADAPTED_DIRECTORY");
    const char* textures=std::getenv("GENERALS_W3D_SURFACE_TEXTURES");
    const char* legacy=std::getenv("GENERALS_W3D_LEGACY_TEXTURES");
    if(!directory || !textures) { BOOST_TEST_MESSAGE("Set adapted model directory for batch drawing");return; }
    auto upper=[](std::string value) { for(auto& c:value)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));return value; };
    GraphicsTestDevice device({true});PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    constexpr unsigned extent=512;
    const auto target=device.Create_Texture({extent,extent,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({extent,extent,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,extent,extent}));
    unsigned rendered=0;
    std::vector<std::pair<std::string,std::string>> unsupported;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(directory)) {
        const auto& path=entry.path();
        if(!entry.is_regular_file() || upper(path.extension().string())!=".W3D"
            || upper(path.stem().string())!=upper(path.parent_path().filename().string()))continue;
        BOOST_TEST_CONTEXT(path.filename().string()) {
            const auto bytes=Read(path);
            Assets::AssetCache assets([&](const Assets::AssetIdentity& identity) {
                if(identity.type==Assets::AssetType::Model)return bytes;
                auto texture_path=std::filesystem::path(textures)/identity.canonical_name;
                auto data=Read(texture_path);
                if(data.empty()) {texture_path.replace_extension(".dds");data=Read(texture_path);}
                if(data.empty() && legacy) {
                    texture_path=std::filesystem::path(legacy)/identity.canonical_name;
                    texture_path.replace_extension(".dds");data=Read(texture_path);
                }
                return data;
            });
            BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
            const auto handle=assets.Request_Model(path.filename().string());assets.Wait(handle);
            const auto* model=assets.Try_Get_Model(handle);
            if(!model && assets.Get_Error(handle).find("W3D shader 'DefaultW3D.fx': unsupported shader program")!=std::string::npos) {
                // Staging can contain programs whose runtime translator is still
                // pending. Assert explicit rejection and report it separately.
                unsupported.emplace_back(path.filename().string(),assets.Get_Error(handle));
                continue;
            }
            BOOST_REQUIRE_MESSAGE(model,assets.Get_Error(handle));
            BOOST_REQUIRE_EQUAL(model->Skin_Bone_Count(),0u);
            std::string error;ModelAssetPose pose;
            BOOST_REQUIRE_MESSAGE(pose.Initialize(model->Rig(),error),error);
            PropAssetBinding binding;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
            std::array<float,3> low{1e30f,1e30f,1e30f},high{-1e30f,-1e30f,-1e30f};
            for(const auto& part:model->Submeshes()) {
                const auto label=upper(part.name);
                if(label.find("FX")!=std::string::npos || label.find("HEADLIGHT")!=std::string::npos)continue;
                RenderTransform transform;
                for(const auto& attachment:model->Rig().attachments) {
                    const auto dot=attachment.object_name.find('.');
                    if(attachment.object_name.substr(dot==std::string::npos ? 0 : dot+1)==part.name) {
                        BOOST_REQUIRE(pose.Bone_Transform(attachment.bone,transform));break;
                    }
                }
                for(auto index:model->Indices().subspan(part.first_index,part.index_count)) {
                    const auto p=model->Vertices()[index].position;
                    for(unsigned axis=0;axis<3;++axis) {
                        const auto& m=transform.matrix;
                        const auto value=m[axis*4]*p.x+m[axis*4+1]*p.y+m[axis*4+2]*p.z+m[axis*4+3];
                        low[axis]=(std::min)(low[axis],value);high[axis]=(std::max)(high[axis],value);
                    }
                }
            }
            const float span=(std::max)({high[0]-low[0],high[1]-low[1],high[2]-low[2]});
            BOOST_REQUIRE(span>0);const float s=1.2f/span;
            const float x=(low[0]+high[0])*.5f,y=(low[1]+high[1])*.5f,z=(low[2]+high[2])*.5f;
            PropParameters parameters;
            parameters.view_projection={.7f*s,.7f*s,0,-.7f*s*(x+y),-.35f*s,.35f*s,.8f*s,s*(.35f*x-.35f*y-.8f*z),-.2f*s,.2f*s,-.2f*s,.5f+s*(.2f*x-.2f*y+.2f*z),0,0,0,1};
            parameters.camera_position={300,-450,600,1};parameters.scene_ambient={.7f,.75f,.8f,1};
            parameters.light_direction[0]={.3f,-.5f,.8f,1};parameters.light_diffuse[0]={1,1,1,1};parameters.light_specular[0]={.4f,.4f,.4f,1};
            BOOST_REQUIRE(commands.Clear({.02f,.02f,.02f,1},1));
            for(std::size_t part=0;part<binding.Part_Count();++part)BOOST_REQUIRE(binding.Draw_Part(commands,part,parameters,&pose));
            std::vector<std::byte> pixels(extent*extent*4);BOOST_REQUIRE(device.Readback_Texture(target,pixels,extent*4));
            std::size_t visible=0;for(std::size_t i=0;i<pixels.size();i+=4)if(std::to_integer<unsigned>(pixels[i])>25)++visible;
            BOOST_REQUIRE_MESSAGE(visible>600u,"No substantial model image: "<<visible<<" pixels");
            if(const char* preview=std::getenv("GENERALS_W3D_ADAPTED_PREVIEW")) {
                Engine::Video::DecodedVideoFrame frame;frame.width=extent;frame.height=extent;frame.row_pitch=extent*4;
                frame.format=Engine::Video::PixelFormat::RGBA8;frame.pixels=std::move(pixels);
                BOOST_REQUIRE(Engine::Video::Write_Frame_Image(std::filesystem::path(preview)/(path.stem().string()+".png"),frame));
            }
            ++rendered;
        }
    }
    BOOST_REQUIRE(rendered>=6u);
    if(const char* preview=std::getenv("GENERALS_W3D_ADAPTED_PREVIEW")) {
        std::ofstream report(std::filesystem::path(preview)/"validation.json");
        report<<"{\"rendered\":"<<rendered<<",\"unsupported\":[";
        for(std::size_t i=0;i<unsupported.size();++i) {
            if(i)report<<',';
            report<<"{\"file\":"<<std::quoted(unsupported[i].first)<<",\"error\":"<<std::quoted(unsupported[i].second)<<'}';
        }
        report<<"],\"whole_game_compatible\":false}";
        BOOST_REQUIRE(report.good());
    }
    renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(weighted_surface_pose_renders_and_survives_deferred_owner_release)
{
    const char* source=std::getenv("GENERALS_W3D_SKIN_RENDER_ASSET");
    const char* textures=std::getenv("GENERALS_W3D_SURFACE_TEXTURES");
    if(!source || !textures) { BOOST_TEST_MESSAGE("Set weighted surface fixture paths"); return; }
    const auto bytes=Read(source);
    Assets::AssetCache assets([&](const Assets::AssetIdentity& identity) {
        return identity.type==Assets::AssetType::Model ? bytes : Read(std::filesystem::path(textures)/identity.canonical_name);
    });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
    const auto handle=assets.Request_Model("weighted_surface.w3d");assets.Wait(handle);
    const auto* model=assets.Try_Get_Model(handle);BOOST_REQUIRE_MESSAGE(model,assets.Get_Error(handle));
    BOOST_REQUIRE(model->Skin_Bone_Count()>0);
    std::string error;ModelAssetPose pose;BOOST_REQUIRE_MESSAGE(pose.Initialize(model->Rig(),error),error);
    BOOST_REQUIRE(!model->Rig().animations.empty());
    GraphicsTestDevice device({true});PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    PropAssetBinding binding;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    constexpr unsigned extent=512;
    const auto target=device.Create_Texture({extent,extent,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({extent,extent,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    PropParameters parameters;
    parameters.view_projection={.006f,.004f,0,0,-.003f,.0045f,.006f,-.15f,-.0005f,.00075f,-.001f,.5f,0,0,0,1};
    parameters.camera_position={300,-450,600,1};parameters.scene_ambient={.7f,.75f,.8f,1};
    parameters.light_direction[0]={.3f,-.5f,.8f,1};parameters.light_diffuse[0]={1,1,1,1};parameters.light_specular[0]={.4f,.4f,.4f,1};
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,extent,extent}));
    const float last=static_cast<float>(model->Rig().animations[0].frame_count-1);
    std::array<std::vector<std::byte>,2> images;
    for(unsigned sample=0;sample<2;++sample) {
        BOOST_REQUIRE(pose.Evaluate(0,sample ? last : 0));BOOST_REQUIRE(commands.Clear({.02f,.02f,.02f,1},1));
        for(std::size_t part=0;part<binding.Part_Count();++part) BOOST_REQUIRE(binding.Draw_Part(commands,part,parameters,&pose));
        images[sample].resize(extent*extent*4);BOOST_REQUIRE(device.Readback_Texture(target,images[sample],extent*4));
        if(const char* preview=std::getenv("GENERALS_W3D_SKIN_PREVIEW")) {
            Engine::Video::DecodedVideoFrame frame;frame.width=extent;frame.height=extent;frame.row_pitch=extent*4;
            frame.format=Engine::Video::PixelFormat::RGBA8;frame.pixels=images[sample];
            BOOST_REQUIRE(Engine::Video::Write_Frame_Image(std::filesystem::path(preview)/(sample ? "skin_finished.png" : "skin_start.png"),frame));
        }
    }
    std::size_t visible=0,changed=0;
    for(std::size_t i=0;i<images[0].size();i+=4) {
        if(std::to_integer<unsigned>(images[1][i])>25) ++visible;
        if(images[0][i]!=images[1][i] || images[0][i+1]!=images[1][i+1]) ++changed;
    }
    BOOST_TEST(visible>2000u);BOOST_TEST(changed>1000u);
    DirectionalShadowRenderer shadows;PropSubmission submission;submission.Initialize(device,renderer,shadows);
    BOOST_REQUIRE(commands.Clear({.02f,.02f,.02f,1},1));
    for(std::size_t part=0;part<binding.Part_Count();++part)
        BOOST_REQUIRE(binding.Submit_Part(submission,part,parameters,PropDrawPhase::Material,{},&pose));
    BOOST_REQUIRE(pose.Evaluate(0,0));binding.Clear();
    BOOST_REQUIRE(submission.Flush_Transparent());
    std::vector<std::byte> captured(extent*extent*4);BOOST_REQUIRE(device.Readback_Texture(target,captured,extent*4));
    BOOST_TEST(std::equal(captured.begin(),captured.end(),images[1].begin()));
    submission.Shutdown();renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(converted_airfield_renders_through_typed_asset_and_prop_bindings)
{
    const char* source=std::getenv("GENERALS_W3D_SURFACE_ASSET");
    const char* textures=std::getenv("GENERALS_W3D_SURFACE_TEXTURES");
    if(!source || !textures) { BOOST_TEST_MESSAGE("Set surface asset/texture paths for the converted airfield draw"); return; }
    const auto bytes=Read(source); BOOST_REQUIRE(!bytes.empty());
    Assets::AssetCache assets([&](const Assets::AssetIdentity& identity) {
        return identity.type==Assets::AssetType::Model ? bytes : Read(std::filesystem::path(textures)/identity.canonical_name);
    });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
    const auto handle=assets.Request_Model("airfield.w3d"); assets.Wait(handle);
    BOOST_REQUIRE_MESSAGE(assets.Try_Get_Model(handle)!=nullptr,assets.Get_Error(handle));
    GraphicsTestDevice device({true}); PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    PropAssetBinding binding; std::string error;
    BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    BOOST_TEST(binding.Part_Count()==1u); BOOST_TEST(binding.Texture_Count()==3u);
    BOOST_TEST(binding.Part_Name(0)=="USA_AIRFIELD");
    BOOST_TEST(!binding.Load(device,renderer,assets,{},error));
    BOOST_TEST(binding.Part_Count()==1u);
    constexpr unsigned width=1024,height=768;
    const auto target=device.Create_Texture({width,height,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({width,height,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    PropParameters parameters;
    const auto& bounds=assets.Try_Get_Model(handle)->Bounds();
    const std::array center{(bounds.minimum.x+bounds.maximum.x)*.5f,
        (bounds.minimum.y+bounds.maximum.y)*.5f,(bounds.minimum.z+bounds.maximum.z)*.5f};
    const std::array<float,3> right{.789352f,.613941f,0},up{-.31000f,.39857f,.863174f},out{.529929f,-.681337f,.504f};
    const auto dot=[&](auto v) { return v[0]*center[0]+v[1]*center[1]+v[2]*center[2]; };
    parameters.view_projection={right[0]*2/430,right[1]*2/430,0,-dot(right)*2/430,
        up[0]*2/322.5f,up[1]*2/322.5f,up[2]*2/322.5f,-dot(up)*2/322.5f,
        -out[0]/800,-out[1]/800,-out[2]/800,.5f+dot(out)/800,0,0,0,1};
    parameters.camera_position={center[0]+out[0]*600,center[1]+out[1]*600,center[2]+out[2]*600,1};
    parameters.scene_ambient={.7f,.75f,.82f,1};
    parameters.light_direction[0]={-.3f,-.5f,.8124f,1};
    parameters.light_diffuse[0]={.9f,.85f,.8f,1};
    parameters.light_specular[0]={.6f,.6f,.6f,1};
    parameters.surface.team_color={.15f,.35f,.9f,1};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Set_Viewport({0,0,width,height}));
    BOOST_REQUIRE(commands.Clear({.06f,.07f,.09f,1},1));
    BOOST_REQUIRE(binding.Draw_Part(commands,0,parameters));
    std::vector<std::byte> pixels(width*height*4);
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,width*4));
    std::size_t visible=0;
    for(std::size_t pixel=0;pixel<pixels.size();pixel+=4)
        if(std::to_integer<int>(pixels[pixel])>40 || std::to_integer<int>(pixels[pixel+1])>40 || std::to_integer<int>(pixels[pixel+2])>40) ++visible;
    BOOST_TEST(visible>30000u);
    if(const char* preview=std::getenv("GENERALS_W3D_SURFACE_PREVIEW")) {
        Engine::Video::DecodedVideoFrame frame;
        frame.width=width; frame.height=height; frame.row_pitch=width*4;
        frame.format=Engine::Video::PixelFormat::RGBA8; frame.pixels=pixels;
        BOOST_REQUIRE(Engine::Video::Write_Frame_Image(preview,frame));
    }
    binding.Clear(); renderer.Shutdown(); device.Destroy_Texture(target); device.Destroy_Texture(depth);
}

// The original's opacity override (Drawable::setDrawableOpacity -> MeshClass alpha override; the legacy path in
// ModelMeshDrawing: an opaque shader turns to source alpha over what is behind, alpha tested at 96 x opacity). A part
// drawn through Draw_Part_Translucent at 0.45 lands, where it is drawn, 0.45 of the way from the clear colour to the
// same part drawn opaque (within 4 of 255 for 8-bit rounding of both captures). Run on e.g. NBAirfield.W3D.
BOOST_AUTO_TEST_CASE(translucent_parts_blend_at_the_opacity_override)
{
    const char* source=std::getenv("GENERALS_W3D_TRANSLUCENT_ASSET");
    const char* textures=std::getenv("GENERALS_W3D_TRANSLUCENT_TEXTURES");
    if(!source || !textures) { BOOST_TEST_MESSAGE("Set a W3D model and its texture folder for the translucent draw"); return; }
    const auto bytes=Read(source); BOOST_REQUIRE(!bytes.empty());
    Assets::AssetCache assets([&](const Assets::AssetIdentity& identity) {
        if(identity.type==Assets::AssetType::Model) return bytes;
        auto path=std::filesystem::path(textures)/identity.canonical_name;
        auto data=Read(path);
        if(data.empty()) { path.replace_extension(".dds"); data=Read(path); }
        return data;
    });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
    const auto handle=assets.Request_Model(std::filesystem::path(source).filename().string()); assets.Wait(handle);
    const auto* model=assets.Try_Get_Model(handle); BOOST_REQUIRE_MESSAGE(model!=nullptr,assets.Get_Error(handle));
    std::string error; ModelAssetPose pose; BOOST_REQUIRE_MESSAGE(pose.Initialize(model->Rig(),error),error);
    GraphicsTestDevice device({true}); PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    PropAssetBinding binding; BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    constexpr unsigned extent=512;
    const auto target=device.Create_Texture({extent,extent,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({extent,extent,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    const auto& bounds=model->Bounds();
    const float x=(bounds.minimum.x+bounds.maximum.x)*.5f,y=(bounds.minimum.y+bounds.maximum.y)*.5f,z=(bounds.minimum.z+bounds.maximum.z)*.5f;
    const float span=(std::max)({bounds.maximum.x-bounds.minimum.x,bounds.maximum.y-bounds.minimum.y,bounds.maximum.z-bounds.minimum.z});
    BOOST_REQUIRE(span>0); const float s=1.2f/span;
    PropParameters parameters;
    parameters.view_projection={.7f*s,.7f*s,0,-.7f*s*(x+y),-.35f*s,.35f*s,.8f*s,s*(.35f*x-.35f*y-.8f*z),-.2f*s,.2f*s,-.2f*s,.5f+s*(.2f*x-.2f*y+.2f*z),0,0,0,1};
    parameters.camera_position={300,-450,600,1}; parameters.scene_ambient={.7f,.75f,.8f,1};
    parameters.light_direction[0]={.3f,-.5f,.8f,1}; parameters.light_diffuse[0]={1,1,1,1};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth)); BOOST_REQUIRE(commands.Set_Viewport({0,0,extent,extent}));
    // One part at a time, so each pixel is a single surface over the clear colour (parts crossing each other blend
    // over what is already there, as the original's did).
    const auto capture=[&](std::size_t part,bool translucent) {
        BOOST_REQUIRE(commands.Clear({.2f,.2f,.2f,1},1));
        PropParameters drawn=parameters;
        if(translucent) drawn.vertex_material_override={.45f,1,0,1};
        BOOST_REQUIRE(translucent ? binding.Draw_Part_Translucent(commands,part,drawn,.45f,&pose) : binding.Draw_Part(commands,part,drawn,&pose));
        std::vector<std::byte> pixels(extent*extent*4);
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,extent*4));
        return pixels;
    };
    constexpr int clear=51;
    std::size_t exact=0;
    for(std::size_t part=0;part<binding.Part_Count();++part) {
        const auto opaque=capture(part,false),blended=capture(part,true);
        std::size_t partCovered=0,partBetween=0;
        for(std::size_t pixel=0;pixel<opaque.size();pixel+=4) {
            const int drawn=std::to_integer<int>(opaque[pixel+1]),mixed=std::to_integer<int>(blended[pixel+1]);
            if(std::abs(drawn-clear)<40 || mixed==clear) continue; // not drawn, or cut by the alpha test
            ++partCovered;
            if(std::abs(static_cast<float>(mixed)-(clear+.45f*static_cast<float>(drawn-clear)))<=4.0f) ++partBetween;
        }
        BOOST_TEST_MESSAGE(binding.Part_Name(part)<<": "<<partBetween<<" of "<<partCovered);
        if(partCovered>=400 && partBetween==partCovered) ++exact;
    }
    // Opaque parts that do not cross themselves land exactly there (the airfield's cylinder, house colour and
    // helipad); parts folding over themselves blend over their own nearer faces, and already blended parts
    // (banners, lights) keep their own blending.
    BOOST_TEST(exact>=3u);
    binding.Clear(); renderer.Shutdown(); device.Destroy_Texture(target); device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(converted_section_door_opens_through_native_rig_and_surface_renderer)
{
    const char* directory=std::getenv("GENERALS_W3D_DOOR_DIRECTORY");
    const char* textures=std::getenv("GENERALS_W3D_SURFACE_TEXTURES");
    if(!directory || !textures) { BOOST_TEST_MESSAGE("Set door and texture paths for animated surface drawing"); return; }
    const auto bytes=Read(std::filesystem::path(directory)/"ABArFrcCmd_A2.W3D");
    Assets::AssetCache assets([&](const Assets::AssetIdentity& identity) {
        return identity.type==Assets::AssetType::Model ? bytes : Read(std::filesystem::path(textures)/identity.canonical_name);
    });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
    const auto handle=assets.Request_Model("ABArFrcCmd_A2.W3D");assets.Wait(handle);
    const auto* model=assets.Try_Get_Model(handle);BOOST_REQUIRE_MESSAGE(model,assets.Get_Error(handle));
    std::string error;ModelAssetPose pose;BOOST_REQUIRE_MESSAGE(pose.Initialize(model->Rig(),error),error);
    GraphicsTestDevice device({true});PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    PropAssetBinding binding;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    BOOST_REQUIRE_EQUAL(binding.Part_Count(),3u);
    constexpr unsigned extent=512;
    const auto target=device.Create_Texture({extent,extent,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({extent,extent,1,RHITextureFormat::D32_Float,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    PropParameters parameters;
    parameters.view_projection={0,1.f/20,0,49.23512f/20,0,0,1.f/20,-15.f/20,-1.f/200,0,0,.1f,0,0,0,1};
    parameters.camera_position={100,-49,30,1};parameters.scene_ambient={.8f,.8f,.8f,1};
    parameters.light_direction[0]={1,0,0,1};parameters.light_diffuse[0]={1,1,1,1};parameters.light_specular[0]={.3f,.3f,.3f,1};
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,extent,extent}));
    std::array<std::size_t,2> coverage{};
    for(unsigned sample=0;sample<2;++sample) {
        const auto frame=sample?40.f:0.f;BOOST_REQUIRE(pose.Evaluate(0,frame));BOOST_REQUIRE(commands.Clear({.02f,.02f,.02f,1},1));
        for(std::size_t part=0;part<binding.Part_Count();++part) BOOST_REQUIRE(binding.Draw_Part(commands,part,parameters,&pose));
        std::vector<std::byte> pixels(extent*extent*4);BOOST_REQUIRE(device.Readback_Texture(target,pixels,extent*4));
        for(std::size_t i=0;i<pixels.size();i+=4)if(std::to_integer<unsigned>(pixels[i])>25)++coverage[sample];
        if(const char* preview=std::getenv("GENERALS_W3D_DOOR_PREVIEW")) {
            Engine::Video::DecodedVideoFrame image;image.width=extent;image.height=extent;image.row_pitch=extent*4;
            image.format=Engine::Video::PixelFormat::RGBA8;image.pixels=std::move(pixels);
            BOOST_REQUIRE(Engine::Video::Write_Frame_Image(std::filesystem::path(preview)/(sample?"door_open.png":"door_closed.png"),image));
        }
    }
    BOOST_TEST(coverage[0]>10000u);BOOST_TEST(coverage[1]<coverage[0]*.4);
    DirectionalShadowRenderer shadows;PropSubmission submission;submission.Initialize(device,renderer,shadows);
    BOOST_REQUIRE(pose.Evaluate(0,0));BOOST_REQUIRE(commands.Clear({.02f,.02f,.02f,1},1));
    for(std::size_t part=0;part<binding.Part_Count();++part)
        BOOST_REQUIRE(binding.Submit_Part(submission,part,parameters,PropDrawPhase::Material,{},&pose));
    BOOST_REQUIRE(pose.Evaluate(0,40));binding.Clear();
    BOOST_REQUIRE(submission.Flush_Transparent());
    std::vector<std::byte> captured(extent*extent*4);BOOST_REQUIRE(device.Readback_Texture(target,captured,extent*4));
    std::size_t retained_coverage=0;for(std::size_t i=0;i<captured.size();i+=4)if(std::to_integer<unsigned>(captured[i])>25)++retained_coverage;
    BOOST_TEST(retained_coverage==coverage[0]);
    submission.Shutdown();renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(texture_replacements_quality_mapping_and_stencil_coexist)
{
    const auto texture=[](bool blue) {
        std::vector<std::byte> pixels(18+4*4*4);pixels[2]=std::byte{2};
        pixels[12]=pixels[14]=std::byte{4};pixels[16]=std::byte{32};pixels[17]=std::byte{0x28};
        for(unsigned i=18;i<pixels.size();i+=4) {
            pixels[i+(blue ? 0 : 2)]=std::byte{255};pixels[i+3]=std::byte{255};
        }
        return pixels;
    };
    Assets::AssetCache assets([&](const auto& identity) {
        return identity.type==Assets::AssetType::Texture ? texture(identity.canonical_name=="replacement.tga") : std::vector<std::byte>{std::byte{1}};
    });
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<MappingAdapter>()));
    const auto model=assets.Request_Model("unmapped.w3d");assets.Wait(model);
    const auto original=assets.Request_Texture("paint.tga"),replacement=assets.Request_Texture("replacement.tga");
    assets.Wait(original);assets.Wait(replacement);
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({8,8,1,RHITextureFormat::RGBA8_UNorm,static_cast<unsigned>(RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({8,8,1,RHITextureFormat::D24_UNorm_S8,static_cast<unsigned>(RHITextureUsage::DepthStencil)});
    BOOST_REQUIRE(target.Is_Valid());BOOST_REQUIRE(depth.Is_Valid());
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Set_Viewport({0,0,8,8}));
    PropParameters parameters;parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};parameters.scene_ambient={1,1,1,1};
    PropTextureMappingContext mapping;mapping.milliseconds=3456;
    const std::array swaps{std::pair{original,replacement}};
    PropAssetBinding changed,overridden;std::string error;
    BOOST_REQUIRE_MESSAGE(changed.Load(device,renderer,assets,model,error,{},swaps,2,1),error);
    BOOST_REQUIRE_MESSAGE(overridden.Load(device,renderer,assets,model,error,original,swaps,2,1),error);
    changed.Clamp_Texture_Addressing();overridden.Clamp_Texture_Addressing();
    RHIStencilDescription reject;reject.enabled=true;
    reject.front.comparison=reject.back.comparison=RHIComparison::Never;
    const auto check=[&](PropAssetBinding& binding,const RHIStencilDescription* stencil,unsigned red,unsigned blue) {
        BOOST_REQUIRE(commands.Clear({0,1,0,1},1));
        BOOST_REQUIRE(binding.Draw_Part(commands,0,parameters,nullptr,0,{},stencil,&mapping));
        std::array<std::byte,8*8*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        const auto center=(4*8+4)*4;
        BOOST_CHECK_SMALL(int(std::to_integer<unsigned>(pixels[center]))-int(red),2);
        BOOST_CHECK_SMALL(int(std::to_integer<unsigned>(pixels[center+2]))-int(blue),2);
    };
    check(changed,&reject,0,0); // Stencil must reject despite the texture override.
    check(changed,nullptr,0,255); // Per-prototype replacement still applies with reduced mips.
    check(overridden,nullptr,255,0); // Whole-model override takes precedence.
    check(changed,nullptr,0,255); // The other binding cannot mutate this one.
    changed.Clear();overridden.Clear();renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(staged_uploads_keep_the_previous_binding_through_partial_work_cancel_and_failure)
{
    std::vector<std::byte> texture(18+4);texture[2]=std::byte{2};texture[12]=texture[14]=std::byte{1};texture[16]=std::byte{32};texture[17]=std::byte{0x28};
    texture[20]=texture[21]=std::byte{255};
    Assets::AssetCache assets([&](const auto& identity) {return identity.type==Assets::AssetType::Texture ? texture : std::vector<std::byte>{std::byte{1}};});
    BOOST_REQUIRE(assets.Register_Model_Adapter(std::make_shared<MappingAdapter>()));
    const auto handle=assets.Request_Model("opaque.w3d");assets.Wait(handle);const auto* model=assets.Try_Get_Model(handle);BOOST_REQUIRE(model);
    GraphicsTestDevice device({true});BOOST_REQUIRE(device.Is_Valid());PropRenderer renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    std::string error;PropAssetBinding binding;BOOST_REQUIRE_MESSAGE(binding.Load(device,renderer,assets,handle,error),error);
    auto textures=std::make_shared<PreparedPropTextures>();BOOST_REQUIRE_MESSAGE(Prepare_Prop_Textures(assets,handle,*textures,error),error);
    std::vector<PropAssetPart> geometry;BOOST_REQUIRE_MESSAGE(Build_Prop_Asset_Geometry(*model,geometry,error),error);
    geometry.push_back(geometry.front());
    const auto prepare=[&] {return PreparedPropAsset{handle,geometry,textures};};
    const auto forever=(std::chrono::steady_clock::time_point::max)();
    BOOST_REQUIRE(binding.Begin_Load(device,renderer,assets,prepare(),error));
    BOOST_CHECK(binding.Advance_Load(assets,0,forever,error)==PropAssetLoadState::Pending);
    BOOST_TEST(binding.Uploaded_Parts()==0u);BOOST_TEST(binding.Part_Count()==1u);
    BOOST_CHECK(binding.Advance_Load(assets,8,std::chrono::steady_clock::now(),error)==PropAssetLoadState::Pending);
    BOOST_TEST(binding.Uploaded_Parts()==0u);
    BOOST_CHECK(binding.Advance_Load(assets,1,forever,error)==PropAssetLoadState::Pending);
    BOOST_TEST(binding.Uploaded_Parts()==1u);BOOST_TEST(binding.Part_Count()==1u);
    binding.Cancel_Load();BOOST_TEST(binding.Part_Count()==1u);
    auto invalid=prepare();invalid.geometry.back().material_index=999;
    BOOST_REQUIRE(binding.Begin_Load(device,renderer,assets,std::move(invalid),error));
    BOOST_CHECK(binding.Advance_Load(assets,1,forever,error)==PropAssetLoadState::Pending);
    BOOST_CHECK(binding.Advance_Load(assets,1,forever,error)==PropAssetLoadState::Failed);
    BOOST_TEST(binding.Part_Count()==1u);BOOST_CHECK(error=="invalid prepared material index");
    BOOST_REQUIRE(binding.Begin_Load(device,renderer,assets,prepare(),error));
    BOOST_CHECK(binding.Advance_Load(assets,1,forever,error)==PropAssetLoadState::Pending);
    BOOST_CHECK(binding.Advance_Load(assets,1,forever,error)==PropAssetLoadState::Ready);
    BOOST_TEST(binding.Part_Count()==2u);BOOST_TEST(binding.Texture_Count()==1u);
    binding.Clear();renderer.Shutdown();
}
