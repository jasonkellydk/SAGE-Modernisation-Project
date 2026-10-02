module;
#define BOOST_TEST_MODULE W3DTextDrawing
#include <boost/test/included/unit_test.hpp>
export module engine.gui.w3d.text_view.tests;
import std;
import engine.gui.w3d.text_view;
import engine.gui.w3d.value_view;
import engine.gui.w3d.input_view;
import engine.gui.w3d.input_capture;
import engine.gui.w3d.text_edit;
import engine.gui.w3d.text_edit_view;
import engine.gui.w3d.button_view;
import engine.gui.w3d.popup_view;
import engine.gui.w3d.marquee_view;
import engine.gui.w3d.marquee;
import engine.gui.w3d.list_view;
import engine.gui.w3d.list_selection;
import engine.gui.w3d.scrollbar;
import engine.gui.w3d.scrollbar_view;
import engine.gui.w3d.value_controls;
import engine.gui.w3d.menu_entry;
import engine.gui.text.font_face;
import Assets.Fonts;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import engine.gui.images;
import Graphics.Renderer2D;
import Graphics.Tests.Device;
import Graphics.Scene.Props.Renderer;
import engine.platform;
import engine.platform.adapters.sdl3;

BOOST_AUTO_TEST_CASE(scrollbar_source_geometry_uvs_grab_state_and_degenerate_layout_are_independently_checked) {
    using namespace engine::gui::w3d;
    ScrollSkin skin{{100,212,121,234},{76,212,98,234},{52,212,74,234},{28,212,50,234},
        {233,70,254,136},{233,138,254,204},{255,255},{1,1},10,10};
    ScrollBarModel model;model.Range(10,20);model.Set(15);
    auto layout=Layout_Scrollbar(model,{100.75f,20.5f,110.75f,220.5f},skin);BOOST_REQUIRE(layout);
    BOOST_TEST(layout->client.left==100);BOOST_TEST(layout->geometry.previous.left==95);
    BOOST_TEST(layout->geometry.previous.top==10);BOOST_TEST(layout->geometry.previous.right==116);
    BOOST_TEST(layout->geometry.next.top==208);BOOST_TEST(layout->geometry.next.bottom==230);
    BOOST_TEST(layout->geometry.track.top==32);BOOST_TEST(layout->geometry.track.bottom==142);
    BOOST_TEST(layout->geometry.thumb.left==88.75f);BOOST_TEST(layout->geometry.thumb.right==109.75f);
    BOOST_TEST(layout->geometry.thumb.top==87);BOOST_TEST(layout->geometry.thumb.bottom==153);
    auto patches=Scrollbar_Patches(model,*layout,skin);BOOST_REQUIRE(patches);
    BOOST_CHECK_CLOSE((*patches)[0].uv.top,70.f/255,0.0001f);
    BOOST_REQUIRE(model.PointerDown(100,100,layout->geometry));
    patches=Scrollbar_Patches(model,*layout,skin);BOOST_REQUIRE(patches);BOOST_CHECK_CLOSE((*patches)[0].uv.top,138.f/255,0.0001f);
    model.PointerUp();model.Range(10,10);layout=Layout_Scrollbar(model,{100,20,110,40},skin);BOOST_REQUIRE(layout);
    BOOST_TEST(layout->geometry.track.top==layout->geometry.track.bottom);BOOST_TEST(layout->geometry.thumb.top==32);
    BOOST_TEST(model.PointerDown(100,60,layout->geometry));BOOST_CHECK(model.Interaction()==ScrollInteraction::Idle);
    BOOST_TEST(!model.PointerMove(100,layout->geometry));model.PointerUp();
    skin.width=std::numeric_limits<float>::infinity();BOOST_TEST(!Layout_Scrollbar(model,{100,20,110,220},skin));
    skin.width=10;skin.centered_thumb=skin.flip_thumb_below_midpoint=true;
    skin.previous_up={147,228,169,241};skin.thumb_up={171,224,193,233};model.Range(0,10);model.Set(10);
    layout=Layout_Scrollbar(model,{100,20,110,220},skin);BOOST_REQUIRE(layout);
    BOOST_TEST(layout->geometry.thumb.left==94);patches=Scrollbar_Patches(model,*layout,skin);BOOST_REQUIRE(patches);
    BOOST_TEST((*patches)[0].uv.top>(*patches)[0].uv.bottom);
}

BOOST_AUTO_TEST_CASE(scrollbar_uses_shared_image_binding_and_gradient_quad_rendering_without_advancing_state) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,128,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,128,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    // Independent color-coded atlas: up=red, down=green, thumb=yellow.
    std::array<std::byte,8*8*4> atlas{};
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        atlas[(y*8+x)*4]=std::byte(x<4 ? 255 : 0);
        atlas[(y*8+x)*4+1]=std::byte(y>=4 || x>=4 ? 255 : 0);atlas[(y*8+x)*4+3]=std::byte{255};
    }
    const auto texture=ui.Register_Texture({Graphics::TextureHandle(916,1),8,8,32,1,atlas});BOOST_REQUIRE(texture.index.Is_Valid());
    const ScrollSkin skin{{0,0,3,3},{4,0,7,3},{4,0,7,3},{0,0,3,3},{0,4,3,7},{4,4,7,7},
        {8,8},{2,2},4,1};
    ScrollBarModel model;model.Range(0,10);model.Set(5);
    const auto layout=Layout_Scrollbar(model,{28,8,32,120},skin);BOOST_REQUIRE(layout);
    auto& commands=device.Immediate_Command_List();std::array<std::byte,64*128*4> pixels{};
    for(unsigned state=0;state<2;++state) {
        if(state) BOOST_REQUIRE(model.PointerDown(30,8,layout->geometry));
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,128);
        BOOST_REQUIRE(ui.Add_Rect({25,40,28,85},{0,1,0,.25f})); // Parent list region beneath the protruding thumb.
        BOOST_REQUIRE(Draw_Scrollbar(ui,model,*layout,skin,texture,{1,0,0,1},10.f/255));
        BOOST_TEST(model.position.Get()==5); // GPU redraws cannot repeat held input.
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,128}));BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
        const auto c=[&](unsigned x,unsigned y,unsigned channel){return std::to_integer<int>(pixels[(y*64+x)*4+channel]);};
        BOOST_TEST(c(30,9,0)==(state ? 0 : 255));BOOST_TEST(c(30,9,1)==(state ? 255 : 0));
        BOOST_TEST(c(30,117,1)==255);BOOST_TEST(c(28,64,0)==255);BOOST_TEST(c(28,64,1)==255);
        BOOST_TEST(c(26,64,0)==255);BOOST_TEST(c(26,64,1)==255);BOOST_TEST(c(26,64,2)==0);
        BOOST_TEST(c(30,30,0)>c(30,55,0));BOOST_TEST(c(30,90,0)>c(30,75,0));
        BOOST_TEST(c(24,64,2)==255);BOOST_TEST(!ui.Get_Clip().enabled);
    }
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}
BOOST_AUTO_TEST_CASE(list_scrollbar_visibility_preserves_exact_fit_boundary_and_remeasures_reduced_client_width) {
    using namespace engine::gui::w3d;
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("ScrollList",4,false,4,0,{{65,4,5,std::vector<std::uint8_t>(16,255)},
        {87,2,3,std::vector<std::uint8_t>(8,255)}})));
    const std::array rows{ListRow{u"A"},ListRow{u"A"},ListRow{u"A"}};
    const auto layout=Build_List_Layout(font,font,rows,{0,0,40,20},{1,0,2,false,6});BOOST_REQUIRE(layout);
    BOOST_REQUIRE(layout->scrollbar);BOOST_TEST(layout->bounds.right==34);BOOST_TEST(layout->scrollbar->right==40);
    BOOST_TEST(layout->text.bottom-layout->text.top==18);BOOST_TEST(layout->heights[0]==6);
    const auto shorter=Build_List_Layout(font,font,std::span(rows).first(2),{0,0,40,20},{1,0,2,false,6});BOOST_REQUIRE(shorter);
    BOOST_TEST(!shorter->scrollbar);BOOST_TEST(shorter->bounds.right==40);
    const auto empty=Build_List_Layout(font,font,{}, {0,0,40,20},{1,0,2,false,6});BOOST_REQUIRE(empty);BOOST_TEST(!empty->scrollbar);
    BOOST_TEST(!Build_List_Layout(font,font,rows,{0,0,40,20},{1,0,2,false,std::numeric_limits<float>::quiet_NaN()}));
}

BOOST_AUTO_TEST_CASE(text_edit_view_source_insets_selection_caret_blink_and_pointer_midpoints_match_gpu_pixels) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,24,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,24,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Edit",4,false,4,0,{{73,1,2,std::vector<std::uint8_t>(4,255)},
        {65,4,5,std::vector<std::uint8_t>(16,255)}})));
    TextEditModel model;BOOST_REQUIRE(model.Restore(u"AA"));model.Focus(true,0);TextEditViewState state;
    auto layout=BuildTextEditLayout(model,font,{4,4,60,20},state);BOOST_REQUIRE(layout);
    BOOST_TEST(layout->client.left==6);BOOST_TEST(layout->client.right==58);BOOST_TEST(layout->client.top==6);BOOST_TEST(layout->client.bottom==18);
    BOOST_TEST(TextEditHit(*layout,8.5f)==0u);BOOST_TEST(TextEditHit(*layout,8.6f)==1u);BOOST_TEST(TextEditHit(*layout,50)==2u);
    const ValuePalette palette{{1,1,0,1},{0,0,0,1},{1,0,0,1},{0,0,1,1},{0,.2f,0,1}};
    auto& commands=device.Immediate_Command_List();std::array<std::byte,64*24*4> pixels{};
    const auto channel=[&](unsigned x,unsigned y,unsigned c){return std::to_integer<int>(pixels[(y*64+x)*4+c]);};
    for(unsigned phase=0;phase<3;++phase) {
        if(phase==1) model.Tick(501);if(phase==2) model.Focus(false,502);
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,24);
        BOOST_REQUIRE(DrawTextEdit(model,font,ui,{4,4,60,20},palette,state));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,24}));BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
        BOOST_TEST(channel(59,19,0)==255);BOOST_TEST(channel(59,19,1)==255);BOOST_TEST(channel(3,4,2)==255);
        BOOST_TEST(channel(6,10,0)==255);BOOST_CHECK_SMALL(channel(6,10,1)-(phase<2 ? 51 : 0),1);
        BOOST_TEST(channel(16,8,0)==(phase==0 ? 255 : 0));BOOST_TEST(!ui.Get_Clip().enabled);
    }
    BOOST_REQUIRE(model.Restore(std::u16string(30,u'A')));model.Focus(true,600);
    layout=BuildTextEditLayout(model,font,{4,4,60,20},state);BOOST_REQUIRE(layout);BOOST_TEST(state.scroll>0u);
    BOOST_TEST(layout->positions[layout->caret]-layout->positions[state.scroll]<layout->client.right-layout->client.left);
    BOOST_TEST(!DrawTextEdit(model,font,ui,{0,0,2,2},palette,state));
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(input_capture_view_centers_and_shadows_caption_then_adds_focus_inside_border) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({32,24,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({32,24,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Capture",4,false,4,0,{{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    InputCaptureModel model;model.Restore({17,u"A"});
    const ValuePalette palette{{1,1,0,1},{0,0,0,1},{1,0,0,1},{0,0,1,1},{0,.2f,0,1}};
    auto& commands=device.Immediate_Command_List();std::array<std::byte,32*24*4> pixels{};
    const auto channel=[&](unsigned x,unsigned y,unsigned c){return std::to_integer<int>(pixels[(y*32+x)*4+c]);};
    for(bool focused:{false,true}) {
        model.Focus(focused,0);BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
        BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(32,24);
        BOOST_REQUIRE(DrawInputCapture(model,font,ui,{4,4,28,20},palette));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,32,24}));
        BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
        BOOST_TEST(channel(27,19,0)==255);BOOST_TEST(channel(27,19,1)==255);
        BOOST_TEST(channel(4,4,0)==0); // source fills over the outline's top/left edges
        BOOST_TEST(channel(3,4,2)==255);BOOST_TEST(channel(6,6,0)==0);
        BOOST_CHECK_SMALL(channel(6,6,1)-(focused ? 51 : 0),1);
        unsigned red{},shadow{};
        for(unsigned y=6;y<18;++y) for(unsigned x=10;x<21;++x) {
            if(channel(x,y,0)==255) ++red;
            if(channel(x,y,2)==255 && channel(x,y,0)==0) ++shadow;
        }
        BOOST_TEST(red==16u);BOOST_TEST(shadow>0u);
        BOOST_TEST(channel(15,11,0)==255);BOOST_CHECK_SMALL(channel(15,11,1)-(focused ? 51 : 0),1);
        BOOST_TEST(!ui.Get_Clip().enabled);
    }
    BOOST_TEST(!DrawInputCapture(model,font,ui,{0,0,2,4},palette));
    BOOST_TEST(!DrawInputCapture(model,font,ui,{0,0,std::numeric_limits<float>::infinity(),4},palette));
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(marquee_layout_reuses_word_wrapping_font_roles_and_source_visible_row_anchors) {
    using namespace engine::gui::w3d;
    engine::gui::text::FontFace regular,bold;
    BOOST_REQUIRE(regular.Build(Assets::FontAsset("Regular",4,false,4,0,
        {{32,0,2,{}},{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    BOOST_REQUIRE(bold.Build(Assets::FontAsset("Bold",6,true,6,0,{{65,5,6,std::vector<std::uint8_t>(30,255)}})));
    const std::array<const engine::gui::text::FontFace*,2> fonts{&regular,&bold};
    const auto lines=ReadMarqueeLines(u"A A A\n<bold><color=255,0,0>A",0xff00ff00);BOOST_REQUIRE(lines);
    const auto client=MarqueeClient({2.75f,3.75f,28.5f,23.5f},{5,3.75f});BOOST_REQUIRE(client);
    BOOST_TEST(client->left==7);BOOST_TEST(client->top==7);BOOST_TEST(client->right==23);BOOST_TEST(client->bottom==19);
    const auto layout=Build_Marquee_Layout(*lines,fonts,{8,4,18,16});BOOST_REQUIRE(layout);
    BOOST_REQUIRE_EQUAL(layout->rows.size(),4);BOOST_TEST(layout->height==18);
    BOOST_TEST(layout->rows[0].width==5);BOOST_TEST(layout->rows[3].height==6);BOOST_TEST(layout->rows[3].font==1u);
    BOOST_TEST(Visible_Marquee_Rows(*layout,0).empty());
    const auto visible=Visible_Marquee_Rows(*layout,18.5f);BOOST_REQUIRE_EQUAL(visible.size(),3);
    BOOST_TEST(visible[0].index==1u);BOOST_TEST(visible[0].top==1.5f);BOOST_TEST(visible[1].top==5);BOOST_TEST(visible[2].top==9);
    BOOST_CHECK(!Build_Marquee_Layout(*lines,{},*client));BOOST_CHECK(!MarqueeClient({0,0,2,2},{2,2}));
}
BOOST_AUTO_TEST_CASE(single_column_list_reuses_shared_wrapping_header_geometry_selection_and_exact_fit_clipping) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,40,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,40,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("List",4,false,4,0,
        {{32,0,2,{}},{65,4,5,std::vector<std::uint8_t>(16,255)},{87,8,9,std::vector<std::uint8_t>(32,255)}})));
    std::vector<ListRow> rows(5,ListRow{u"A",{1,0,0,1}});
    const auto layout=Build_List_Layout(font,font,rows,{0,0,64,40},{1,2,2,true});BOOST_REQUIRE(layout);
    BOOST_TEST(layout->header.left==9);BOOST_TEST(layout->header.right==55);BOOST_TEST(layout->text.top==9);
    BOOST_TEST(layout->heights[0]==6);ListSelectionModel selection;
    BOOST_REQUIRE(selection.Configure(layout->heights,30));selection.Select(1);
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,40);
    const ValuePalette palette{{1,1,0,1},{0,0,0,0},{1,1,1,1},{0,0,0,1},{0,0.2f,0,1}};
    BOOST_REQUIRE(Draw_List(ui,*layout,selection,font,font,u"W",rows,palette,{0,1,0,1}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,40}));
    std::array<std::byte,64*40*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    const auto channel=[&](unsigned x,unsigned y,unsigned c){return std::to_integer<int>(pixels[(y*64+x)*4+c]);};
    BOOST_TEST(channel(11,4,0)==255);BOOST_TEST(channel(11,4,1)==255);
    BOOST_TEST(channel(11,11,0)==255);BOOST_TEST(channel(11,17,0)==255);BOOST_CHECK_SMALL(channel(11,17,1)-51,1);
    BOOST_TEST(channel(11,34,0)==0);BOOST_TEST(channel(11,34,2)==255); // source omits an exactly-fitting row
    BOOST_TEST(channel(3,11,2)==255);BOOST_TEST(!ui.Get_Clip().enabled);
    selection.Select(4);BOOST_TEST(selection.scroll.Get()==0);
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,40);
    BOOST_REQUIRE(Draw_List(ui,*layout,selection,font,font,u"W",rows,palette,{0,1,0,1}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,40}));
    BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    BOOST_TEST(channel(9,34,1)==255); // source selection outline survives an exact fit
    BOOST_TEST(channel(11,34,0)==0);BOOST_TEST(channel(11,34,1)==0);BOOST_TEST(channel(11,34,2)==255);
    const auto wrapped=Build_List_Layout(font,font,std::array{ListRow{u"A A A"}},{0,0,28,40},{1,2,2,true});
    BOOST_REQUIRE(wrapped);BOOST_TEST(wrapped->heights[0]==14);
    BOOST_TEST(!Build_List_Layout(font,font,rows,{0,0,10,10},{1,3,4,true}));
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}
BOOST_AUTO_TEST_CASE(marquee_painting_clips_scrolled_rows_and_reuses_font_shadow_batches) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({32,24,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({32,24,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace regular,bold;
    BOOST_REQUIRE(regular.Build(Assets::FontAsset("Regular",4,false,4,0,
        {{32,0,2,{}},{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    BOOST_REQUIRE(bold.Build(Assets::FontAsset("Bold",6,true,6,0,{{65,5,6,std::vector<std::uint8_t>(30,255)}})));
    const std::array<const engine::gui::text::FontFace*,2> fonts{&regular,&bold};
    const auto lines=ReadMarqueeLines(u"A A A\n<bold><color=255,0,0>A",0xff00ff00);BOOST_REQUIRE(lines);
    const auto layout=Build_Marquee_Layout(*lines,fonts,{8,4,18,16});BOOST_REQUIRE(layout);
    auto& commands=device.Immediate_Command_List();BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
    BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(32,24);ui.Set_Clip(true,{9,5,17,15});
    BOOST_REQUIRE(Draw_Marquee(ui,*layout,fonts,18,{0,0,0,1}));
    BOOST_TEST(ui.Get_Clip().enabled);BOOST_TEST(ui.Get_Clip().rectangle.top==5);
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,32,24}));
    std::array<std::byte,32*24*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
    const auto check=[&](unsigned x,unsigned y,std::array<int,3> color) {
        for(unsigned c=0;c<3;++c) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(y*32+x)*4+c])-color[c],2);
    };
    check(10,7,{0,255,0});check(10,12,{255,0,0});check(10,4,{0,0,255});check(10,15,{0,0,255});
    check(9,13,{0,0,0});check(8,13,{0,0,255});
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(popup_title_layout_retains_source_truncation_and_scaled_padding) {
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Popup",4,false,4,0,{{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    const auto shape=engine::gui::w3d::Layout_Popup(font,u"AA",{2.5f,20.5f,50,44},{10,8},{1.5f,1.25f});
    BOOST_REQUIRE(shape);BOOST_REQUIRE(shape->title);
    BOOST_TEST(shape->title->left==2.5f);BOOST_TEST(shape->title->right==42.5f);
    BOOST_TEST(shape->title->top==-3.5f);BOOST_TEST(shape->title->bottom==20.5f);
    const auto empty=engine::gui::w3d::Layout_Popup(font,u"",{2,20,50,44},{10,8},{1,1});
    BOOST_REQUIRE(empty);BOOST_TEST(!empty->title);
    BOOST_TEST(!engine::gui::w3d::Layout_Popup(font,u"A",{2,20,50,44},{10,8},{0,1}));
}
BOOST_AUTO_TEST_CASE(popup_blackout_covers_the_screen_with_a_translucent_body_and_caption) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,48,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,48,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Popup",4,false,4,0,{{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    const ValuePalette palette{{1,0,0,1},{0,1,0,.25f},{1,0,0,1},{0,0,0,0},{0,0,0,0}};
    auto& commands=device.Immediate_Command_List();
    for(bool darken:{false,true}) {
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,48);
        BOOST_REQUIRE(Draw_Popup(font,ui,u"A",{8,20,50,44},{0,0,64,48},palette,{2,1},{1.5f,2},{0,0,0,.5f},darken));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,48}));
        std::array<std::byte,64*48*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
        const auto check=[&](unsigned x,unsigned y,std::array<int,3> expected) {
            for(unsigned channel=0;channel<3;++channel)
                BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(y*64+x)*4+channel])-expected[channel],2);
        };
        check(60,45,{0,0,darken ? 127 : 255});check(30,25,{0,64,darken ? 95 : 191});
        check(12,14,{255,0,0});check(20,15,{0,0,darken ? 127 : 255});
    }
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(component_frame_uses_source_corner_and_partial_tile_geometry) {
    using namespace engine::gui::w3d;
    const FrameImageSpec spec{{96,51,217,84},{256,256},{10,10},{10,10}};
    const auto patches=Layout_Component_Frame({2,3,47,36},spec);BOOST_REQUIRE(patches);
    BOOST_REQUIRE_EQUAL(patches->size(),14);
    BOOST_TEST((*patches)[0].screen.left==2);BOOST_TEST((*patches)[0].screen.right==12);
    BOOST_TEST((*patches)[0].uv.left==96.0f/256);BOOST_TEST((*patches)[0].uv.bottom==61.0f/256);
    BOOST_TEST((*patches)[4].screen.left==12);BOOST_TEST((*patches)[4].screen.bottom==13);
    BOOST_TEST((*patches)[4].uv.left==151.5f/256);BOOST_TEST((*patches)[4].uv.right==161.5f/256);
    BOOST_TEST((*patches)[8].screen.left==32);BOOST_TEST((*patches)[8].screen.right==37);
    BOOST_TEST((*patches)[8].uv.right==156.5f/256);
    BOOST_TEST((*patches)[10].screen.top==13);BOOST_TEST((*patches)[10].uv.top==62.5f/256);
    BOOST_TEST((*patches)[12].screen.top==23);BOOST_TEST((*patches)[12].screen.bottom==26);
    BOOST_TEST((*patches)[12].uv.bottom==65.5f/256);
    BOOST_TEST((*patches)[13].screen.left==37);BOOST_TEST((*patches)[13].uv.left==207.0f/256);
    BOOST_TEST(!Layout_Component_Frame({0,0,50,50},{{0,0,12,12},{16,16},{10,10},{2,2}}));
    BOOST_TEST(!Layout_Component_Frame({0,0,50,50},{{0,0,16,16},{16,16},{2,2},{0,2}}));
    // Small controls retain the source overlapping corners without tiling loops.
    const auto compact_frame=Layout_Component_Frame({0,0,9,9},spec);BOOST_REQUIRE(compact_frame);BOOST_TEST(compact_frame->size()==4u);
}

BOOST_AUTO_TEST_CASE(component_buttons_render_additive_border_pressed_fill_and_focus_glow) {
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({80,20,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({80,20,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    std::array<std::byte,16*16*4> atlas{};
    for(unsigned index=0;index<256;++index) atlas[index*4+1]=std::byte{80};
    // Zero alpha retains RGB with source ONE/ONE blending, like the retail atlas.
    const auto texture=ui.Register_Texture({Graphics::TextureHandle(750,1),16,16,64,1,atlas});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Button",4,false,4,0,{{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    const ButtonSkin skin{{{0,0,16,16},{16,16},{2,2},{2,2}},{0,0,1,1},{3,3},{1,1}};
    const ValuePalette palette{{},{},{1,0,0,1},{},{}};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(80,20);
    BOOST_REQUIRE(Draw_Component_Button(font,ui,u"",{2,2,18,18},texture,skin,palette,false,false,{},{}));
    BOOST_REQUIRE(Draw_Component_Button(font,ui,u"",{22,2,38,18},texture,skin,palette,false,true,{},{}));
    const std::array<Graphics::Point2D,1> offsets{{{0,-1}}};
    BOOST_REQUIRE(Draw_Component_Button(font,ui,u"A",{42,2,58,18},texture,skin,palette,true,false,{0,.2f,0,1},offsets));
    auto disabled=palette;disabled.text={1,0,0,.5f};
    BOOST_REQUIRE(Draw_Component_Button(font,ui,u"A",{62,2,78,18},texture,skin,disabled,false,false,{},{}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,80,20}));
    std::array<std::byte,80*20*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,80*4));
    const auto check=[&](unsigned x,unsigned y,std::array<int,3> expected) {
        for(unsigned channel=0;channel<3;++channel)
            BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(y*80+x)*4+channel])-expected[channel],2);
    };
    check(3,3,{0,80,255});check(10,10,{0,0,255});check(30,10,{0,80,255});
    check(48,7,{0,51,255});check(48,8,{255,0,0});check(68,8,{128,0,127});
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(gui_image_asset_binding_preserves_pixels_and_owner_across_frames_and_renderer_restart) {
    // A decoded source image exercises the same binding used by WND and W3D,
    // independently of the retail atlas and the renderer's generated textures.
    Assets::AssetCache cache([](const Assets::AssetIdentity&) {
        std::vector<std::byte> tga(21);tga[2]=std::byte{2};tga[12]=tga[14]=std::byte{1};
        tga[16]=std::byte{24};tga[17]=std::byte{32};
        tga[18]=std::byte{17};tga[19]=std::byte{63};tga[20]=std::byte{191};return tga;
    });
    const auto handle=cache.Request_Texture("button.tga");
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    Graphics::TextureHandle owner;
    for(unsigned restart=0;restart<2;++restart) {
        BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
        const auto target=device.Create_Texture({8,8,1,Graphics::RHITextureFormat::RGBA8_UNorm,
            static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
        const auto depth=device.Create_Texture({8,8,1,Graphics::RHITextureFormat::D24_UNorm_S8,
            static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
        const auto first=engine::gui::images::Resolve_Texture(cache,handle,ui);BOOST_REQUIRE(first.index.Is_Valid());
        if(restart==0) owner=first.owner;else BOOST_CHECK(first.owner==owner);
        const auto second=engine::gui::images::Resolve_Texture(cache,handle,ui);
        BOOST_CHECK(second.owner==first.owner);BOOST_CHECK(second.texture==first.texture);BOOST_CHECK(second.index==first.index);
        BOOST_TEST(!engine::gui::images::Resolve_Texture(cache,Assets::TextureAssetHandle{},ui).index.Is_Valid());
        auto& commands=device.Immediate_Command_List();
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(8,8);
        BOOST_REQUIRE(ui.Add_Quad(Graphics::Rect2D{0,0,8,8},Graphics::Rect2D{0,0,1,1},first,Graphics::Color2D{1,1,1,1}));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,8,8}));
        std::array<std::byte,8*8*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,8*4));
        const std::array expected{191,63,17};
        for(unsigned channel=0;channel<3;++channel) BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(4*8+4)*4+channel])-expected[channel],2);
        ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
    }
}

BOOST_AUTO_TEST_CASE(menu_entry_bounds_and_embossed_layers_follow_source_caption_geometry)
{
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({80,16,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({80,16,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Entry",4,false,4,0,
        {{65,4,5,std::vector<std::uint8_t>(16,255)},{87,6,7,std::vector<std::uint8_t>(24,255)}})));
    const auto left=Menu_Entry_Bounds(font,u"A",{2.5f,2.5f,22.5f,10.5f},0x100);
    BOOST_TEST(left.left==2.5f);BOOST_TEST(left.right==14.5f);
    const auto center=Menu_Entry_Bounds(font,u"A",{2.5f,2.5f,22.5f,10.5f},0x300);
    BOOST_TEST(center.left==10);BOOST_TEST(center.right==22);
    const auto anchored=Menu_Entry_Bounds(font,u"A",{2.5f,2.5f,22.5f,10.5f},0x300,true);
    BOOST_TEST(anchored.left==2.5f);BOOST_TEST(anchored.right==14.5f);
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(80,16);
    BOOST_REQUIRE(Draw_Menu_Entry(font,ui,u"A",left,0x100,MenuEntryPhase::Idle,{1,0,0,1},{0,0,0,1},{},{}));
    BOOST_REQUIRE(Draw_Menu_Entry(font,ui,u"A",{24.5f,2.5f,36.5f,10.5f},0x100,MenuEntryPhase::Focused,
        {1,0,0,1},{0,0,0,1},{},{}));
    BOOST_REQUIRE(Draw_Menu_Entry(font,ui,u"A",{46.5f,2.5f,58.5f,10.5f},0x100,MenuEntryPhase::Pressed,
        {1,0,0,1},{0,0,0,1},{},{}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,80,16}));
    std::array<std::byte,80*16*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,80*4));
    const auto channel=[&](unsigned x,unsigned y,unsigned c) {return std::to_integer<int>(pixels[(y*80+x)*4+c]);};
    BOOST_TEST(channel(3,4,0)==255);BOOST_TEST(channel(3,4,2)==0);
    BOOST_TEST(channel(24,3,0)==255);BOOST_TEST(channel(25,4,0)==0);BOOST_TEST(channel(25,4,2)==0);
    BOOST_TEST(channel(47,4,0)==0);BOOST_TEST(channel(47,4,2)==0);BOOST_TEST(channel(51,8,2)==0);
    BOOST_TEST(channel(52,8,2)==255);
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(radial_text_effect_accepts_caller_geometry_and_preserves_ring_order)
{
    using engine::gui::w3d::Radial_Text_Offsets;
    const auto offsets=Radial_Text_Offsets(4,7,{2,2},{1,1});
    BOOST_REQUIRE_EQUAL(offsets.size(),28);
    for(unsigned ring=0;ring<4;++ring) {
        BOOST_CHECK_SMALL(offsets[ring*7].x-float(ring+2),0.00001f);
        BOOST_CHECK_SMALL(offsets[ring*7].y,0.00001f);
        // At the second angular sample: cos(2pi/7), sin(2pi/7).
        BOOST_CHECK_SMALL(offsets[ring*7+1].x-float(ring+2)*0.623489802f,0.00001f);
        BOOST_CHECK_SMALL(offsets[ring*7+1].y-float(ring+2)*0.781831482f,0.00001f);
    }
    const auto oval=Radial_Text_Offsets(2,4,{2,3},{4,5});
    BOOST_CHECK_SMALL(oval[1].x,0.00001f);BOOST_CHECK_SMALL(oval[1].y-3,0.00001f);
    BOOST_CHECK_SMALL(oval[4].x-6,0.00001f);BOOST_CHECK_SMALL(oval[5].y-8,0.00001f);
    BOOST_TEST(Radial_Text_Offsets(0,7,{2,2},{1,1}).empty());
    BOOST_CHECK_THROW(Radial_Text_Offsets(65536,7,{2,2},{1,1}),std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(offset_text_adds_source_ink_mask_without_overwriting_the_background)
{
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    std::vector<std::uint8_t> coverage(16,128);coverage[10]=0;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Effect",4,false,4,0,{{65,4,5,coverage}})));
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    ui.Begin(32,16);
    const std::array<Graphics::Point2D,3> offsets{{{0,0},{0,0},{8,0}}};
    BOOST_REQUIRE(Draw_Text_Offsets(font,ui,u"A",4,4,{0.2f,0,0,1},offsets,Graphics::Renderer2DBlendMode::Additive));
    // The default remains alpha blending for ordinary WND/W3D text.
    BOOST_REQUIRE(Draw_Text(font,ui,u"A",20,4,{0.2f,0,0,1}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,32,16}));
    std::array<std::byte,32*16*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
    const auto channel=[&](unsigned x,unsigned y,unsigned c) {return std::to_integer<int>(pixels[(y*32+x)*4+c]);};
    // Render2DSentence's RGB is white for nonzero coverage, black for zero;
    // Make_Additive uses ONE/ONE, independently of its alpha component.
    BOOST_CHECK_SMALL(channel(5,5,0)-102,2);BOOST_TEST(channel(5,5,2)==255);
    BOOST_CHECK_SMALL(channel(13,5,0)-51,1);BOOST_TEST(channel(13,5,2)==255);
    BOOST_TEST(channel(6,6,0)==0);BOOST_TEST(channel(14,6,0)==0);BOOST_TEST(channel(22,6,0)==0);
    BOOST_CHECK_SMALL(channel(21,5,0)-26,1);BOOST_CHECK_SMALL(channel(21,5,2)-127,1);
    BOOST_TEST(channel(9,5,0)==0);
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(font_atlas_switches_keep_alpha_spacing_and_color_across_frames)
{
    Graphics::GraphicsTestDevice device({true});
    Graphics::Renderer2D renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace main_font,control_font;
    const Assets::FontAsset main_source("Main",4,false,4,0,
        {{32,0,3,{}},{65,4,5,std::vector<std::uint8_t>(16,255)}});
    const Assets::FontAsset control_source("Control",4,false,4,0,
        {{32,0,2,{}},{65,4,6,std::vector<std::uint8_t>(16,128)}});
    BOOST_REQUIRE(main_font.Build(main_source));BOOST_REQUIRE(control_font.Build(control_source));
    auto& commands=device.Immediate_Command_List();
    for(unsigned frame=0;frame<600;++frame) {
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
        BOOST_REQUIRE(commands.Clear({0,0,1,1},0));
        renderer.Begin(32,16);
        const bool control_page=frame>=200 && frame<400;
        const auto& face=control_page ? control_font : main_font;
        BOOST_REQUIRE(engine::gui::w3d::Draw_Text(face,renderer,u"A A",2,2,{1,0,0,1}));
        // A second font in one frame exercises the same atlas switch as a
        // popup over the animated main menu. Returning uses the cached atlas.
        if(control_page) BOOST_REQUIRE(engine::gui::w3d::Draw_Text(main_font,renderer,u"A",24,8,{0,1,0,1}));
        BOOST_REQUIRE(renderer.Execute(device,commands,target,depth,{0,0,32,16}));
        if(frame==0 || frame==200 || frame==399 || frame==400 || frame==599) {
            std::array<std::byte,32*16*4> pixels{};
            BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
            const auto check=[&](unsigned x,unsigned y,std::array<int,3> rgb) {
                for(unsigned c=0;c<3;++c)
                    BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(y*32+x)*4+c])-rgb[c],2);
            };
            check(3,3,control_page ? std::array{128,0,127} : std::array{255,0,0});
            check(7,3,{0,0,255});check(11,3,control_page ? std::array{128,0,127} : std::array{255,0,0});
            if(control_page) check(25,9,{0,255,0});
        }
    }
    renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(rebuilding_a_font_updates_the_existing_atlas_and_keeps_advance_only_pages_empty)
{
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D renderer;
    BOOST_REQUIRE(renderer.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({16,16,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({16,16,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace face;Graphics::Renderer2DTexture first,first_additive;
    auto& commands=device.Immediate_Command_List();
    for(unsigned rebuild=0;rebuild<4;++rebuild) {
        const unsigned alpha=rebuild%2 ? 64 : 255;
        std::vector<std::uint8_t> coverage(16,alpha);coverage[10]=rebuild%2 ? 0 : alpha;
        const Assets::FontAsset source("Resize",4,false,4,0,{{65,4,5,coverage}});
        BOOST_REQUIRE(face.Build(source));
        const auto texture=face.Ensure_Texture(renderer);
        const auto additive=face.Ensure_Texture(renderer,0,engine::gui::text::FontAtlasMode::AdditiveInk);
        if(rebuild==0) first=texture;
        if(rebuild==0) first_additive=additive;
        BOOST_CHECK(texture.owner==first.owner);BOOST_CHECK(texture.texture==first.texture);BOOST_CHECK(texture.index==first.index);
        BOOST_CHECK(additive.owner==first_additive.owner);BOOST_CHECK(additive.texture==first_additive.texture);
        BOOST_CHECK(additive.index==first_additive.index);BOOST_CHECK(additive.owner!=texture.owner);
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},0));
        renderer.Begin(16,16);BOOST_REQUIRE(engine::gui::w3d::Draw_Text(face,renderer,u"A",2,2,{1,0,0,1}));
        BOOST_REQUIRE(engine::gui::w3d::Draw_Text(face,renderer,u"A",8,8,{1,0,0,1},Graphics::Renderer2DBlendMode::Additive));
        BOOST_REQUIRE(renderer.Execute(device,commands,target,depth,{0,0,16,16}));
        std::array<std::byte,16*16*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,16*4));
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(3*16+3)*4])-int(alpha),2);
        BOOST_CHECK_SMALL(std::to_integer<int>(pixels[(3*16+3)*4+2])-int(255-alpha),2);
        BOOST_TEST(std::to_integer<int>(pixels[(9*16+9)*4])==255);
        BOOST_TEST(std::to_integer<int>(pixels[(9*16+9)*4+2])==255);
        BOOST_TEST(std::to_integer<int>(pixels[(10*16+10)*4])==(rebuild%2 ? 0 : 255));
    }
    const Assets::FontAsset blank("Blank",4,false,4,0,{{32,0,3,{}}});BOOST_REQUIRE(face.Build(blank));
    BOOST_CHECK(!face.Ensure_Texture(renderer).index.Is_Valid());
    BOOST_CHECK(!face.Ensure_Texture(renderer,0,engine::gui::text::FontAtlasMode::AdditiveInk).index.Is_Valid());
    renderer.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(font_overlay_survives_scene_materials_and_frame_state_resets)
{
    engine::platform::sdl3::SDL3PlatformAdapter platform;
    engine::platform::WindowConfig config;config.hidden=true;config.size={32,16};
    auto window=platform.windows().create(config);BOOST_REQUIRE(window);
    const auto native=window->native_handle();BOOST_REQUIRE(native);
    Graphics::GraphicsTestDeviceOptions options;options.use_warp=true;
    options.window=native->window;options.width=32;options.height=16;
    Graphics::GraphicsTestDevice device(options);
    Graphics::PropRenderer scene;
    Graphics::Renderer2D ui;
    const auto shaders=Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY);
    BOOST_REQUIRE(scene.Initialize(device,shaders));BOOST_REQUIRE(ui.Initialize(device,shaders));
    const auto target=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({32,16,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    std::array<Graphics::PropVertex,4> vertices{};
    vertices[0].position={-1,-1,0.5f};vertices[1].position={1,-1,0.5f};
    vertices[2].position={1,1,0.5f};vertices[3].position={-1,1,0.5f};
    for(auto& vertex:vertices) vertex.color={0,0,1,1};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    const auto mesh=scene.Create_Mesh(vertices,indices);BOOST_REQUIRE(mesh.Is_Valid());
    Graphics::PropParameters parameters;
    parameters.world=parameters.view=parameters.view_projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    parameters.textured=0;
    Graphics::PropStyle style;style.blend=Graphics::RHIBlendMode::Disabled;
    engine::gui::text::FontFace face;
    BOOST_REQUIRE(face.Build(Assets::FontAsset("Overlay",4,false,4,0,
        {{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    auto& commands=device.Immediate_Command_List();
    for(unsigned frame=0;frame<12;++frame) {
        BOOST_REQUIRE(device.Begin_Frame());BOOST_REQUIRE(commands.Reset_State());
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));
        BOOST_REQUIRE(commands.Set_Viewport({0,0,32,16}));BOOST_REQUIRE(commands.Clear({0,0,0,1},1));
        BOOST_REQUIRE(scene.Draw(commands,mesh,style,parameters,{}));
        ui.Begin(32,16);BOOST_REQUIRE(engine::gui::w3d::Draw_Text(face,ui,u"A",2,2,{1,0,0,1}));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,32,16}));
        std::array<std::byte,32*16*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,32*4));
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(3*32+3)*4]),255);
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(3*32+3)*4+2]),0);
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(8*32+16)*4+2]),255);
        BOOST_REQUIRE(device.End_Frame());
        BOOST_REQUIRE(device.Get_Swap_Chain().Present());
    }
    scene.Destroy_Mesh(mesh);scene.Shutdown();ui.Shutdown();
    device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(glyphs_beyond_the_first_atlas_row_keep_their_uv_coordinates)
{
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({128,96,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({128,96,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    std::vector<Assets::FontGlyphAsset> glyphs{{32,0,8,{}}};
    for(unsigned character=33;character<256;++character) {
        std::vector<std::uint8_t> alpha(24*82);
        for(unsigned y=7;y<75;++y) for(unsigned x=3;x<21;++x) alpha[y*24+x]=255;
        glyphs.push_back({std::uint16_t(character),24,26,std::move(alpha)});
    }
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Atlas rows",40,false,82,0,std::move(glyphs))));
    BOOST_REQUIRE_GT(font.Get_Glyph(121)->uv.top,0.1f);
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    ui.Begin(128,96);BOOST_REQUIRE(engine::gui::w3d::Draw_Text(font,ui,u"S y",2,2,{1,0,0,1}));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,128,96}));
    std::array<std::byte,128*96*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,128*4));
    for(const auto x:{10u,44u}) {
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(20*128+x)*4]),255);
        BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(20*128+x)*4+2]),0);
    }
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(4*128+10)*4+2]),255);
    BOOST_CHECK_EQUAL(std::to_integer<int>(pixels[(20*128+28)*4+2]),255);
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(value_controls_keep_source_pixel_geometry_and_caption_hit_extents)
{
    using namespace engine::gui::w3d;
    SliderValueModel slider;slider.Range(-20,80);slider.Set(30);
    auto shape=Layout_Slider(slider,{10.5f,0.5f,110.5f,23},10);BOOST_REQUIRE(shape);
    BOOST_TEST(shape->thumb.left==55);BOOST_TEST(shape->thumb.right==65);
    BOOST_TEST(shape->thumb.top==1);BOOST_TEST(shape->thumb.bottom==21);
    BOOST_TEST(shape->before.left==10.5f);BOOST_TEST(shape->before.right==55);
    BOOST_TEST(shape->before.top==6);BOOST_TEST(shape->before.bottom==16);
    BOOST_TEST(shape->after.left==65);BOOST_TEST(shape->after.right==110.5f);
    slider.Set(-20);shape=Layout_Slider(slider,{10.5f,0.5f,110.5f,23},10);BOOST_REQUIRE(shape);
    BOOST_TEST(shape->thumb.left==5);BOOST_TEST(shape->thumb.right==15);
    slider.Set(80);shape=Layout_Slider(slider,{10.5f,0.5f,110.5f,23},10);BOOST_REQUIRE(shape);
    BOOST_TEST(shape->thumb.left==105);BOOST_TEST(shape->thumb.right==115);
    shape=Layout_Slider(slider,{10.5f,0.5f,110.5f,24},10);BOOST_REQUIRE(shape);
    // An odd integer thumb height uses integer division for each half.
    BOOST_TEST(shape->thumb.top==2);BOOST_TEST(shape->thumb.bottom==22);
    BOOST_TEST(!Layout_Slider(slider,{0,0,0,20},8));
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Geometry",4,false,4,0,
        {{65,4,5,std::vector<std::uint8_t>(16,255)},{87,8,9,std::vector<std::uint8_t>(32,255)}})));
    const auto check=Layout_Check_Box(font,u"AA",{2.5f,3.5f,82.5f,27.5f},2);BOOST_REQUIRE(check);
    BOOST_TEST(check->button.left==2.5f);BOOST_TEST(check->button.right==15.5f);
    BOOST_TEST(check->button.top==8.5f);BOOST_TEST(check->button.bottom==21.5f);
    BOOST_TEST(check->text.left==20);BOOST_TEST(check->text.right==46);BOOST_TEST(check->hit.right==46);
    const auto clipped=Layout_Check_Box(font,u"AAAAAAAAAAAA",{2.5f,3.5f,82.5f,27.5f},2);BOOST_REQUIRE(clipped);
    BOOST_TEST(clipped->text.right==82.5f);BOOST_TEST(!Layout_Check_Box(font,u"A",{2,2,50,26},0));
}

BOOST_AUTO_TEST_CASE(checkbox_background_and_focus_highlight_use_caller_alpha_and_additive_palette)
{
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,32,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,32,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Palette",4,false,4,0,
        {{65,4,5,std::vector<std::uint8_t>(16,255)},{87,8,9,std::vector<std::uint8_t>(32,255)}})));
    CheckValueModel check;
    const ValuePalette palette{{1,0,0,1},{0,1,0,0.25f},{1,0,0,1},{0,0,0,0},{0,0.2f,0,1}};
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));ui.Begin(64,32);
    BOOST_REQUIRE(DrawCheckBox(check,font,ui,u"A",{2,2,50,26},palette,true));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,32}));
    std::array<std::byte,64*32*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    const auto channel=[&](unsigned x,unsigned y,unsigned c) {return std::to_integer<int>(pixels[(y*64+x)*4+c]);};
    BOOST_TEST(channel(6,10,0)==0);BOOST_CHECK_SMALL(channel(6,10,1)-64,1);BOOST_CHECK_SMALL(channel(6,10,2)-191,1);
    BOOST_TEST(channel(21,13,0)==255);BOOST_CHECK_SMALL(channel(21,13,1)-51,1);BOOST_TEST(channel(21,13,2)==0);
    BOOST_TEST(channel(27,13,0)==0);BOOST_CHECK_SMALL(channel(27,13,1)-51,1);BOOST_TEST(channel(27,13,2)==255);
    BOOST_TEST(channel(33,13,1)==0); // Highlight ends at the caption's padded bound.
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(w3d_text_uses_shared_multiline_layout_and_value_views_follow_observable_state)
{
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({128,40,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({128,40,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Controls",4,false,4,0,
        {{65,4,5,std::vector<std::uint8_t>(16,255)},{87,8,9,std::vector<std::uint8_t>(32,255)}})));
    engine::gui::w3d::SliderValueModel slider;slider.Range(0,100);
    engine::gui::w3d::CheckValueModel check;
    auto& commands=device.Immediate_Command_List();
    for(unsigned state=0;state<3;++state) {
        slider.Set(state*50);slider.enabled.Set(state!=2);check.checked.Set(state!=0);
        BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
        ui.Begin(128,40);
        BOOST_REQUIRE(engine::gui::w3d::Draw_Text(font,ui,u"A\nA",2,28,{1,0,0,1}));
        const engine::gui::w3d::ValuePalette palette{{1,0,0,1},{0,0,0,0},{1,0,0,1},{0,0,0,0},{0,0,0,0}};
        auto slider_palette=palette;if(!slider.enabled.Get()) slider_palette.line.red=0.5f;
        BOOST_REQUIRE(engine::gui::w3d::DrawCheckBox(check,font,ui,u"A",{2,2,50,26},palette));
        BOOST_REQUIRE(engine::gui::w3d::DrawSlider(slider,ui,{60,2,112,26},slider_palette,true,8));
        BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,128,40}));
        std::array<std::byte,128*40*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,128*4));
        const auto red=[&](unsigned x,unsigned y) {return std::to_integer<int>(pixels[(y*128+x)*4]);};
        BOOST_CHECK_EQUAL(red(3,29),255);BOOST_CHECK_EQUAL(red(3,33),255);BOOST_CHECK_EQUAL(red(8,29),0);
        BOOST_CHECK_EQUAL(red(8,13),state ? 255 : 0);BOOST_CHECK_EQUAL(red(21,13),255);
        BOOST_CHECK_SMALL(red(60+state*26,5)-(state==2 ? 128 : 255),1);
        if(state==1) BOOST_CHECK_EQUAL(red(60,5),0);
    }
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}

BOOST_AUTO_TEST_CASE(text_boxes_align_each_line_wrap_words_and_restore_intersected_clipping)
{
    using namespace engine::gui::w3d;
    Graphics::GraphicsTestDevice device({true});Graphics::Renderer2D ui;
    BOOST_REQUIRE(ui.Initialize(device,Graphics::Test_Shader_Directory(GRAPHICS_TERRAIN_SHADER_DIRECTORY)));
    const auto target=device.Create_Texture({64,32,1,Graphics::RHITextureFormat::RGBA8_UNorm,
        static_cast<unsigned>(Graphics::RHITextureUsage::RenderTarget)});
    const auto depth=device.Create_Texture({64,32,1,Graphics::RHITextureFormat::D24_UNorm_S8,
        static_cast<unsigned>(Graphics::RHITextureUsage::DepthStencil)});
    engine::gui::text::FontFace font;
    BOOST_REQUIRE(font.Build(Assets::FontAsset("Aligned",4,false,4,0,
        {{32,0,3,{}},{65,4,5,std::vector<std::uint8_t>(16,255)}})));
    engine::gui::text::TextStyle style;style.color={1,0,0,1};style.drop_color={0,1,0,1};style.x_drop=-1;style.y_drop=1;
    auto& commands=device.Immediate_Command_List();
    BOOST_REQUIRE(commands.Set_Render_Targets(target,depth));BOOST_REQUIRE(commands.Clear({0,0,1,1},1));
    ui.Begin(64,32);
    TextBoxOptions centered;centered.alignment=TextAlignment::Center;centered.vertical_center=true;
    BOOST_REQUIRE(Draw_Text_Box(font,ui,u"AA\nA",{2.5f,2.5f,22.5f,18.5f},style,centered));
    TextBoxOptions right;right.alignment=TextAlignment::Right;
    BOOST_REQUIRE(Draw_Text_Box(font,ui,u"AA\nA",{26.5f,2.5f,46.5f,18.5f},style,right));
    TextBoxOptions wrapped;wrapped.wrap=true;wrapped.clip=true;wrapped.left_inset=1;
    BOOST_REQUIRE(Draw_Text_Box(font,ui,u"A A",{2,22,13,30},style,wrapped));
    ui.Set_Clip(true,{51,4,54,8});
    const auto previous=ui.Get_Clip();
    TextBoxOptions clipped;clipped.clip=true;
    BOOST_REQUIRE(Draw_Text_Box(font,ui,u"AA",{50,2,60,10},style,clipped));
    BOOST_TEST(ui.Get_Clip().enabled==previous.enabled);BOOST_TEST(ui.Get_Clip().rectangle.left==51);
    BOOST_REQUIRE(Draw_Text_Box(font,ui,u"A",{0,0,1,1},style,clipped));
    BOOST_TEST(ui.Get_Clip().enabled);BOOST_TEST(ui.Get_Clip().rectangle.left==51);
    ui.Set_Clip(false,{});
    BOOST_CHECK(!Draw_Text_Box(font,ui,u"A",{2,2,1,4},style));
    BOOST_REQUIRE(ui.Execute(device,commands,target,depth,{0,0,64,32}));
    std::array<std::byte,64*32*4> pixels{};BOOST_REQUIRE(device.Readback_Texture(target,pixels,64*4));
    const auto channel=[&](unsigned x,unsigned y,unsigned c) {return std::to_integer<int>(pixels[(y*64+x)*4+c]);};
    // Anchors truncate toward zero. Each multiline row has its own justification.
    BOOST_TEST(channel(7,6,0)==255);BOOST_TEST(channel(12,6,0)==255);BOOST_TEST(channel(10,10,0)==255);BOOST_TEST(channel(9,10,0)==0);
    BOOST_TEST(channel(7,10,0)==0);BOOST_TEST(channel(36,2,0)==255);BOOST_TEST(channel(41,6,0)==255);
    BOOST_TEST(channel(36,6,0)==0);BOOST_TEST(channel(6,7,1)==255); // shadow at (-1,+1)
    BOOST_TEST(channel(3,22,0)==255);BOOST_TEST(channel(3,26,0)==255);BOOST_TEST(channel(8,22,0)==0);
    BOOST_TEST(channel(51,4,0)==255);BOOST_TEST(channel(50,4,0)==0);BOOST_TEST(channel(54,4,0)==0);
    ui.Shutdown();device.Destroy_Texture(target);device.Destroy_Texture(depth);
}
