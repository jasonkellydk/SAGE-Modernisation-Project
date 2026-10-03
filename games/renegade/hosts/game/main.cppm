#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

import std;
import Engine.Core.Math.FixedPresentation;
import engine.platform;
import engine.platform.adapters.sdl3;
import games.renegade.content.install.install_mount;
import games.renegade.content.presentation.asset_paths;
import games.renegade.content.presentation.strings;
import games.renegade.content.presentation.style;
import games.renegade.content.presentation.credits;
import games.renegade.presentation.menu.main_menu;
import games.renegade.presentation.menu.intro;
import games.renegade.presentation.menu.navigation;
import games.renegade.presentation.menu.single_player;
import games.renegade.presentation.menu.pause;
import games.renegade.content.campaign.campaign_flow;
import games.renegade.presentation.scene.level_view;
import games.renegade.session;
import games.renegade.presentation.player.input;
import Graphics.Resources.Loading.Queue;
import Graphics.Frame.RenderServices;
import games.renegade.presentation.menu.audio_settings;
import games.renegade.presentation.menu.audio_preview;
import games.renegade.presentation.menu.audio_preferences;
import games.renegade.presentation.menu.video_settings;
import games.renegade.presentation.menu.performance_settings;
import games.renegade.presentation.menu.controls_settings;
import games.renegade.content.presentation.input_configuration;
import games.renegade.content.presentation.input_profile_store;
import games.renegade.presentation.menu.control_profiles;
import games.renegade.presentation.menu.multiplayer_options;
import engine.gui.w3d.text_edit;
import engine.gui.w3d.text_edit_view;
import engine.gui.w3d.input_capture;
import engine.gui.w3d.input_view;
import Graphics.Resources.Textures.Quality;
import Graphics.Frame.RenderSettings;
import games.renegade.presentation.menu.menu_entry;
import engine.assets.adapters.filesystem.virtual_asset_source;
import engine.filesystem.adapters.pe.resources;
import engine.filesystem.core.text_files;
import engine.gui.w3d.dialog_template;
import engine.gui.w3d.binding;
import engine.gui.w3d.dialog_input;
import engine.gui.w3d.navigation;
import engine.gui.w3d.marquee;
import engine.gui.w3d.marquee_view;
import engine.gui.w3d.dialog_layout;
import engine.gui.w3d.text_view;
import engine.gui.w3d.value_view;
import engine.gui.w3d.button_view;
import engine.gui.w3d.popup_view;
import engine.gui.images;
import engine.gui.text.font_face;
import games.renegade.presentation.menu.movie_audio;
import games.renegade.presentation.menu.movie_gallery;
import engine.audio.decoders.ffmpeg.ffmpeg_decoder;
import engine.audio.adapters.preferred_output;
import Video.FFmpeg.Player;
import Video.Adapters.MemorySource;
import engine.gui.w3d.list_view;
import engine.gui.w3d.list_selection;
import engine.gui.w3d.scrollbar;
import engine.gui.w3d.scrollbar_view;
import Assets.Runtime;
import Assets.Cache;
import Assets.Adapters.W3D;
import Assets.Models;
import Assets.Identity;
import Assets.Fonts;
import Assets.Fonts.Metadata;
import Assets.Textures;
import Assets.Handles;
import Assets.Importers.Models;
import Graphics.Frame.Runtime;
import Graphics.Frame.SceneRenderers;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.StaticDrawOrder;
import Graphics.Scene.Models.AssetPose;
import Graphics.Scene.Views.CameraState;
import Graphics.Renderer2D;
import Graphics.Capture.FrameCapture;
import Graphics.Video.Renderer;
import Graphics.Scene.Screen.Filters;

namespace {
engine::gui::w3d::ValuePalette ControlPalette(bool enabled) {
    const auto palette=enabled ? renegade::content::ControlStyle::enabled : renegade::content::ControlStyle::disabled;
    using Graphics::Color2D;
    return {Color2D::From_ARGB(palette.line),Color2D::From_ARGB(palette.background),Color2D::From_ARGB(palette.text),
        Color2D::From_ARGB(palette.shadow),Color2D::From_ARGB(palette.highlight)};
}
engine::gui::w3d::ButtonSkin ButtonSkin() {
    using Style=renegade::content::ButtonStyle;
    return {{{Style::source_pixels[0],Style::source_pixels[1],Style::source_pixels[2],Style::source_pixels[3]},
        {Style::texture_size[0],Style::texture_size[1]},{Style::corner_size[0],Style::corner_size[1]},
        {Style::tile_size[0],Style::tile_size[1]}},
        {Style::pressed_pixels[0]/Style::texture_size[0],Style::pressed_pixels[1]/Style::texture_size[1],
            Style::pressed_pixels[2]/Style::texture_size[0],Style::pressed_pixels[3]/Style::texture_size[1]},
        {Style::pressed_inset[0],Style::pressed_inset[1]},{Style::pressed_far_edge[0],Style::pressed_far_edge[1]}};
}
engine::gui::w3d::ScrollSkin ScrollSkin(unsigned width,unsigned height) {
    using Style=renegade::content::ScrollStyle;
    const auto rect=[](const auto& value){return Graphics::Rect2D{value[0],value[1],value[2],value[3]};};
    return {rect(Style::previous_up),rect(Style::previous_down),rect(Style::next_up),rect(Style::next_down),
        rect(Style::thumb_up),rect(Style::thumb_down),{Style::uv_divisor,Style::uv_divisor},
        {width/800.f,height/600.f},Style::width*width/800.f,Style::button_offset};
}
engine::gui::w3d::ValueKey TranslateValueKey(engine::platform::KeyCode key) {
    using engine::platform::KeyCode;using engine::gui::w3d::ValueKey;
    switch(key) {
    case KeyCode::left:return ValueKey::Left;case KeyCode::right:return ValueKey::Right;
    case KeyCode::up:return ValueKey::Up;case KeyCode::down:return ValueKey::Down;
    case KeyCode::home:return ValueKey::Home;case KeyCode::end:return ValueKey::End;
    case KeyCode::space:return ValueKey::Space;
    case KeyCode::enter:case KeyCode::keypad_enter:return ValueKey::Enter;
    default:return ValueKey::Other;
    }
}
std::vector<std::byte> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) return {};
    const auto size=file.tellg(); if(size<0) return {};
    std::vector<std::byte> out(static_cast<std::size_t>(size)); file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(out.size()))) return {};
    return out;
}
std::array<float,16> Multiply(const std::array<float,16>& a,const std::array<float,16>& b) {
    std::array<float,16> out{};
    for(unsigned row=0;row<4;++row) for(unsigned col=0;col<4;++col) for(unsigned k=0;k<4;++k)
        out[row*4+col]+=a[row*4+k]*b[k*4+col];
    return out;
}
struct MenuModel {
    Graphics::PropAssetBinding binding;
    Graphics::ModelAssetPose pose;
    Assets::ModelAssetHandle asset;
    float rate{};
    bool animated{};
    bool Rebind(Graphics::Device& device,Assets::AssetCache& cache) {
        const auto quality=Graphics::Get_Texture_Quality_Settings();std::string error;
        if(binding.Load(device,Graphics::Get_Prop_Renderer(),cache,asset,error,{},{},
            static_cast<unsigned>(std::max(quality.mip_reduction,0)),static_cast<unsigned>(std::max(quality.minimum_dimension,1)))) return true;
        std::fprintf(stderr,"model texture replacement: %s\n",error.c_str());return false;
    }
    bool Load(Graphics::Device& device,Assets::AssetCache& cache,std::string_view name,bool animation) {
        const auto handle=cache.Request_Model(std::string(name)+".w3d"); cache.Wait(handle);
        const auto* model=cache.Try_Get_Model(handle);
        std::string error=cache.Get_Error(handle);
        asset=handle;
        if(!model || !Rebind(device,cache)) {
            std::fprintf(stderr,"model %.*s: %s\n",int(name.size()),name.data(),error.c_str()); return false;
        }
        auto rig=model->Rig();
        if(animation) {
            const std::string clip=std::string(name)+"."+std::string(name);
            if(auto external=cache.Load_Rig(Assets::AssetType::Animation,clip,error)) rig.animations=external->animations;
            if(rig.animations.empty()) { std::fprintf(stderr,"animation %s: %s\n",clip.c_str(),error.c_str()); return false; }
            rate=rig.animations.front().frame_rate;
            animated=true;
        }
        if(!rig.bones.empty() && !pose.Initialize(rig,error)) { std::fprintf(stderr,"pose: %s\n",error.c_str());return false; }
        std::printf("model %.*s: %zu parts, %zu bones, %zu animations\n",int(name.size()),name.data(),binding.Part_Count(),rig.bones.size(),rig.animations.size());
        return true;
    }
    bool Sample(float frame,bool loop) { return !animated || pose.Evaluate(0,frame,loop); }
    bool Draw(Graphics::CommandList& commands,Graphics::PropParameters parameters,const Graphics::PropTextureMappingContext& mapping) {
        for(std::size_t part=0;part<binding.Part_Count();++part)
            if(!binding.Draw_Part(commands,part,parameters,pose.Bone_Count() ? &pose : nullptr,0,{},nullptr,&mapping)) return false;
        return true;
    }
};
struct MenuScene {
    std::unique_ptr<renegade::presentation::LevelView> level_view;
    std::unique_ptr<renegade::presentation::PauseMenu> pause;
	bool verify_level_return{};
    unsigned loading_frames{},loading_events{},level_ready_frames{};
    bool capture_loading{},loading_captured{},capture_loading_progress{},loading_progress_captured{};
    struct Entry {
        engine::gui::w3d::MenuEntryModel state{renegade::presentation::menu_entry_timings};
        renegade::presentation::MenuEntryVisual visual;
    };
    std::map<std::uint32_t,std::unique_ptr<Entry>> entries;
    MenuModel backdrop,logo,title,gizmo;
    Graphics::CameraState camera;
    renegade::presentation::MainMenuTransition transition;
    std::filesystem::path capture;
    float seconds{};
    bool capture_next{},captured{},verify_capture{};
    std::optional<std::size_t> capture_step;
    std::optional<std::string> capture_movie;
    Graphics::VideoRenderer video_renderer;
    Engine::Video::DecodedVideoFrame movie_frame;
    std::string movie_name;
    std::set<std::string> captured_movies;
    std::map<std::string,std::set<std::uint64_t>> presented_movie_frames;
    std::map<std::string,std::uint64_t> expected_movie_frames;
    unsigned frames{};
    engine::gui::text::FontFace font;
    engine::gui::text::FontFace loading_font,loading_large_font;
    engine::gui::text::FontFace small_font,control_font,title_font,large_control_font,tooltip_font,header_font;
    engine::gui::text::FontFace list_font;
    renegade::presentation::MovieGallery gallery;
    std::vector<engine::gui::w3d::ListRow> gallery_rows;
    std::optional<engine::gui::w3d::ListLayout> gallery_layout;
    std::optional<engine::gui::w3d::ScrollLayout> gallery_scroll_layout;
    float gallery_pulse{1},gallery_pulse_direction{1};
    bool verify_gallery{},gallery_captured{};
    std::array<engine::gui::text::FontFace,2> credits_fonts;
    std::vector<engine::gui::w3d::MarqueeLine> credits_lines;
    std::optional<engine::gui::w3d::MarqueeLayout> credits_layout;
    engine::gui::w3d::MarqueeScrollModel credits_scroll;
    bool credits_captured{};
    bool verify_credits_cycle{};
    std::set<std::size_t> credits_seen_rows,credits_verified_rows;
    std::optional<std::string> credits_checkpoint;
    renegade::content::MenuStyle style;
    std::unique_ptr<renegade::presentation::AudioSettings> audio_settings;
    std::unique_ptr<renegade::presentation::VideoSettings> video_settings;
    std::unique_ptr<renegade::presentation::PerformanceSettings> performance_settings;
    std::unique_ptr<renegade::presentation::ControlsSettings> controls_settings;
    std::unique_ptr<renegade::content::InputProfileStore> profile_store;
    std::unique_ptr<renegade::presentation::ControlProfiles> control_profiles;
    std::unique_ptr<renegade::presentation::MultiplayerOptions> multiplayer_options;
    std::vector<engine::gui::w3d::ListRow> profile_rows;
    std::optional<engine::gui::w3d::ListLayout> profile_layout;
    std::optional<engine::gui::w3d::ScrollLayout> profile_scroll_layout;
    engine::gui::w3d::TextEditViewState profile_edit_view;
    std::optional<engine::gui::w3d::TextEditLayout> profile_edit_layout;
    engine::gui::w3d::TabSelectionModel controls_tabs{5};
    unsigned performance_texture_replacements{};
    bool check_performance_frame{};unsigned performance_frame_checks{};
    renegade::presentation::VideoRamp video_ramp{};
    Graphics::ColorTransferFrame color_transfer;
    Graphics::ColorTransferFrame::Curve color_curve{};
    std::array<engine::gui::w3d::HitRect,3> video_caption_bounds;
    bool check_video_frame{};unsigned video_frame_checks{};
    bool check_cinematic_effects{},check_tutorial_gates{},check_player_body{},check_objective_hud{},check_speech_pose{};
    renegade::presentation::AudioPreview audio_preview;
    std::array<std::shared_ptr<const engine::audio::PcmBuffer>,4> preview_pcm;
    std::array<engine::audio::VoiceId,4> preview_voices{};
    std::array<unsigned,4> preview_starts{},preview_timeouts{};
    bool preview_assets_valid{true};
    std::uint64_t preview_milliseconds{};
    bool preview_ready{};
    renegade::presentation::TechOptions tech_options;
    std::optional<std::uint32_t> dragging_slider;
    renegade::content::StringCatalog strings;
    engine::gui::w3d::DialogDefinition dialog;
    renegade::presentation::MenuNavigation navigation;
    std::map<std::uint32_t,engine::gui::w3d::DialogDefinition> dialogs;
    std::array<engine::gui::w3d::Control,6> controls;
    std::array<std::unique_ptr<engine::gui::w3d::CommandBinding>,6> commands;
    engine::gui::w3d::DialogInputState input;
    std::vector<engine::gui::w3d::InputTarget> targets;
    std::map<std::uint32_t,renegade::presentation::InterfaceControlKind> interface_kinds;
    std::map<std::uint32_t,unsigned> interface_sound_starts;
    Assets::TextureAssetHandle button_texture;
    Assets::TextureAssetHandle scrollbar_texture;
    unsigned popup_voice_starts{};
    bool movie_visible{};
    std::uint32_t displayed_dialog{128};
    std::optional<std::uint32_t> pending_dialog;
    std::uint32_t targets_dialog{};
    bool focus_pending{true};
    std::optional<std::uint32_t> requested_focus;
};
MenuScene* scene{};
constexpr std::uint32_t list_scrollbar_target=0x20000u+1206;
engine::gui::w3d::ListSelectionModel* ActiveList(MenuScene& menu) {
    if(menu.displayed_dialog==216) return &menu.control_profiles->list;
    if(menu.displayed_dialog==170) return &menu.gallery.list;
    return nullptr;
}
const engine::gui::w3d::ScrollLayout* ActiveScrollLayout(const MenuScene& menu) {
    const auto* layout=menu.displayed_dialog==216 ? &menu.profile_scroll_layout : menu.displayed_dialog==170 ? &menu.gallery_scroll_layout : nullptr;
    return layout && *layout ? &**layout : nullptr;
}
bool MenuCommandEnabled(MenuScene& menu,std::uint32_t id) {
    if(menu.displayed_dialog==136 && (id==1338 || id==1339)) return true;
    if(menu.displayed_dialog==170 && id==1032) return true;
    if(menu.displayed_dialog==209 && (id==6 || id==7)) return true;
    if(menu.displayed_dialog==210 && id==1) return true;
    if(menu.displayed_dialog==216 && id>=1298 && id<=1300) return true;
    return menu.navigation.For(id).enabled.Get();
}
void RefreshProfileRows(MenuScene& menu) {
    menu.profile_rows.clear();menu.profile_layout.reset();menu.profile_scroll_layout.reset();menu.profile_edit_layout.reset();menu.profile_edit_view={};
    menu.control_profiles->list.scrollbar.CaptureLost();
    for(const auto index:menu.control_profiles->Rows()) {
        if(index==menu.profile_store->Catalog().profiles.size()) menu.profile_rows.push_back({menu.strings.Lookup("IDS_MENU_EMPTY_SLOT"),{1,1,1,1}});
        else {const auto& profile=menu.profile_store->Catalog().profiles[index];menu.profile_rows.push_back({profile.name,{1,1,1,1}});}
    }
}
unsigned MenuPage(const MenuScene& menu) {
    if(menu.displayed_dialog==153) return renegade::presentation::PauseMenu::pages[menu.pause->selected_tab.Get()];
    if(menu.displayed_dialog==136) return renegade::presentation::controls_tabs[menu.controls_tabs.position.Get()].resource;
    return menu.displayed_dialog==169 ? menu.tech_options.Resource() : 0;
}
engine::gui::w3d::InputCaptureModel* ControlCapture(MenuScene& menu,std::uint32_t id) {
    return menu.displayed_dialog==136 ? menu.controls_settings->Capture(MenuPage(menu),id) : nullptr;
}
engine::gui::w3d::SliderValueModel* ControlSlider(MenuScene& menu,std::uint32_t id) {
    if(menu.displayed_dialog==136) return MenuPage(menu)==141 && id==1222 ? &menu.controls_settings->sensitivity : nullptr;
    if(menu.displayed_dialog!=169) return nullptr;
    if(menu.tech_options.Resource()==231) if(const auto index=renegade::presentation::AudioSettings::SliderIndex(id))
        return &menu.audio_settings->volumes[*index];
    if(menu.tech_options.Resource()==233) if(const auto index=renegade::presentation::VideoSettings::SliderIndex(id))
        return &menu.video_settings->sliders[*index];
    if(menu.tech_options.Resource()==232) return menu.performance_settings->Slider(id);
    return nullptr;
}
engine::gui::w3d::CheckValueModel* ControlCheck(MenuScene& menu,std::uint32_t id) {
    if(menu.displayed_dialog==136) {
        if(MenuPage(menu)==139 && id==1179) return &menu.controls_settings->damage_indicators;
        if(MenuPage(menu)==141) switch(id) {
        case 1180:return &menu.controls_settings->invert;
        case 1181:return &menu.controls_settings->invert_2d;
        case 1182:return &menu.controls_settings->camera_locked;
        }
        return nullptr;
    }
    if(menu.displayed_dialog==166) return menu.multiplayer_options->Check(id);
    if(menu.displayed_dialog!=169) return nullptr;
    if(menu.tech_options.Resource()==231) if(const auto index=renegade::presentation::AudioSettings::CheckIndex(id))
        return &menu.audio_settings->enabled[*index];
    if(menu.tech_options.Resource()==232) return menu.performance_settings->Check(id);
    return nullptr;
}
MenuScene::Entry& EntryFor(MenuScene& menu,std::uint32_t id) {
    auto& entry=menu.entries[id];if(!entry) entry=std::make_unique<MenuScene::Entry>();return *entry;
}
bool DrawEntry(const engine::gui::text::FontFace& font,Graphics::Renderer2D& renderer,std::u16string_view label,
    engine::gui::w3d::HitRect bounds,std::uint32_t style,MenuScene::Entry& entry) {
    using namespace engine::gui::w3d;
    using Glow=renegade::content::MenuGlowStyle;
    const auto step=Glow::RadiusStep(entry.visual.radius_x,entry.visual.radius_y);
    const auto offsets=Radial_Text_Offsets(Glow::rings,Glow::samples,{Glow::initial_radius,Glow::initial_radius},{step[0],step[1]});
    return Draw_Menu_Entry(font,renderer,label,bounds,style,entry.state.phase.Get(),
        Graphics::Color2D::From_ARGB(Glow::text),{0,0,0,1},Graphics::Color2D::From_ARGB(entry.visual.glow),offsets);
}
bool DrawButton(MenuScene& menu,Graphics::Renderer2D& renderer,const engine::gui::text::FontFace& font,
    std::u16string_view label,engine::gui::w3d::HitRect bounds,std::uint32_t id,bool enabled) {
    auto* cache=Assets::Try_Get_Asset_Cache();if(!cache) return false;
    const auto texture=engine::gui::images::Resolve_Texture(*cache,menu.button_texture,renderer);
    using Glow=renegade::content::MenuGlowStyle;using Style=renegade::content::ButtonStyle;
    const auto step=Glow::RadiusStep(Style::glow_radius,Style::glow_radius);
    const auto offsets=engine::gui::w3d::Radial_Text_Offsets(Glow::rings,Glow::samples,
        {Glow::initial_radius,Glow::initial_radius},{step[0],step[1]});
    return engine::gui::w3d::Draw_Component_Button(font,renderer,label,{bounds.left,bounds.top,bounds.right,bounds.bottom},
        texture,ButtonSkin(),ControlPalette(enabled),menu.input.focused==id,
        menu.input.pressed==id && menu.input.hovered==id,Graphics::Color2D::From_ARGB(Style::glow),offsets);
}
const engine::gui::text::FontFace& ControlFont(const MenuScene& menu,renegade::content::FontRole role) {
    using renegade::content::FontRole;
    switch(role) {
    case FontRole::Menu:return menu.font;
    case FontRole::SmallMenu:return menu.small_font;
    case FontRole::Title:return menu.title_font;
    case FontRole::LargeControls:return menu.large_control_font;
    case FontRole::Tooltips:return menu.tooltip_font;
    default:return menu.control_font;
    }
}
bool DrawControlText(const engine::gui::text::FontFace& font,Graphics::Renderer2D& renderer,
    std::u16string_view label,engine::gui::w3d::HitRect bounds,std::uint32_t flags,bool title,bool button,
    Graphics::Color2D color) {
    using namespace engine::gui::w3d;
    TextBoxOptions options;options.left_inset=1;
    options.alignment=title || (flags&15)==1 ? TextAlignment::Center : (flags&15)==2 ? TextAlignment::Right : TextAlignment::Left;
    options.vertical_center=title || button || (flags&0x200)!=0;
    options.wrap=!title && !button && (flags&15)!=12;
    options.clip=!title && !button;
    engine::gui::text::TextStyle style;style.color=title ? Graphics::Color2D{1,1,36/255.0f,1} : color;
    style.drop_color=title ? Graphics::Color2D{0,0,0,0} : Graphics::Color2D{0,0,0,1};
    style.x_drop=-1;style.y_drop=1;
    if(title && !label.empty()) {
        using Glow=renegade::content::MenuGlowStyle;
        const auto size=Measure_Text(font,label);if(!size) return false;
        const float x=std::trunc(bounds.left+(bounds.right-bounds.left-(*size)[0])*0.5f);
        const float y=std::trunc(bounds.top+(bounds.bottom-bounds.top-(*size)[1])*0.5f);
        const auto step=Glow::RadiusStep(5,5);
        const auto offsets=Radial_Text_Offsets(Glow::rings,Glow::samples,{Glow::initial_radius,Glow::initial_radius},{step[0],step[1]});
        if(!Draw_Text_Offsets(font,renderer,label,x,y,Graphics::Color2D::From_ARGB(Glow::title_glow),offsets,
            Graphics::Renderer2DBlendMode::Additive)) return false;
    }
    return Draw_Text_Box(font,renderer,label,{bounds.left,bounds.top,bounds.right,bounds.bottom},style,options);
}
Graphics::Color2D MainEntryColor() {
    return Graphics::Color2D::From_ARGB(renegade::content::MenuGlowStyle::text);
}
int DisplayChannel(const MenuScene& menu,int value) {
    return static_cast<int>(std::lround(menu.video_ramp[std::clamp(value,0,255)]*255.0/65535));
}
bool VerifyMenuLabels(const MenuScene& menu,const Engine::Video::DecodedVideoFrame& frame) {
    if(menu.displayed_dialog!=128 || !menu.transition.Done()) return false;
    for(std::size_t index=0;index<6;++index) {
        const auto id=menu.dialog.controls[index].id;
        const auto target=std::ranges::find_if(menu.targets,[id](const auto& hit) {return hit.id==id;});
        if(target==menu.targets.end()) return false;
        const auto color=MainEntryColor();
        const std::array expected{int(std::lround(color.red*255)),int(std::lround(color.green*255)),int(std::lround(color.blue*255))};
        const auto entry=menu.entries.find(id);
        const bool embossed=entry!=menu.entries.end() && entry->second->state.phase.Get()==engine::gui::w3d::MenuEntryPhase::Focused;
        unsigned coverage=0;
        const auto& rect=target->bounds;
        const int left=std::clamp(int(std::ceil(rect.left)),0,int(frame.width));
        const int right=std::clamp(int(std::floor(rect.right)),left,int(frame.width));
        const int top=std::clamp(int(std::ceil(rect.top)),0,int(frame.height));
        const int bottom=std::clamp(int(std::floor(rect.bottom)),top,int(frame.height));
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const auto offset=std::size_t(y)*frame.row_pitch+std::size_t(x)*4;
            bool matches=true;
            for(unsigned channel=0;channel<3;++channel)
                matches &= std::abs(std::to_integer<int>(frame.pixels[offset+channel])-DisplayChannel(menu,expected[channel]))<=2;
            // The source focus style overlays a black face on offset gold text.
            // Its visible antialiased rim retains the gold green:blue ratio;
            // red may also include the additive red glow. Keep a bright-edge
            // floor to exclude the retail backdrop's dark brown/grey pixels.
            if(embossed) {
                const int red=std::to_integer<int>(frame.pixels[offset]);
                const int green=std::to_integer<int>(frame.pixels[offset+1]);
                const int blue=std::to_integer<int>(frame.pixels[offset+2]);
                if(red>=DisplayChannel(menu,220)) for(int source_green=160;source_green<=expected[1];++source_green)
                    matches |= std::abs(green-DisplayChannel(menu,source_green))<=2 &&
                        std::abs(blue-DisplayChannel(menu,int(std::lround(double(source_green)*expected[2]/expected[1]))))<=2;
            }
            coverage+=matches;
        }
        if(coverage<32) {std::fprintf(stderr,"menu capture: label %u has only %u expected-color pixels\n",id,coverage);return false;}
    }
    std::printf("GPU menu label coverage passed for all six entries\n");
    return true;
}
bool VerifyGalleryImage(const MenuScene& menu,const Engine::Video::DecodedVideoFrame& frame) {
    if(!menu.gallery_layout || menu.gallery.list.selected.Get()!=0 || menu.gallery_rows.empty()) return false;
    const auto& layout=*menu.gallery_layout;
    const auto white_pixels=[&](engine::gui::w3d::HitRect bounds) {
        unsigned count{};
        const int left=std::clamp(int(std::ceil(bounds.left)),0,int(frame.width));
        const int right=std::clamp(int(std::floor(bounds.right)),left,int(frame.width));
        const int top=std::clamp(int(std::ceil(bounds.top)),0,int(frame.height));
        const int bottom=std::clamp(int(std::floor(bounds.bottom)),top,int(frame.height));
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const auto at=std::size_t(y)*frame.row_pitch+x*4;bool white=true;
            for(unsigned c=0;c<3;++c) white &= std::to_integer<unsigned>(frame.pixels[at+c])>=250;
            count+=white;
        }
        return count;
    };
    // Exclude the header underline and selected row outline from text coverage.
    const float header_top=std::trunc(layout.header.top+(layout.header.bottom-layout.header.top-menu.header_font.Height())*0.5f);
    const float row_top=std::trunc(layout.text.top+(layout.heights[0]-menu.list_font.Height())*0.5f);
    const auto header_extent=engine::gui::w3d::Measure_Text(menu.header_font,menu.strings.Lookup("IDS_MOVIE_COL_HEADER"));
    const auto row_extent=engine::gui::w3d::Measure_Text(menu.list_font,menu.gallery_rows[0].text);
    if(!header_extent || !row_extent ||
        white_pixels({layout.header.left+1,header_top,layout.header.left+(*header_extent)[0]-1,header_top+menu.header_font.Height()})<32 ||
        white_pixels({layout.text.left+1,row_top,layout.text.left+(*row_extent)[0]-1,row_top+menu.list_font.Height()})<32) {
        std::fprintf(stderr,"gallery header or intro caption lacks white text pixels\n");return false;
    }
    std::printf("GPU gallery list image passed: header and selected retail intro caption\n");return true;
}
bool VerifyCreditsImage(const MenuScene& menu,const Engine::Video::DecodedVideoFrame& frame,
    std::set<std::size_t>* verified=nullptr) {
    if(!menu.credits_layout) return false;
    const auto& layout=*menu.credits_layout;std::set<unsigned> fonts;unsigned rows{};
    for(const auto& position:engine::gui::w3d::Visible_Marquee_Rows(layout,menu.credits_scroll.position.Get())) {
        const auto& row=layout.rows[position.index];
        if(row.placements.empty() || position.top<layout.client.top || position.top+row.height>layout.client.bottom) continue;
        if(verified && verified->contains(position.index)) continue;
        const std::array expected{int((row.color>>16)&255),int((row.color>>8)&255),int(row.color&255)};
        unsigned coverage{};
        const int left=std::clamp(int(std::ceil(layout.client.left)),0,int(frame.width));
        const int right=std::clamp(int(std::floor(layout.client.right)),left,int(frame.width));
        const int top=std::clamp(int(std::trunc(position.top)),0,int(frame.height));
        const int bottom=std::clamp(top+int(row.height),top,int(frame.height));
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const auto offset=std::size_t(y)*frame.row_pitch+std::size_t(x)*4;bool match=true;
            for(unsigned c=0;c<3;++c) match &= std::abs(std::to_integer<int>(frame.pixels[offset+c])-DisplayChannel(menu,expected[c]))<=2;
            coverage+=match;
        }
        if(coverage<16) {std::fprintf(stderr,"credits row %zu has only %u source-color pixels\n",position.index,coverage);return false;}
        fonts.insert(row.font);++rows;
        if(verified) verified->insert(position.index);
    }
    if(verified) return true;
    if(rows<3 || fonts.size()!=2) {std::fprintf(stderr,"credits capture must cover formatted text in both retail fonts\n");return false;}
    std::printf("GPU credits image passed: %u fully visible source-color rows in both fonts\n",rows);return true;
}
bool VerifyPerformanceImage(MenuScene& menu,const Engine::Video::DecodedVideoFrame& frame) {
    if(menu.displayed_dialog!=169 || menu.tech_options.Resource()!=232) return false;
    const bool expert=menu.performance_settings->expert.checked.Get();unsigned controls{};
    for(const auto id:std::array{1008u,1007u,1017u,1010u,1012u,1013u,1015u,1009u,1011u}) {
        const auto target=std::ranges::find_if(menu.targets,[&](const auto& item){return item.id==id;});
        const bool visible=id==1008 || id==1007 || expert;
        if((target!=menu.targets.end())!=visible) {std::fprintf(stderr,"performance visibility differs for %u\n",id);return false;}
        if(!visible) continue;
        if(target->enabled!=(id!=1011)) {std::fprintf(stderr,"performance capability differs for %u\n",id);return false;}
        const auto color=target->enabled ? renegade::content::ControlStyle::enabled.line : renegade::content::ControlStyle::disabled.line;
        const std::array expected{int((color>>16)&255),int((color>>8)&255),int(color&255)};
        auto bounds=target->bounds;
        // Source thumbs can extend half a thumb beyond the slider client.
        bounds.left-=8;bounds.right+=8;bounds.top-=12;bounds.bottom+=12;
        unsigned ink{};
        for(int y=std::max(0,int(bounds.top));y<std::min(int(frame.height),int(bounds.bottom));++y)
            for(int x=std::max(0,int(bounds.left));x<std::min(int(frame.width),int(bounds.right));++x) {
                const auto offset=std::size_t(y)*frame.row_pitch+x*4;bool match=true;
                for(unsigned c=0;c<3;++c) {
                    const float alpha=((color>>24)&255)/255.f;
                    const int lower=DisplayChannel(menu,int(std::floor(expected[c]*alpha)));
                    const int upper=DisplayChannel(menu,int(std::ceil(expected[c]*alpha+255*(1-alpha))));
                    const int actual=std::to_integer<int>(frame.pixels[offset+c]);
                    match &= actual>=lower-2 && actual<=upper+2;
                }
                ink+=match;
            }
        if(ink<8) {std::fprintf(stderr,"performance control %u has only %u outline pixels\n",id,ink);return false;}
        ++controls;
    }
    std::printf("GPU performance controls passed: expert=%u, %u visible value controls, unsupported NPatches disabled\n",unsigned(expert),controls);return true;
}
bool VerifyVideoCalibration(const MenuScene& menu,const Engine::Video::DecodedVideoFrame& source,
    const Engine::Video::DecodedVideoFrame& output) {
    if(!source.Is_Valid() || source.width!=output.width || source.height!=output.height ||
        menu.displayed_dialog!=169 || menu.tech_options.Resource()!=233) return false;
    unsigned changed{};
    for(unsigned gy=1;gy<16;++gy) for(unsigned gx=1;gx<16;++gx) {
        const auto x=gx*source.width/16,y=gy*source.height/16;bool different=false;
        for(unsigned c=0;c<3;++c) {
            const int raw=std::to_integer<int>(source.pixels[std::size_t(y)*source.row_pitch+x*4+c]);
            const int displayed=std::to_integer<int>(output.pixels[std::size_t(y)*output.row_pitch+x*4+c]);
            if(std::abs(displayed-DisplayChannel(menu,raw))>1) {std::fprintf(stderr,"video calibration differs at %u,%u channel %u\n",x,y,c);return false;}
            different |= std::abs(displayed-raw)>=4;
        }
        changed+=different;
    }
    if(changed<16) {std::fprintf(stderr,"video controls did not visibly alter the frame\n");return false;}
    for(unsigned index=0;index<3;++index) {
        const auto bounds=menu.video_caption_bounds[index];unsigned ink{};
        const int left=std::clamp(int(std::ceil(bounds.left)),0,int(source.width)),right=std::clamp(int(std::floor(bounds.right)),left,int(source.width));
        const int top=std::clamp(int(std::ceil(bounds.top)),0,int(source.height)),bottom=std::clamp(int(std::floor(bounds.bottom)),top,int(source.height));
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const auto at=std::size_t(y)*source.row_pitch+x*4;
            const int red=std::to_integer<int>(source.pixels[at]),green=std::to_integer<int>(source.pixels[at+1]),blue=std::to_integer<int>(source.pixels[at+2]);
            // Small retail control fonts may contain only antialiased stems.
            // Require bright caption ink with the supplied color's channel ratios.
            ink += red>=100 && red<=201 && green>=80 && std::abs(green-int(std::lround(red*165.0/199)))<=3 &&
                std::abs(blue-int(std::lround(green*76.0/165)))<=3;
        }
        if(ink<16) {std::fprintf(stderr,"video numeric caption %u is missing\n",index);return false;}
    }
    std::printf("GPU video calibration passed: 225 RGB samples, %u changed sample positions and three numeric captions\n",changed);return true;
}
std::array<int,3> MovieSample(const Engine::Video::DecodedVideoFrame& source,unsigned x,unsigned y,unsigned width,unsigned height) {
    const double sx=std::clamp((x+0.5)*source.width/width-0.5,0.0,double(source.width-1));
    const double sy=std::clamp((y+0.5)*source.height/height-0.5,0.0,double(source.height-1));
    const unsigned left=unsigned(sx),top=unsigned(sy),right=std::min(left+1,source.width-1),bottom=std::min(top+1,source.height-1);
    const auto channel=[&](unsigned px,unsigned py,unsigned c) {
        return std::to_integer<int>(source.pixels[std::size_t(py)*source.row_pitch+px*4+c]);
    };
    std::array<int,3> color;
    for(unsigned c=0;c<3;++c) color[c]=int(std::lround(std::lerp(std::lerp(double(channel(left,top,c)),double(channel(right,top,c)),sx-left),
        std::lerp(double(channel(left,bottom,c)),double(channel(right,bottom,c)),sx-left),sy-top)));
    return color;
}
bool MovieImageVisible(const Engine::Video::DecodedVideoFrame& source) {
    if(!source.Is_Valid() || source.format!=Engine::Video::PixelFormat::RGBA8) return false;
    for(unsigned gy=1;gy<16;++gy) for(unsigned gx=1;gx<16;++gx) {
        const auto color=MovieSample(source,gx*source.width/16,gy*source.height/16,source.width,source.height);
        if(color[0]+color[1]+color[2]>64) return true;
    }
    return false;
}
bool VerifyMovieImage(const MenuScene& menu,const Engine::Video::DecodedVideoFrame& source,const Engine::Video::DecodedVideoFrame& rendered) {
    if(!MovieImageVisible(source) || !rendered.Is_Valid()) return false;
    for(unsigned gy=1;gy<16;++gy) for(unsigned gx=1;gx<16;++gx) {
        const unsigned x=gx*rendered.width/16,y=gy*rendered.height/16;
        const auto expected=MovieSample(source,x,y,rendered.width,rendered.height);
        for(unsigned c=0;c<3;++c) if(std::abs(std::to_integer<int>(rendered.pixels[std::size_t(y)*rendered.row_pitch+x*4+c])-DisplayChannel(menu,expected[c]))>4) {
            std::fprintf(stderr,"movie capture differs at %u,%u channel %u\n",x,y,c);return false;
        }
    }
    return true;
}
bool InitRenderers(Graphics::Device& device) {
    const auto shaders=Graphics::Frame_Shader_Directory(RENEGADE_SHADER_DIRECTORY);
    return Graphics::Initialize_Scene_Renderers(device,shaders) && scene->video_renderer.Initialize(device,shaders) &&
        (Graphics::Get_Render_Services().Is_Initialized() || Graphics::Get_Render_Services().Initialize());
}
bool DrawFrame(Graphics::Device& device,Graphics::CommandList& commands,const Graphics::FrameTargets& targets) noexcept {
    try {
        auto& s=*scene;
        Graphics::RenderBeginOptions begin;begin.clear_depth=false;
        if(!Graphics::Get_Render_Services().Begin_Render(begin)) return false;
        if(!s.color_transfer.Prepare(device,targets.backbuffer.width,targets.backbuffer.height,s.color_curve)) return false;
        auto draw_targets=targets;draw_targets.backbuffer.texture=s.color_transfer.Source_Target();draw_targets.identity=0;
        if(!commands.Set_Render_Targets(draw_targets.backbuffer.texture,targets.depth.texture) ||
            !commands.Clear_Color_Target(draw_targets.backbuffer.texture,{0,0,0,1}) || !commands.Clear_Depth(1) ||
            !commands.Set_Viewport({0,0,targets.backbuffer.width,targets.backbuffer.height,0,1})) return false;
        if(s.movie_visible) {
            s.video_renderer.Begin_Frame();
            if(!s.video_renderer.Submit(1,s.movie_frame,{0,0,targets.backbuffer.width,targets.backbuffer.height}) ||
                !s.video_renderer.Render(commands,draw_targets)) return false;
            s.presented_movie_frames[s.movie_name].insert(s.movie_frame.frame_index);
        }
        if(s.level_view && s.level_view->Ready() && !s.movie_visible && !s.pause->active.Get()) {
            s.targets.clear();s.interface_kinds.clear();
            if(s.check_cinematic_effects || s.check_tutorial_gates || s.check_player_body || s.check_objective_hud || s.check_speech_pose) {
                const auto tick=s.level_view->Simulation().Tick();const auto hash=s.level_view->Simulation().World().StateHash();
                std::array<std::vector<std::byte>,2> comparison;
                for(unsigned pass=0;pass<(s.check_tutorial_gates || s.check_player_body || s.check_objective_hud || s.check_speech_pose ? 1u : 2u);++pass) {
                    renegade::presentation::LevelDrawLayers layers{pass!=0,false};
                    if(s.check_tutorial_gates) {layers.projectile_models=layers.projectile_emitters=true;layers.mechanism_frame=Engine::Math::Fixed{};}
                    if(s.check_player_body) {layers.projectile_models=layers.projectile_emitters=true;layers.player_body=false;}
                    if(s.check_objective_hud) {layers.projectile_models=layers.projectile_emitters=true;}
                    if(s.check_speech_pose) {layers.projectile_models=layers.projectile_emitters=true;layers.speech_animation=false;}
                    if(!commands.Set_Render_Targets(draw_targets.backbuffer.texture,targets.depth.texture) ||
                        !commands.Clear_Color_Target(draw_targets.backbuffer.texture,{0,0,0,1}) || !commands.Clear_Depth(1) ||
                        !commands.Set_Viewport({0,0,targets.backbuffer.width,targets.backbuffer.height,0,1}) ||
                        !s.level_view->Draw(commands,targets.backbuffer.width,targets.backbuffer.height,static_cast<std::uint32_t>(s.seconds*1000),layers) ||
                        !s.level_view->DrawCinematicOverlay(Graphics::Get_Renderer2D(),targets.backbuffer.width,targets.backbuffer.height) ||
                        (s.check_objective_hud && !s.level_view->DrawHelp(Graphics::Get_Renderer2D(),s.loading_large_font,targets.backbuffer.width,targets.backbuffer.height)) ||
                        !Graphics::Get_Renderer2D().Execute(device,commands,draw_targets.backbuffer.texture,targets.depth.texture,{0,0,targets.backbuffer.width,targets.backbuffer.height,0,1}) ||
                        !s.color_transfer.Draw_Output(Graphics::Get_Screen_Filter_Renderer(),commands,targets.backbuffer.texture,targets.depth.texture)) return false;
                    Graphics::FrameCapture capture;const auto frame=capture.Read(device,targets.backbuffer.texture,targets.backbuffer.width,targets.backbuffer.height,Graphics::RHITextureFormat::RGBA8_UNorm);
                    if(!frame.Is_Valid()) return false;
                    comparison[pass].assign(frame.pixels.begin(),frame.pixels.end());
                    const auto path=s.capture.parent_path()/(s.capture.stem().string()+(s.check_speech_pose ? "-without-speech-pose.png" : s.check_objective_hud ? "-without-objective.png" : s.check_player_body ? "-without-body.png" : s.check_tutorial_gates ? "-gates-closed.png" : pass ? "-without-trails.png" : "-without-projectiles.png"));
                    if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                }
                // Restore the complete frame without advancing any simulation
                // or presentation clocks between the three GPU readbacks.
                if(!commands.Set_Render_Targets(draw_targets.backbuffer.texture,targets.depth.texture) ||
                    !commands.Clear_Color_Target(draw_targets.backbuffer.texture,{0,0,0,1}) || !commands.Clear_Depth(1) ||
                    !commands.Set_Viewport({0,0,targets.backbuffer.width,targets.backbuffer.height,0,1}) ||
                    !s.level_view->Draw(commands,targets.backbuffer.width,targets.backbuffer.height,static_cast<std::uint32_t>(s.seconds*1000)) ||
                    !s.level_view->DrawCinematicOverlay(Graphics::Get_Renderer2D(),targets.backbuffer.width,targets.backbuffer.height) ||
                    (s.check_objective_hud && (!s.level_view->DrawHelp(Graphics::Get_Renderer2D(),s.loading_large_font,targets.backbuffer.width,targets.backbuffer.height) || !s.level_view->DrawObjectives(Graphics::Get_Renderer2D(),s.loading_font,targets.backbuffer.width,targets.backbuffer.height))) ||
                    !Graphics::Get_Renderer2D().Execute(device,commands,draw_targets.backbuffer.texture,targets.depth.texture,{0,0,targets.backbuffer.width,targets.backbuffer.height,0,1}) ||
                    !s.color_transfer.Draw_Output(Graphics::Get_Screen_Filter_Renderer(),commands,targets.backbuffer.texture,targets.depth.texture)) return false;
                Graphics::FrameCapture capture;const auto frame=capture.Read(device,targets.backbuffer.texture,targets.backbuffer.width,targets.backbuffer.height,Graphics::RHITextureFormat::RGBA8_UNorm);
                if(!frame.Is_Valid() || s.level_view->Simulation().Tick()!=tick || s.level_view->Simulation().World().StateHash()!=hash) return false;
                const auto changed=[](std::span<const std::byte> first,std::span<const std::byte> second) {
                    std::size_t count{};for(std::size_t pixel=0;pixel+3<first.size();pixel+=4) {
                        bool visible=false;for(unsigned channel=0;channel<3;++channel) visible|=std::abs(std::to_integer<int>(first[pixel+channel])-std::to_integer<int>(second[pixel+channel]))>4;
                        count+=visible;
                    }return count;
                };
                if(s.check_speech_pose) {
                    const auto pixels=changed(comparison[0],frame.pixels);
                    std::printf("SDL3 cinematic visible speech pose: tick=%llu changed_pixels=%zu; unchanged authoritative state\n",static_cast<unsigned long long>(tick),pixels);
                    if(pixels<4) return false;
                } else if(s.check_objective_hud) {
                    const auto pixels=changed(comparison[0],frame.pixels);
                    std::printf("SDL3 tutorial visible objective HUD: tick=%llu changed_pixels=%zu; unchanged authoritative state\n",static_cast<unsigned long long>(tick),pixels);
                    if(pixels<64) return false;
                } else if(s.check_player_body) {
                    const auto pixels=changed(comparison[0],frame.pixels);
                    std::printf("SDL3 tutorial visible transition body: tick=%llu changed_pixels=%zu; unchanged authoritative state\n",static_cast<unsigned long long>(tick),pixels);
                    if(pixels<64) return false;
                } else if(s.check_tutorial_gates) {
                    const auto pixels=changed(comparison[0],frame.pixels);
                    std::printf("SDL3 tutorial visible gates: tick=%llu changed_pixels=%zu; unchanged authoritative state\n",static_cast<unsigned long long>(tick),pixels);
                    if(pixels<16) return false;
                } else std::printf("SDL3 cinematic visible effects: tick=%llu meshes=%zu trail_pixels=%zu; unchanged authoritative state\n",static_cast<unsigned long long>(tick),changed(comparison[0],comparison[1]),changed(comparison[1],frame.pixels));
                s.check_cinematic_effects=false;
                s.check_tutorial_gates=false;
                s.check_player_body=false;
                s.check_objective_hud=false;
                s.check_speech_pose=false;
                if(!commands.Set_Render_Targets(draw_targets.backbuffer.texture,targets.depth.texture) ||
                    !commands.Clear_Color_Target(draw_targets.backbuffer.texture,{0,0,0,1}) || !commands.Clear_Depth(1) ||
                    !commands.Set_Viewport({0,0,targets.backbuffer.width,targets.backbuffer.height,0,1})) return false;
            }
            if(!s.level_view->Draw(commands,targets.backbuffer.width,targets.backbuffer.height,
                static_cast<std::uint32_t>(s.seconds*1000))) return false;
            if(!s.level_view->DrawCinematicOverlay(Graphics::Get_Renderer2D(),targets.backbuffer.width,targets.backbuffer.height)) return false;
            if(!s.level_view->DrawHelp(Graphics::Get_Renderer2D(),s.loading_large_font,targets.backbuffer.width,targets.backbuffer.height)) return false;
            if(!s.level_view->DrawObjectives(Graphics::Get_Renderer2D(),s.loading_font,targets.backbuffer.width,targets.backbuffer.height)) return false;
            if(!s.level_view->DrawPlayerHud(Graphics::Get_Renderer2D(),s.loading_font,s.loading_font,targets.backbuffer.width,targets.backbuffer.height)) return false;
        }
        if(!s.movie_visible && (!s.level_view || s.pause->active.Get())) {
        if(!s.backdrop.Sample(s.seconds*s.backdrop.rate,true) || !s.title.Sample(s.transition.Frame(),false) ||
            !s.gizmo.Sample(s.seconds*s.gizmo.rate,true)) return false;
        Graphics::PropParameters parameters;
        parameters.view=s.camera.Get_View_Matrix().values;
        parameters.view_projection=Multiply(s.camera.Get_Backend_Projection_Matrix().values,parameters.view);
        parameters.scene_ambient={1,1,1,0};
        // wwui/menubackdrop.cpp: a white point light above the menu, without
        // distance attenuation, and a white scene ambient. The fourth direction
        // component enables the shared prop light; position.w selects point mode.
        parameters.light_direction[0]={0,0,1,1}; parameters.light_diffuse[0]={1,1,1,0};
        parameters.light_specular[0]={1,1,1,0};parameters.light_position[0]={0,0,15000,1};
        parameters.light_attenuation[0]={1,0,0,0};
        const auto camera_position=s.camera.Get_Position();
        parameters.camera_position={camera_position.x,camera_position.y,camera_position.z,1};
        const Graphics::PropTextureMappingContext mapping{static_cast<std::uint32_t>(s.seconds*1000),s.camera.Get_Backend_Projection_Matrix().values};
        const bool main_visible=s.displayed_dialog==128 || s.displayed_dialog==129;
        // The source scene traverses its head-inserted objects in this order.
        // Static bins flush high to low; ordinary transparency flushes last.
        struct DrawPart {MenuModel* model;std::size_t part;Graphics::PropParameters parameters;};
        std::vector<DrawPart> parts;std::vector<std::int32_t> levels;
        const auto collect=[&](MenuModel& model,const Graphics::PropParameters& state) {
            for(std::size_t part=0;part<model.binding.Part_Count();++part) {
                parts.push_back({&model,part,state});levels.push_back(model.binding.Part_Sort_Level(part));
            }
        };
        if(main_visible) {
            collect(s.logo,parameters);collect(s.title,parameters);
            const auto bone=s.title.pose.Bone_Index("IF_GIZMOBONE");
            Graphics::RenderTransform attachment;
            if(s.title.pose.Visible(bone) && s.title.pose.Bone_Transform(bone,attachment)) {
                auto attached=parameters;
                attached.world=Graphics::Multiply_Affine({parameters.world},attachment).matrix;
                collect(s.gizmo,attached);
            }
        }
        collect(s.backdrop,parameters);
        auto& submission=Graphics::Get_Prop_Submission();submission.Clear();
        const std::array<float,4> camera_depth{parameters.view[8],parameters.view[9],parameters.view[10],parameters.view[11]};
        for(std::size_t index=0;index<parts.size();++index) if(levels[index]==0) {
            const auto& part=parts[index];const auto* pose=part.model->pose.Bone_Count() ? &part.model->pose : nullptr;
            if(part.model->binding.Part_Requires_Transparency_Sorting(part.part)) {
                if(!part.model->binding.Submit_Part(submission,part.part,part.parameters,
                    Graphics::PropDrawPhase::Transparent,camera_depth,pose,0,&mapping)) return false;
            } else if(!part.model->binding.Draw_Part(commands,part.part,part.parameters,pose,0,{},nullptr,&mapping)) return false;
        }
        std::vector<std::size_t> ordered;
        if(!Graphics::Build_Static_Draw_Order(levels,ordered)) return false;
        for(const auto index:ordered) {
            const auto& part=parts[index];
            if(!part.model->binding.Draw_Part(commands,part.part,part.parameters,
                part.model->pose.Bone_Count() ? &part.model->pose : nullptr,0,{},nullptr,&mapping)) return false;
        }
        if(!submission.Flush_Transparent()) return false;
        s.targets.clear();
        s.interface_kinds.clear();
        auto& ui=Graphics::Get_Renderer2D();
        if(main_visible) for(std::size_t index=0;index<6;++index) {
            Graphics::RenderTransform bone;
            if(!s.title.pose.Bone_Transform(s.title.pose.Bone_Index("IF_MMTF"+std::to_string(index+1)),bone)) return false;
            Graphics::Vector3 position;
            s.camera.Project(position,{bone.matrix[3],bone.matrix[7],bone.matrix[11]});
            const auto authored=renegade::content::MainMenuEntryStyle::ProjectedBounds({position.x,position.y},
                s.dialog.controls[index].rect.width,s.dialog.controls[index].rect.height,
                targets.backbuffer.width,targets.backbuffer.height);
            if(!authored) return false;
            const auto id=s.dialog.controls[index].id;
            const auto label=s.strings.Lookup(std::string(s.dialog.controls[index].title.text.begin(),s.dialog.controls[index].title.text.end()));
            const auto hit=engine::gui::w3d::Menu_Entry_Bounds(s.font,label,
                {(*authored)[0],(*authored)[1],(*authored)[2],(*authored)[3]},s.dialog.controls[index].style,true);
            s.targets.push_back({id,hit,s.controls[index].enabled && s.transition.Done()});
            s.interface_kinds[id]=renegade::presentation::InterfaceControlKind::Entry;
            if(!DrawEntry(s.font,ui,label,hit,s.dialog.controls[index].style,EntryFor(s,id))) return false;
        }
        if(s.displayed_dialog!=128) {
            const auto& dialog=s.dialogs.at(s.displayed_dialog);
            const bool popup=renegade::presentation::IsPopupResource(s.displayed_dialog);
            if(popup) {
                engine::gui::w3d::DialogControlDefinition full;full.rect={0,0,dialog.rect.width,dialog.rect.height};
                const auto rect=engine::gui::w3d::LayoutControl(dialog,full,float(targets.backbuffer.width),float(targets.backbuffer.height),400,300);
                const auto marker=dialog.title.text.find(u"IDS_");
                const auto title=marker==std::u16string::npos ? dialog.title.text :
                    s.strings.Lookup(std::string(dialog.title.text.begin()+marker,dialog.title.text.end()));
                using Style=renegade::content::PopupStyle;
                if(!engine::gui::w3d::Draw_Popup(s.header_font,ui,title,rect,
                    {0,0,float(targets.backbuffer.width),float(targets.backbuffer.height)},ControlPalette(true),
                    {Style::title_padding[0],Style::title_padding[1]},
                    {targets.backbuffer.width/800.0f,targets.backbuffer.height/600.0f},
                    Graphics::Color2D::From_ARGB(Style::blackout),Style::darken_background)) return false;
            }
            s.targets.clear();
            for(const auto& control:dialog.controls) {
                if(s.pause->active.Get() && !renegade::presentation::PauseMenu::SoloControlVisible(control.id)) continue;
                const bool button=control.kind.ordinal==0x80 || control.kind.text==u"Button";
                const auto bounds=engine::gui::w3d::LayoutControl(dialog,control,float(targets.backbuffer.width),float(targets.backbuffer.height),400,300);
                const auto* check=ControlCheck(s,control.id);
                const bool enabled=button && !s.pending_dialog && (check ? check->enabled.Get() : MenuCommandEnabled(s,control.id));
                const auto formatted=renegade::content::FormatControlText(control.title.text,
                    button && !popup && !check ? (control.style&15)==11 ? renegade::content::FontRole::SmallMenu : renegade::content::FontRole::Menu : renegade::content::FontRole::Controls);
                const auto& descriptor=formatted.text;
                const auto prefix=descriptor.find(u"IDS_");
                const auto label=prefix!=std::u16string::npos ? s.strings.Lookup(std::string(descriptor.begin()+prefix,descriptor.end())) : descriptor;
                const auto color=popup ? ControlPalette(true).text : Graphics::Color2D::From_ARGB(button && !enabled ? 0xff807040 : s.input.hovered==control.id || s.input.focused==control.id ? 0xffffe080 : 0xffc6a84a);
                const auto& font=ControlFont(s,formatted.font);
                const bool entry=engine::gui::w3d::IsMenuEntry(control);
                const auto hit=entry ? engine::gui::w3d::Menu_Entry_Bounds(font,label,bounds,control.style) : bounds;
                if(button && !check) {s.targets.push_back({control.id,hit,enabled});s.interface_kinds[control.id]=renegade::presentation::InterfaceKind(control);}
                if(check) {
                    const auto shape=engine::gui::w3d::Layout_Check_Box(s.control_font,label,bounds,targets.backbuffer.width/800.0f);
                    if(!shape) return false;s.targets.push_back({control.id,shape->hit,enabled});
                    s.interface_kinds[control.id]=renegade::presentation::InterfaceKind(control);
                    if(!engine::gui::w3d::DrawCheckBox(*check,s.control_font,ui,label,bounds,ControlPalette(check->enabled.Get()),
                        s.input.focused==control.id,targets.backbuffer.width/800.0f)) return false;
                }
                else if(s.displayed_dialog==166 && control.id==1389) {
                    if(!engine::gui::w3d::Draw_Filled_Outline(ui,bounds,ControlPalette(false).line,ControlPalette(false).background)) return false;
                }
                else if(s.displayed_dialog==166 && control.kind.ordinal==0x82 && (control.style&15)==7) {
                    if(!ui.Add_Outline({bounds.left,bounds.top,bounds.right,bounds.bottom},1,ControlPalette(true).line)) return false;
                }
                else if(entry) {if(!DrawEntry(font,ui,label,hit,control.style,EntryFor(s,control.id))) return false;}
                else if(button && renegade::presentation::InterfaceKind(control)==renegade::presentation::InterfaceControlKind::Button) {
                    if(!DrawButton(s,ui,s.control_font,label,bounds,control.id,enabled)) return false;
                }
                else if(s.displayed_dialog==216 && control.id==1310) {
                    s.targets.push_back({control.id,bounds,s.control_profiles->name.enabled.Get()});
                    engine::gui::w3d::TextEditLayout layout;
                    if(!engine::gui::w3d::DrawTextEdit(s.control_profiles->name,s.control_font,ui,bounds,
                        ControlPalette(s.control_profiles->name.enabled.Get()),s.profile_edit_view,&layout)) return false;
                    s.profile_edit_layout=std::move(layout);
                }
                else if(s.displayed_dialog==216 && control.id==1206) {
                    using Style=renegade::content::ListStyle;
                    if(!s.profile_layout) {
                        auto layout=engine::gui::w3d::Build_List_Layout(s.header_font,s.list_font,s.profile_rows,bounds,
                            {Style::vertical_inset,Style::header_rows,Style::row_spacing*targets.backbuffer.height/600,true,
                                renegade::content::ScrollStyle::width*targets.backbuffer.width/800.f});
                        if(!layout || !s.control_profiles->list.Configure(layout->heights,layout->text.bottom-layout->text.top,false)) return false;
                        s.profile_layout=std::move(*layout);
                    }
                    s.targets.push_back({control.id,s.profile_layout->bounds,true});
                    const auto outline=s.input.focused==control.id ? Graphics::Color2D::From_ARGB(renegade::content::ListStyle::FocusColor(
                        renegade::content::ControlStyle::enabled.line,s.gallery_pulse)) : ControlPalette(true).line;
                    if(!engine::gui::w3d::Draw_List(ui,*s.profile_layout,s.control_profiles->list,s.header_font,s.list_font,
                        {},s.profile_rows,ControlPalette(true),outline)) return false;
                    s.profile_scroll_layout.reset();
                    if(s.profile_layout->scrollbar) {
                        const auto skin=ScrollSkin(targets.backbuffer.width,targets.backbuffer.height);
                        s.profile_scroll_layout=engine::gui::w3d::Layout_Scrollbar(s.control_profiles->list.scrollbar,*s.profile_layout->scrollbar,skin);
                        if(!s.profile_scroll_layout || !engine::gui::w3d::Draw_Scrollbar(ui,s.control_profiles->list.scrollbar,*s.profile_scroll_layout,skin,
                            engine::gui::images::Resolve_Texture(*Assets::Try_Get_Asset_Cache(),s.scrollbar_texture,ui),ControlPalette(true).line,
                            renegade::content::ScrollStyle::gradient_edge_alpha)) return false;
                        s.targets.push_back({list_scrollbar_target,s.profile_scroll_layout->bounds,true,true,false});
                    }
                }
                else if(s.displayed_dialog==170 && control.id==1206) {
                    using Style=renegade::content::ListStyle;
                    if(!s.gallery_layout) {
                        auto layout=engine::gui::w3d::Build_List_Layout(s.header_font,s.list_font,s.gallery_rows,bounds,
                            {Style::vertical_inset,Style::header_rows,Style::row_spacing*targets.backbuffer.height/600,true,
                                renegade::content::ScrollStyle::width*targets.backbuffer.width/800.f});
                        if(!layout || !s.gallery.list.Configure(layout->heights,layout->text.bottom-layout->text.top,false)) return false;
                        s.gallery_layout=std::move(*layout);
                    }
                    s.targets.push_back({control.id,s.gallery_layout->bounds,true});
                    auto outline=ControlPalette(true).line;
                    if(s.input.focused==control.id) {
                        outline=Graphics::Color2D::From_ARGB(renegade::content::ListStyle::FocusColor(
                            renegade::content::ControlStyle::enabled.line,s.gallery_pulse));
                    }
                    if(!engine::gui::w3d::Draw_List(ui,*s.gallery_layout,s.gallery.list,s.header_font,s.list_font,
                        s.strings.Lookup("IDS_MOVIE_COL_HEADER"),s.gallery_rows,ControlPalette(true),outline)) return false;
                    s.gallery_scroll_layout.reset();
                    if(s.gallery_layout->scrollbar) {
                        const auto skin=ScrollSkin(targets.backbuffer.width,targets.backbuffer.height);
                        s.gallery_scroll_layout=engine::gui::w3d::Layout_Scrollbar(s.gallery.list.scrollbar,*s.gallery_layout->scrollbar,skin);
                        if(!s.gallery_scroll_layout || !engine::gui::w3d::Draw_Scrollbar(ui,s.gallery.list.scrollbar,*s.gallery_scroll_layout,skin,
                            engine::gui::images::Resolve_Texture(*Assets::Try_Get_Asset_Cache(),s.scrollbar_texture,ui),ControlPalette(true).line,
                            renegade::content::ScrollStyle::gradient_edge_alpha)) return false;
                        s.targets.push_back({list_scrollbar_target,s.gallery_scroll_layout->bounds,true,true,false});
                    }
                }
                else if(s.displayed_dialog==172 && control.id==1390) {
                    using Style=renegade::content::CreditsStyle;
                    const std::array<const engine::gui::text::FontFace*,2> fonts{&s.credits_fonts[0],&s.credits_fonts[1]};
                    if(!s.credits_layout) {
                        const auto client=engine::gui::w3d::MarqueeClient(bounds,
                            {Style::padding[0]*targets.backbuffer.width/800,Style::padding[1]*targets.backbuffer.height/600});
                        if(!client) return false;
                        auto layout=engine::gui::w3d::Build_Marquee_Layout(s.credits_lines,fonts,*client);if(!layout) return false;
                        if(!s.credits_scroll.Configure(layout->height,client->bottom-client->top,fonts[0]->Height(),Style::rows_per_second,Style::blank_pages)) return false;
                        s.credits_layout=std::move(*layout);
                        std::printf("retail credits layout: %zu formatted rows, %.0f content pixels, %.0f cycle pixels\n",
                            s.credits_layout->rows.size(),s.credits_layout->height,s.credits_scroll.Extent());
                    }
                    s.targets.push_back({control.id,bounds,true});
                    if((control.style&0x00800000u) && !ui.Add_Outline({bounds.left,bounds.top,bounds.right,bounds.bottom},1,ControlPalette(true).line)) return false;
                    if(!engine::gui::w3d::Draw_Marquee(ui,*s.credits_layout,fonts,s.credits_scroll.position.Get(),ControlPalette(true).shadow)) return false;
                }
                else if(!DrawControlText(font,ui,label,bounds,control.style,formatted.title,button,color)) return false;
                if((s.displayed_dialog==169 && control.id==1223) || (s.displayed_dialog==136 && control.id==1337) || (s.displayed_dialog==153 && control.id==40002)) {
                    // The tab control owns keyboard focus; its individual tab
                    // hit regions are pointer choices, not separate controls.
                    s.targets.push_back({control.id,{},true});
                    std::vector<unsigned> pages;
                    if(s.displayed_dialog==169) pages={231,233,232};
                    else if(s.displayed_dialog==153) pages.assign(renegade::presentation::PauseMenu::pages.begin(),renegade::presentation::PauseMenu::pages.end());
                    else for(const auto& page:renegade::presentation::controls_tabs) pages.push_back(page.resource);
                    const auto selected=s.displayed_dialog==153 ? s.pause->selected_tab.Get() : s.displayed_dialog==169 ? s.tech_options.selected_tab.Get() : s.controls_tabs.position.Get();
                    const auto page_resource=MenuPage(s);
                    const float scale=targets.backbuffer.width/800.0f;
                    const float tab_height=(bounds.bottom-bounds.top)/pages.size();
                    for(unsigned tab=0;tab<pages.size();++tab) {
                        const auto id=0x10000u+tab;
                        const auto& page=s.dialogs.at(pages[tab]);
                        auto caption=page.title.text;
                        const auto marker=caption.find(u"IDS_");
                        const auto text=marker==std::u16string::npos ? caption : s.strings.Lookup(std::string(caption.begin()+marker,caption.end()));
                        engine::gui::w3d::HitRect hit{bounds.left,bounds.top+tab*tab_height,bounds.left+245*scale,bounds.top+(tab+1)*tab_height};
                        s.targets.push_back({id,hit,true,true,false});
                        const auto tint=Graphics::Color2D::From_ARGB(selected==tab ? 0xffffe080 : 0xffc6a84a);
                        if(!engine::gui::w3d::Draw_Text(s.small_font,ui,text,hit.left+60*scale,(hit.top+hit.bottom-s.small_font.Height())*0.5f,tint)) return false;
                    }
                    const auto& page=s.dialogs.at(page_resource);
                    float expert_top{};
                    if(page_resource==232) for(const auto& item:page.controls) if(item.id==1007) expert_top=item.rect.y;
                    for(const auto& item:page.controls) {
                        if(page_resource==232 && !s.performance_settings->expert.checked.Get() &&
                            renegade::presentation::PerformanceSettings::ExpertControl(item.rect.y,expert_top)) continue;
                        const auto hit=engine::gui::w3d::LayoutControlAt(item,bounds.left+245*scale,bounds.top,
                            float(targets.backbuffer.width),float(targets.backbuffer.height),400,300);
                        const auto* slider=ControlSlider(s,item.id);
                        const auto* check=ControlCheck(s,item.id);
                        const auto formatted=renegade::content::FormatControlText(item.title.text,renegade::content::FontRole::Controls);
                        const auto& caption=formatted.text;const auto marker=caption.find(u"IDS_");
                        const auto& item_font=ControlFont(s,formatted.font);
                        auto text=marker==std::u16string::npos ? caption : s.strings.Lookup(std::string(caption.begin()+marker,caption.end()));
                        bool read_only=false;
                        if(page_resource==233) {
                            std::string value;
                            if(caption==u"IDS_MENU_TEXT745") text=u"Display information reflects the current game window. Resize the window to change resolution. Color adjustments take effect immediately.";
                            if(const auto index=renegade::presentation::VideoSettings::CaptionIndex(item.id)) {
                                value=s.video_settings->Caption(*index);s.video_caption_bounds[*index]=hit;
                            }
                            else if(item.id==1375) {
                                Graphics::RHIAdapterInfo adapter;
                                value=device.Get_Adapter_Info(adapter) ? std::format("Graphics adapter {:04X}:{:04X}",adapter.vendor_id,adapter.device_id) : "System graphics adapter";
                                read_only=true;
                            } else if(item.id==1376) {value=std::format("{} x {}",targets.backbuffer.width,targets.backbuffer.height);read_only=true;}
                            else if(item.id==1377) {value="32";read_only=true;}
                            if(!value.empty()) text=std::u16string(value.begin(),value.end());
                        }
                        if(page_resource==232 && caption==u"IDS_READ_ONLY") text=u"Lighting mode and texture filtering are read-only display settings.";
                        if(page_resource==232 && (item.id==1650 || item.id==1651)) {
                            read_only=true;
                            if(item.id==1650) {
                                constexpr std::array names{"IDS_MENU_VERTEX","IDS_MENU_MP_LIGHTMAPS","IDS_MENU_MT_LIGHTMAPS"};
                                text=s.strings.Lookup(names[static_cast<unsigned>(Graphics::Get_Render_Settings().Get_Prelit_Mode())]);
                            } else text=s.strings.Lookup("IDS_TRILINEAR");
                        }
                        if(auto* capture=ControlCapture(s,item.id)) {
                            s.targets.push_back({item.id,hit,capture->enabled.Get()});
                            if(!engine::gui::w3d::DrawInputCapture(*capture,item_font,ui,hit,ControlPalette(capture->enabled.Get()))) return false;
                        } else if(slider) {
                            const auto& model=*slider;s.targets.push_back({item.id,hit,model.enabled.Get()});
                            if(!engine::gui::w3d::DrawSlider(model,ui,hit,ControlPalette(model.enabled.Get()),s.input.focused==item.id,
                                renegade::content::ControlStyle::slider_bar_height*targets.backbuffer.height/600.0f)) return false;
                        } else if(check) {
                            const auto& model=*check;
                            const auto shape=engine::gui::w3d::Layout_Check_Box(item_font,text,hit,targets.backbuffer.width/800.0f);
                            if(!shape) return false;s.targets.push_back({item.id,shape->hit,model.enabled.Get()});
                            if(!engine::gui::w3d::DrawCheckBox(model,item_font,ui,text,hit,ControlPalette(model.enabled.Get()),
                                s.input.focused==item.id,targets.backbuffer.width/800.0f)) return false;
                        } else {
                            const bool reset=page_resource==231 && item.id==1339;
                            if(reset) {s.targets.push_back({item.id,hit,true});s.interface_kinds[item.id]=renegade::presentation::InterfaceKind(item);}
                            const bool group=(item.style&15)==7 && item.kind.ordinal==0x82;
                            if(reset) {if(!DrawButton(s,ui,s.control_font,text,hit,item.id,true)) return false;}
                            else if(group) {if(!ui.Add_Outline({hit.left,hit.top,hit.right,hit.bottom},1,{0.78f,0.65f,0.3f,1})) return false;}
                            else if(!DrawControlText(item_font,ui,text,hit,item.style,formatted.title,reset,
                                read_only ? ControlPalette(false).text : Graphics::Color2D{0.78f,0.65f,0.3f,1})) return false;
                        }
                    }
                }
            }
            // DialogBase::Build_Control_List appends child controls only after
            // all own controls, preserving the retail resource order.
            const auto own_order=[&](std::uint32_t id) {
                for(std::size_t i=0;i<dialog.controls.size();++i) if(dialog.controls[i].id==id) return i;
                return dialog.controls.size();
            };
            std::ranges::stable_sort(s.targets,[&](const auto& a,const auto& b) {return own_order(a.id)<own_order(b.id);});
        }
        s.targets_dialog=s.displayed_dialog;
        }
        if(s.level_view && !s.level_view->Ready() && !s.movie_visible) {
            s.targets.clear();s.interface_kinds.clear();
            if(!s.level_view->DrawLoading(commands,Graphics::Get_Renderer2D(),s.loading_font,s.loading_large_font,
                targets.backbuffer.width,targets.backbuffer.height,static_cast<std::uint32_t>(s.seconds*1000))) return false;
        }
        if(!Graphics::Get_Renderer2D().Execute(device,commands,draw_targets.backbuffer.texture,targets.depth.texture,
            {0,0,targets.backbuffer.width,targets.backbuffer.height,0,1})) return false;
        if(!s.color_transfer.Draw_Output(Graphics::Get_Screen_Filter_Renderer(),commands,targets.backbuffer.texture,targets.depth.texture)) return false;
        const bool capture_credits=s.verify_capture && s.displayed_dialog==172 && s.credits_layout && !s.credits_captured &&
            s.credits_scroll.position.Get()>=s.credits_fonts[0].Height()*10;
        const bool check_credit_rows=s.verify_credits_cycle && s.displayed_dialog==172 && s.credits_layout && s.frames%10==0;
        const bool capture_gallery=s.verify_gallery && s.displayed_dialog==170 && !s.movie_visible &&
            s.gallery.list.selected.Get()==0 && s.gallery_layout && !s.gallery_captured;
        if(s.verify_credits_cycle && s.displayed_dialog==172 && s.credits_layout)
            for(const auto& row:engine::gui::w3d::Visible_Marquee_Rows(*s.credits_layout,s.credits_scroll.position.Get()))
                if(row.top>=s.credits_layout->client.top && row.top+s.credits_layout->rows[row.index].height<=s.credits_layout->client.bottom)
                    s.credits_seen_rows.insert(row.index);
        if(s.capture_loading_progress || s.capture_loading || s.capture_next || s.capture_step || s.capture_movie || capture_credits || check_credit_rows || s.credits_checkpoint || capture_gallery || s.check_video_frame || s.check_performance_frame) {
            Graphics::FrameCapture capture;
            const auto frame=capture.Read(device,targets.backbuffer.texture,targets.backbuffer.width,targets.backbuffer.height,Graphics::RHITextureFormat::RGBA8_UNorm);
            if(!frame.Is_Valid()) return false;
            if(s.check_performance_frame) {
                if(!VerifyPerformanceImage(s,frame)) {
                    const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-failed.png");
                    stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch));return false;
                }
                ++s.performance_frame_checks;s.check_performance_frame=false;
            }
            if(s.check_video_frame) {
                Graphics::FrameCapture raw_capture;
                const auto raw=raw_capture.Read(device,s.color_transfer.Source_Target(),targets.backbuffer.width,targets.backbuffer.height,Graphics::RHITextureFormat::RGBA8_UNorm);
                if(!VerifyVideoCalibration(s,raw,frame)) {
                    const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-failed-raw.png");
                    if(raw.Is_Valid()) stbi_write_png(path.string().c_str(),int(raw.width),int(raw.height),4,raw.pixels.data(),int(raw.row_pitch));
                    return false;
                }
                ++s.video_frame_checks;s.check_video_frame=false;
            }
            if(s.capture_loading_progress) {
                const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-loading-progress.png");
                if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                s.capture_loading_progress=false;s.loading_progress_captured=true;
            }
            if(s.capture_loading) {
                const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-loading.png");
                if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                s.capture_loading=false;s.loading_captured=true;
            }
            if(s.capture_next) {
                if((s.verify_capture || (s.verify_level_return && !s.level_view)) && !VerifyMenuLabels(s,frame)) return false;
                s.captured=s.capture.empty() || stbi_write_png(s.capture.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))!=0;
            }
            if(s.capture_step) {
                const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-step-"+std::to_string(*s.capture_step)+".png");
                if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                s.capture_step.reset();
            }
            if(capture_credits) {
                if(!VerifyCreditsImage(s,frame)) return false;
                if(!s.capture.empty()) {
                    const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-credits.png");
                    if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                }
                s.credits_captured=true;
            }
            if(capture_gallery) {
                if(!VerifyGalleryImage(s,frame)) return false;
                const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-gallery.png");
                if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                s.gallery_captured=true;
            }
            if(check_credit_rows) {
                unsigned heading_pixels{};
                for(unsigned y=0;y<std::min(frame.height,frame.height/6);++y) for(unsigned x=0;x<frame.width;++x) {
                    const auto offset=std::size_t(y)*frame.row_pitch+x*4;
                    if(std::to_integer<unsigned>(frame.pixels[offset])>=250 && std::to_integer<unsigned>(frame.pixels[offset+1])>=250 &&
                        std::abs(std::to_integer<int>(frame.pixels[offset+2])-DisplayChannel(s,36))<=4) ++heading_pixels;
                }
                if(heading_pixels<32) {std::fprintf(stderr,"credits heading disappeared at scroll %.1f (frame %u, %u pixels)\n",s.credits_scroll.position.Get(),s.frames,heading_pixels);return false;}
                if(!VerifyCreditsImage(s,frame,&s.credits_verified_rows)) return false;
            }
            if(s.credits_checkpoint) {
                const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-credits-"+*s.credits_checkpoint+".png");
                if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                s.credits_checkpoint.reset();
            }
            if(s.capture_movie) {
                if(!VerifyMovieImage(s,s.movie_frame,frame)) return false;
                if(!s.capture.empty()) {
                    const auto path=s.capture.parent_path()/(s.capture.stem().string()+"-movie-"+*s.capture_movie+".png");
                    if(!stbi_write_png(path.string().c_str(),int(frame.width),int(frame.height),4,frame.pixels.data(),int(frame.row_pitch))) return false;
                }
                std::printf("GPU movie image matched decoded %s frame %llu at %u x %u\n",s.capture_movie->c_str(),
                    static_cast<unsigned long long>(s.movie_frame.frame_index),frame.width,frame.height);
                s.captured_movies.insert(*s.capture_movie);s.capture_movie.reset();
            }
            s.capture_next=false;
        }
        ++s.frames;
        return Graphics::Get_Render_Services().End_Render();
    } catch(const std::exception& error) { std::fprintf(stderr,"render: %s\n",error.what());return false; }
}
}

int main(int argc,char** argv) {
    std::filesystem::path install,capture,user_data;
    std::string level_preview,validate_single_player_menu;
	bool validate_level_preview = false,validate_level_cancel=false,validate_player_motion=false,validate_level_audio=false,validate_frame_timing=false;
    bool skip_mission_cinematics=false,validate_mission_cinematic=false,validate_tutorial_intro=false,validate_tutorial_course=false;
    unsigned frames=0; bool skip_intro=false,realtime=false,validate_menu=false,validate_intro_skip=false,inspect_dialogs=false,validate_quit=false,validate_credits=false,validate_gallery=false,validate_video=false,validate_performance=false,validate_controls=false,validate_profiles=false,validate_multiplayer_options=false,validate_scrollbars=false;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--install" && i+1<argc) install=argv[++i];
        else if(arg=="--level-preview" && i+1<argc) level_preview=argv[++i];
        else if(arg=="--validate-single-player-menu" && i+1<argc) {validate_single_player_menu=argv[++i];validate_level_preview=true;}
		else if(arg=="--validate-level-preview") validate_level_preview=true;
        else if(arg=="--validate-level-cancel") validate_level_cancel=true;
        else if(arg=="--validate-player-motion") validate_player_motion=true;
        else if(arg=="--validate-level-audio") validate_level_audio=true;
        else if(arg=="--validate-frame-timing") validate_frame_timing=true;
        else if(arg=="--skip-mission-cinematics") skip_mission_cinematics=true;
        else if(arg=="--validate-mission-cinematic") {validate_mission_cinematic=true;validate_level_preview=true;}
        else if(arg=="--validate-tutorial-intro") {validate_tutorial_intro=true;validate_level_preview=true;}
        else if(arg=="--validate-tutorial-course") {validate_tutorial_course=true;validate_level_preview=true;}
        else if(arg=="--user-data" && i+1<argc) user_data=argv[++i];
        else if(arg=="--frames" && i+1<argc) frames=static_cast<unsigned>(std::stoul(argv[++i]));
        else if(arg=="--screenshot" && i+1<argc) capture=argv[++i];
        else if(arg=="--skip-intro") skip_intro=true;
        else if(arg=="--validate-intro-skip") validate_intro_skip=true;
        else if(arg=="--realtime") realtime=true;
        else if(arg=="--validate-menu") validate_menu=true;
        else if(arg=="--validate-quit") validate_quit=true;
        else if(arg=="--validate-credits") validate_credits=true;
        else if(arg=="--validate-gallery") validate_gallery=true;
        else if(arg=="--validate-video") validate_video=true;
        else if(arg=="--validate-performance") validate_performance=true;
        else if(arg=="--validate-controls") validate_controls=true;
        else if(arg=="--validate-profiles") validate_profiles=true;
        else if(arg=="--validate-multiplayer-options") validate_multiplayer_options=true;
        else if(arg=="--validate-scrollbars") validate_scrollbars=true;
        else if(arg=="--inspect-dialogs") inspect_dialogs=true;
        else { std::fprintf(stderr,"usage: renegade --install <retail directory> [--user-data directory] [--frames N] [--screenshot file.png] [--skip-intro] [--realtime] [--level-preview map.mix] [--validate-level-preview] [--validate-menu] [--validate-credits] [--validate-gallery] [--validate-video] [--validate-performance] [--validate-quit] [--validate-intro-skip] [--inspect-dialogs]\n");return 2; }
    }
    if(!validate_single_player_menu.empty() && (validate_single_player_menu!="tutorial" && validate_single_player_menu!="campaign" || !level_preview.empty())) {std::fprintf(stderr,"menu launch validation requires tutorial or campaign and no direct preview map\n");return 2;}
    if(validate_level_cancel && !validate_level_preview) {std::fprintf(stderr,"loading cancellation validation requires --validate-level-preview\n");return 2;}
    if(validate_player_motion && (!validate_level_preview || validate_level_cancel)) {std::fprintf(stderr,"player motion validation requires a loaded-level runtime check\n");return 2;}
    if(validate_tutorial_intro && (validate_single_player_menu!="tutorial" || validate_player_motion || validate_frame_timing || validate_mission_cinematic || validate_level_cancel)) {
        std::fprintf(stderr,"tutorial speech validation requires independent tutorial menu entry\n");return 2;
    }
    if(validate_tutorial_course && (validate_single_player_menu!="tutorial" || validate_tutorial_intro || validate_player_motion || validate_frame_timing || validate_mission_cinematic || validate_level_cancel)) {
        std::fprintf(stderr,"tutorial course validation requires independent tutorial menu entry\n");return 2;
    }
    if(validate_level_audio && !validate_player_motion) {std::fprintf(stderr,"level audio validation requires player motion validation\n");return 2;}
    if(validate_frame_timing && (!validate_level_preview || validate_player_motion || validate_level_cancel)) {std::fprintf(stderr,"frame timing requires an independent loaded-level runtime check\n");return 2;}
    if(skip_mission_cinematics && (!validate_level_preview || !(validate_player_motion || validate_frame_timing))) {std::fprintf(stderr,"cinematic bypass is restricted to movement and timing fixtures\n");return 2;}
    if(validate_mission_cinematic && (validate_player_motion || validate_frame_timing || skip_mission_cinematics)) {std::fprintf(stderr,"cinematic validation requires an independent full playback\n");return 2;}
    if(install.empty()) { std::fprintf(stderr,"--install is required\n");return 2; }
	if(validate_level_preview && (level_preview.empty() && validate_single_player_menu.empty() || !skip_intro || capture.empty() || frames < 180 ||
		validate_menu || validate_quit || validate_intro_skip || validate_credits || validate_gallery || validate_video ||
		validate_performance || validate_controls || validate_profiles || validate_multiplayer_options || validate_scrollbars)) {
		std::fprintf(stderr,"level preview validation requires an independent map, skip-intro, capture path and at least 180 frames\n");return 2;
	}
    if(validate_menu && !frames) frames=skip_intro ? 1800 : 6000;
    if(validate_quit && !frames) frames=600;
    if(validate_quit && (validate_menu || validate_intro_skip)) {std::fprintf(stderr,"quit validation requires an independent process\n");return 2;}
    if(validate_credits && (validate_menu || validate_intro_skip || validate_quit || !skip_intro || realtime || capture.empty())) {
        std::fprintf(stderr,"credits validation requires an independent skip-intro process, deterministic clock and screenshot path\n");return 2;
    }
    if(validate_credits && !frames) frames=12000;
    if(validate_gallery && (validate_menu || validate_credits || validate_intro_skip || validate_quit || !skip_intro || !realtime || capture.empty())) {
        std::fprintf(stderr,"gallery validation requires an independent skip-intro process, real-time clock and screenshot path\n");return 2;
    }
    if(validate_gallery && !frames) frames=4800;
    if(validate_video && (validate_menu || validate_credits || validate_gallery || validate_intro_skip || validate_quit || !skip_intro || !realtime || capture.empty())) {
        std::fprintf(stderr,"video validation requires an independent skip-intro process, real-time clock and screenshot path\n");return 2;
    }
    if(validate_video && !frames) frames=1800;
    if(validate_performance && (validate_menu || validate_credits || validate_gallery || validate_video || validate_intro_skip || validate_quit || !skip_intro || !realtime || capture.empty())) {
        std::fprintf(stderr,"performance validation requires an independent skip-intro process, real-time clock and screenshot path\n");return 2;
    }
    if(validate_performance && !frames) frames=2400;
    if(validate_controls && (!skip_intro || !realtime || capture.empty() || validate_menu || validate_credits || validate_gallery || validate_video || validate_performance || validate_quit || validate_intro_skip)) {
        std::fprintf(stderr,"controls validation requires an independent skip-intro real-time process and screenshot path\n");return 2;
    }
    if(validate_controls && !frames) frames=2400;
    if(validate_profiles && (!skip_intro || !realtime || capture.empty() || validate_menu || validate_credits || validate_gallery || validate_video || validate_performance || validate_controls || validate_quit || validate_intro_skip)) {
        std::fprintf(stderr,"profile validation requires an independent skip-intro real-time process and screenshot path\n");return 2;
    }
    if(validate_profiles && !frames) frames=2400;
    if(validate_multiplayer_options && (!skip_intro || !realtime || capture.empty() || validate_menu || validate_credits || validate_gallery ||
        validate_video || validate_performance || validate_controls || validate_profiles || validate_quit || validate_intro_skip)) {
        std::fprintf(stderr,"multiplayer-options validation requires an independent skip-intro real-time process and screenshot path\n");return 2;
    }
    if(validate_multiplayer_options && !frames) frames=1200;
    if(validate_scrollbars && (!skip_intro || !realtime || capture.empty() || validate_menu || validate_credits || validate_gallery ||
        validate_video || validate_performance || validate_controls || validate_profiles || validate_multiplayer_options || validate_quit || validate_intro_skip)) {
        std::fprintf(stderr,"scrollbar validation requires an independent skip-intro real-time process and screenshot path\n");return 2;
    }
    if(validate_scrollbars && !frames) frames=1200;
    if(!capture.empty() && frames==0) frames=180;
    const auto mounted=renegade::content::MountInstall(install);
    if(!mounted) { std::fprintf(stderr,"install: %s\n",mounted.error().c_str());return 1; }
    const auto& files=*mounted->files;
    const auto executable=ReadFile(install/"Game.exe");
    const auto resource=engine::filesystem::pe::Resource(executable,5,128);
    if(!resource) { std::fprintf(stderr,"main menu resource: %s\n",resource.error().c_str());return 1; }
    const auto dialog=engine::gui::w3d::ReadDialogTemplate(*resource);
    if(!dialog) { std::fprintf(stderr,"main menu template: %s\n",dialog.error().c_str());return 1; }
    std::printf("retail main menu: %d x %d, %zu controls\n",dialog->rect.width,dialog->rect.height,dialog->controls.size());
    if(inspect_dialogs) {
        for(unsigned id:{128,129,130,131,135,136,137,139,140,141,143,145,166,167,168,169,170,171,172,174,209,210,216,231,232,233}) {
            const auto bytes=engine::filesystem::pe::Resource(executable,5,id);
            if(!bytes) { std::fprintf(stderr,"dialog %u: %s\n",id,bytes.error().c_str());return 1; }
            const auto definition=engine::gui::w3d::ReadDialogTemplate(*bytes);
            if(!definition) { std::fprintf(stderr,"dialog %u: %s\n",id,definition.error().c_str());return 1; }
            std::printf("dialog %u: %zu controls\n",id,definition->controls.size());
            for(const auto& item:definition->controls) {
                const std::string kind(item.kind.text.begin(),item.kind.text.end()),title(item.title.text.begin(),item.title.text.end());
                std::printf("  %u class %s/%u style %08x rect %d,%d,%d,%d title %s\n",item.id,kind.c_str(),item.kind.ordinal.value_or(0),item.style,item.rect.x,item.rect.y,item.rect.width,item.rect.height,title.c_str());
            }
        }
        return 0;
    }
    const auto style=renegade::content::LoadMenuStyle(files);
    if(!style) { std::fprintf(stderr,"menu style: %s\n",style.error().c_str());return 1; }
    std::map<std::string,std::filesystem::path> root_files;
    for(const auto& file:std::filesystem::directory_iterator(install)) if(file.is_regular_file())
        root_files.emplace(Assets::Canonicalize_Asset_Name(file.path().filename().string()),file.path());
    std::map<std::string,std::vector<std::byte>> font_sources;
    for(const auto& name:style->font_files) {
        auto bytes=files.Read(name);
        if(!bytes) if(const auto found=root_files.find(Assets::Canonicalize_Asset_Name(name));found!=root_files.end()) bytes=ReadFile(found->second);
        if(!bytes) { std::fprintf(stderr,"style font file missing: %s\n",name.c_str());return 1; }
        const auto families=Assets::Font_Families(*bytes);
        if(!families) { std::fprintf(stderr,"font %s: %s\n",name.c_str(),families.error().c_str());return 1; }
        for(const auto& family:*families) font_sources.emplace(Assets::Canonicalize_Asset_Name(family),*bytes);
    }
    engine::platform::sdl3::SDL3PlatformAdapter platform;
    const auto scripts=platform.application().executable_directory()/"scripts";
    if(std::filesystem::is_directory(scripts)) mounted->files->Mount(std::make_unique<engine::filesystem::DirectorySource>(scripts));
    if(user_data.empty()) user_data=platform.application().preference_directory("SAGE Modernisation","Renegade");
    if(user_data.empty()) {std::fprintf(stderr,"user data directory unavailable: %s\n",platform.last_error());return 1;}
    const auto preferences_path=user_data/"Options.ini";
    const auto preferences_text=engine::filesystem::ReadTextFile(preferences_path);
    if(!preferences_text && preferences_text.error()!=std::errc::no_such_file_or_directory) {
        std::fprintf(stderr,"settings read failed: %s\n",preferences_text.error().message().c_str());return 1;
    }
    auto preferences=engine::config::Preferences::Parse(preferences_text ? *preferences_text : "");
    engine::platform::WindowConfig config; config.title="Command & Conquer Renegade";config.size={1280,960};
    auto window=platform.windows().create(config);
    const auto native=window ? window->native_handle() : std::nullopt;
    if(!native || !native->window) { std::fprintf(stderr,"window: %s\n",platform.last_error());return 1; }
    const auto initial_pixels=window->drawable_size();
    Graphics::FrameDeviceOptions device;device.window=native->window;device.width=initial_pixels.width;device.height=initial_pixels.height;device.shader_directory=RENEGADE_SHADER_DIRECTORY;
    if(!Graphics::Initialize_Frame_Device(device)) { std::fprintf(stderr,"graphics device failed\n");return 1; }
    const std::vector<std::shared_ptr<const Assets::IModelAdapter>> adapters{std::make_shared<Assets::W3DAdapter>()};
    if(!Assets::Initialize_Asset_Runtime(engine::assets::VirtualAssetSource(files,renegade::content::AssetPaths,
        [&font_sources](std::string_view identity) {
            if(!identity.starts_with("font/")) return std::vector<std::byte>{};
            const auto end=identity.find('/',5);
            const auto found=font_sources.find(std::string(identity.substr(5,end-5)));
            return found!=font_sources.end() ? found->second : std::vector<std::byte>{};
        }),adapters)) return 1;
    engine::audio::Mixer mixer(engine::audio::AudioOutput::SampleRate);
    auto audio=engine::audio::OpenPreferredOutput(mixer);
    if(!audio) { std::fprintf(stderr,"audio output failed\n");return 1; }
    std::printf("audio: %.*s\n",int(audio->Name().size()),audio->Name().data());
    engine::audio::MovieSound movie_sound(mixer,renegade::presentation::MovieAudioBus);
    std::unique_ptr<Engine::Video::MemorySource> movie_source;
    Engine::Video::FFmpegPlayer movie;movie.Set_Audio_Sink(&movie_sound);
    auto intro_preferences=renegade::presentation::ReadIntroPreferences(preferences);
    renegade::presentation::IntroSequence intro(!skip_intro && !intro_preferences.skip_all,intro_preferences.skip_allowed);
    std::printf("intro settings restored: skip_allowed=%u skip_all=%u\n",
        unsigned(intro_preferences.skip_allowed),unsigned(intro_preferences.skip_all));
    auto movie_output_start=mixer.OutputStats();
    const auto start_movie=[&] {
        movie.Close();
        if(!intro.Playing()) {
            if(intro_preferences.skip_allowed!=intro.SkipAllowed()) {
                intro_preferences.skip_allowed=intro.SkipAllowed();
                renegade::presentation::WriteIntroPreferences(preferences,intro_preferences);
                if(const auto saved=engine::filesystem::WriteTextFile(preferences_path,preferences.Write());!saved) {
                    std::fprintf(stderr,"intro settings write failed: %s\n",saved.error().message().c_str());return false;
                }
                std::printf("intro skip permission persisted\n");
            }
            return true;
        }
        const auto path=install/"Data"/"Movies"/intro.Movie();
        auto bytes=files.Read("movies/"+std::string(intro.Movie()));
        if(!bytes) {std::fprintf(stderr,"movie bytes missing: %s\n",path.string().c_str());return false;}
        movie_source=std::make_unique<Engine::Video::MemorySource>(std::move(*bytes));
        if(!movie.Open(*movie_source)) { std::fprintf(stderr,"movie: %s could not open\n",path.string().c_str());return false; }
        movie.Set_Mode(Engine::Video::PlaybackMode::Once);movie.Play();
        movie_output_start=mixer.OutputStats();
        std::printf("movie %s: %u x %u, %llu frames\n",path.filename().string().c_str(),movie.Info().width,movie.Info().height,static_cast<unsigned long long>(movie.Info().frame_count));
        return true;
    };
    const auto music_bytes=files.Read(renegade::content::MenuAssets::music);
    auto music=music_bytes ? engine::audio::DecodeAll(*music_bytes,mixer.SampleRate()) : std::nullopt;
    if(!music) { std::fprintf(stderr,"menu music decode failed\n");return 1; }
    std::printf("menu music: %zu PCM frames\n",music->Frames());
    auto music_pcm=std::make_shared<engine::audio::PcmBuffer>(std::move(*music));
    engine::audio::VoiceId music_voice{};
    const auto play_sound=[&](std::string_view name,float volume) {
        if(auto bytes=files.Read(name)) if(auto decoded=engine::audio::DecodeAll(std::move(*bytes),mixer.SampleRate())) {
            engine::audio::VoiceStart voice;voice.buffer=std::make_shared<engine::audio::PcmBuffer>(std::move(*decoded));voice.bus=engine::audio::Bus::Interface;voice.gain=volume;return mixer.Play(std::move(voice));
        }
        return engine::audio::VoiceId{};
    };
    int result=0;
    {
        MenuScene menu; scene=&menu;menu.capture=capture;menu.verify_capture=validate_menu || validate_credits || validate_gallery || validate_video || validate_performance || validate_controls || validate_profiles || validate_multiplayer_options || validate_scrollbars;
        menu.verify_gallery=validate_gallery;
        menu.video_settings=std::make_unique<renegade::presentation::VideoSettings>(renegade::presentation::ReadVideoPreferences(preferences),
            [&menu](auto levels,const auto& ramp) {
                menu.video_ramp=ramp;
                for(unsigned i=0;i<ramp.size();++i) {const float value=ramp[i]/65535.f;menu.color_curve[i]={value,value,value,1};}
                std::printf("video calibration applied: gamma=%d brightness=%d contrast=%d\n",levels[0],levels[1],levels[2]);
            });
        menu.video_settings->Activate();
        menu.multiplayer_options=std::make_unique<renegade::presentation::MultiplayerOptions>(preferences);
        std::printf("multiplayer options restored: player_names=%u\n",unsigned(menu.multiplayer_options->Applied()));
        menu.performance_settings=std::make_unique<renegade::presentation::PerformanceSettings>(renegade::presentation::ReadPerformancePreferences(preferences));
        Graphics::Get_Texture_Quality_Settings().mip_reduction=menu.performance_settings->Applied().texture_reduction;
        std::function<void()> apply_performance;
        const auto log_performance=[](const char* action,const renegade::presentation::PerformanceApplication& value) {
            std::printf("performance settings %s: shadow=%d reduction=%d particle=%d surface=%d dynamic=%d static=%d projectors=%u\n",
                action,value.shadow_mode,value.texture_reduction,value.particle_detail,value.surface_detail,value.dynamic_budget,value.static_budget,unsigned(value.static_projectors));
        };
        log_performance("restored",menu.performance_settings->Applied());
        menu.verify_credits_cycle=validate_credits;menu.style=*style;
        const auto defaults_config=renegade::content::LoadDefaultAudioSettings(files);
        const auto defaults=defaults_config.transform([](const auto& settings) {return settings.Volumes();});
        if(!defaults) { std::fprintf(stderr,"audio defaults: %s\n",defaults.error().c_str());result=1; }
        else menu.audio_settings=std::make_unique<renegade::presentation::AudioSettings>(*defaults,[&mixer](auto category,int value,bool enabled) {
            constexpr std::array buses{engine::audio::Bus::Effects,engine::audio::Bus::Music,engine::audio::Bus::Speech,engine::audio::Bus::Cinematic};
            const float gain=enabled ? value/100.0f : 0;
            mixer.SetBusGain(buses[static_cast<unsigned>(category)],gain);
            if(category==renegade::presentation::AudioCategory::Effects) {
                mixer.SetBusGain(engine::audio::Bus::Interface,gain);
                mixer.SetBusGain(engine::audio::Bus::Ambient,gain);
            }
        });
        if(menu.audio_settings) {
            const auto saved=renegade::presentation::ReadAudioPreferences(preferences,*defaults);
            renegade::presentation::ApplyAudioPreferences(*menu.audio_settings,saved);
            std::printf("audio settings restored: effects=%d music=%d dialog=%d cinematic=%d; enabled=%u,%u,%u,%u\n",
                saved.volumes[0],saved.volumes[1],saved.volumes[2],saved.volumes[3],unsigned(saved.enabled[0]),
                unsigned(saved.enabled[1]),unsigned(saved.enabled[2]),unsigned(saved.enabled[3]));
        }
        const auto apply_previews=[&](const std::vector<renegade::presentation::PreviewCommand>& commands) {
            constexpr std::array buses{engine::audio::Bus::Effects,engine::audio::Bus::Music,engine::audio::Bus::Speech,engine::audio::Bus::Cinematic};
            for(const auto& command:commands) {
                const auto index=static_cast<unsigned>(command.category);
                using renegade::presentation::PreviewAction;
                switch(command.action) {
                case PreviewAction::Start:
                    if(menu.preview_pcm[index]) {
                        engine::audio::VoiceStart voice;voice.buffer=menu.preview_pcm[index];voice.bus=buses[index];voice.loop=command.loop;
                        menu.preview_voices[index]=mixer.Play(std::move(voice));
                        ++menu.preview_starts[index];
                    }
                    break;
                case PreviewAction::Stop:
                    if(menu.preview_voices[index]) mixer.Stop(menu.preview_voices[index],0);
                    menu.preview_voices[index]=0;break;
                case PreviewAction::SetLooping:
                    if(menu.preview_voices[index]) mixer.SetLooping(menu.preview_voices[index],command.loop);
                    break;
                }
            }
        };
        if(menu.audio_settings) for(unsigned index=0;index<4;++index)
            menu.audio_settings->volumes[index].position.Subscribe([&,index](int) {
                if(menu.preview_ready && menu.displayed_dialog==169)
                    apply_previews(menu.audio_preview.Changed(static_cast<renegade::presentation::AudioCategory>(index),
                        menu.preview_milliseconds,menu.preview_pcm[index] && menu.preview_pcm[index]->Frames()!=0));
            });
        menu.preview_ready=true;
        menu.dialog=*dialog;
        const auto string_bytes=files.Read("strings.tdb");
        const auto strings=string_bytes ? renegade::content::ReadStrings(*string_bytes) : std::expected<renegade::content::StringCatalog,std::string>(std::unexpected("strings.tdb missing"));
        if(!strings) { std::fprintf(stderr,"strings: %s\n",strings.error().c_str());result=1; }
        else menu.strings=*strings;
        const auto default_bytes=files.Read("DEFAULT_INPUT.CFG");
        const auto default_controls=default_bytes ? renegade::content::ReadInputConfiguration(
            std::string(reinterpret_cast<const char*>(default_bytes->data()),default_bytes->size())) :
            std::expected<renegade::content::InputConfiguration,std::string>(std::unexpected("DEFAULT_INPUT.CFG missing"));
        if(!default_controls) {std::fprintf(stderr,"default controls: %s\n",default_controls.error().c_str());return 1;}
        menu.profile_store=std::make_unique<renegade::content::InputProfileStore>(user_data/"config",
            [&files](std::string_view name)->std::optional<std::string> {
                const auto bytes=files.Read(name);if(!bytes) return {};
                return std::string(reinterpret_cast<const char*>(bytes->data()),bytes->size());
            });
        if(const auto opened=menu.profile_store->Open(menu.strings.Lookup("IDS_MENU_DEFAULT_SETTINGS"),menu.strings.Lookup("IDS_MENU_CUSTOM"));!opened) {
            std::fprintf(stderr,"input profile catalog: %s\n",opened.error().c_str());return 1;
        }
        const auto current_controls=menu.profile_store->Current();
        if(!current_controls) {std::fprintf(stderr,"current input profile: %s\n",current_controls.error().c_str());return 1;}
        auto controls_configuration=*current_controls;
        menu.control_profiles=std::make_unique<renegade::presentation::ControlProfiles>(menu.profile_store->Catalog());
        std::printf("input profile restored: filename=%s entries=%zu\n",menu.profile_store->Catalog().current.c_str(),menu.profile_store->Catalog().profiles.size());
        menu.controls_settings=std::make_unique<renegade::presentation::ControlsSettings>(std::move(controls_configuration),
            [&menu](auto input) {return menu.strings.Lookup(renegade::content::InputTranslation(input));},
            preferences.Number("CameraLockedToTurret",0)!=0);
        std::printf("controls restored: forward=%.*s backward=%.*s sensitivity=%d invert=%u invert2d=%u damage=%u camera=%u\n",
            int(renegade::content::InputFileName(menu.controls_settings->Configuration().Get("MoveForward").primary).size()),
            renegade::content::InputFileName(menu.controls_settings->Configuration().Get("MoveForward").primary).data(),
            int(renegade::content::InputFileName(menu.controls_settings->Configuration().Get("MoveBackward").primary).size()),
            renegade::content::InputFileName(menu.controls_settings->Configuration().Get("MoveBackward").primary).data(),
            menu.controls_settings->sensitivity.position.Get(),unsigned(menu.controls_settings->invert.checked.Get()),
            unsigned(menu.controls_settings->invert_2d.checked.Get()),unsigned(menu.controls_settings->damage_indicators.checked.Get()),unsigned(menu.controls_settings->camera_locked.checked.Get()));
        menu.controls_settings->conflict.Subscribe([&](const auto& pending) {
            if(!pending) return;
            auto message=menu.strings.Lookup("IDS_CONTROL_REMAP_WARNING");
            const auto function=menu.strings.Lookup(renegade::content::InputFunctionTranslation(pending->previous));
            for(const auto& text:{menu.strings.Lookup(renegade::content::InputTranslation(pending->input)),function}) {
                const auto position=message.find(u"%s");if(position!=message.npos) message.replace(position,2,text);
            }
            for(auto& control:menu.dialogs.at(209).controls) if(control.id==1313) control.title.text=message;
            menu.navigation.RememberFocus(menu.input.focused);menu.navigation.Open(209);
        });
        for(std::size_t i=0;i<6;++i) menu.commands[i]=std::make_unique<engine::gui::w3d::CommandBinding>(menu.navigation.For(menu.dialog.controls[i].id),menu.controls[i]);
        for(unsigned id:{129,130,131,135,136,137,139,140,141,143,145,146,147,148,149,150,151,152,153,166,167,168,169,170,171,172,174,209,210,211,216,231,232,233}) {
            const auto resource=engine::filesystem::pe::Resource(executable,5,id);
            const auto definition=resource ? engine::gui::w3d::ReadDialogTemplate(*resource) : std::expected<engine::gui::w3d::DialogDefinition,std::string>(std::unexpected("resource missing"));
            if(!definition) { std::fprintf(stderr,"dialog %u: %s\n",id,definition.error().c_str());result=1;break; }
            menu.dialogs.emplace(id,*definition);
        }
        const auto save_settings=[&] {
            if(menu.audio_settings) renegade::presentation::WriteAudioPreferences(preferences,
                renegade::presentation::CaptureAudioPreferences(*menu.audio_settings));
            renegade::presentation::WriteVideoPreferences(preferences,menu.video_settings->Levels());
            renegade::presentation::WritePerformancePreferences(preferences,menu.performance_settings->Applied());
            renegade::presentation::WriteIntroPreferences(preferences,intro_preferences);
            if(const auto saved=engine::filesystem::WriteTextFile(preferences_path,preferences.Write());!saved) {
                std::fprintf(stderr,"settings write failed: %s\n",saved.error().message().c_str());result=1;
            }
        };
        menu.navigation.closed_menus.Subscribe([&menu,&play_sound,validate_menu,&result](auto count) {
            if(!count) return;
            const auto& sound=menu.style.sounds[2];const auto voice=play_sound(sound.filename,sound.volume/100.0f);
            if(validate_menu) {
                std::printf("menu closing feedback %llu: voice %llu\n",static_cast<unsigned long long>(count),static_cast<unsigned long long>(voice));
                if(!voice) {std::fprintf(stderr,"retail menu back feedback failed\n");result=1;}
            }
        });
        menu.navigation.dialog.Subscribe([&menu,&play_sound,&files,&mixer,&apply_previews,&save_settings,&preferences,&apply_performance,validate_menu,&result](const auto id) {
            if(id==209 && menu.pause && menu.pause->ExitPending()) {
                auto& popup=menu.dialogs.at(209);popup.title.text=menu.strings.Lookup("IDS_MENU_TEXT054");
                for(auto& control:popup.controls) if(control.id==1313) control.title.text=menu.strings.Lookup("IDS_EXIT_GAME_VERIFICATION");
            }
            if(id!=menu.displayed_dialog) {
                if(auto* list=ActiveList(menu)) list->scrollbar.CaptureLost();
                menu.profile_scroll_layout.reset();menu.gallery_scroll_layout.reset();
            }
            if(id==216 && menu.displayed_dialog==136) {
                menu.control_profiles->Open();RefreshProfileRows(menu);
                menu.gallery_pulse=menu.gallery_pulse_direction=1;
                std::printf("input profiles opened: %zu entries, empty slot %d\n",menu.profile_store->Catalog().profiles.size(),menu.control_profiles->list.selected.Get());
            }
            if(id==170 && menu.displayed_dialog!=170) {
                if(!menu.gallery.Open(renegade::presentation::ReadGalleryMovies(preferences))) {std::fprintf(stderr,"movie gallery exceeds row capacity\n");result=1;return;}
                menu.gallery_rows.clear();menu.gallery_layout.reset();
                menu.gallery_pulse=menu.gallery_pulse_direction=1;
                for(const auto& entry:menu.gallery.Movies()) menu.gallery_rows.push_back({menu.strings.Lookup(entry.description),{1,1,1,1}});
                std::printf("movie gallery: %zu unlocked entries, initial selection %d\n",menu.gallery.Movies().size(),menu.gallery.list.selected.Get());
            }
            if(id==172 && menu.displayed_dialog!=172) {
                const auto bytes=files.Read("credits.txt");
                const auto lines=bytes ? engine::gui::w3d::ReadMarqueeLines(renegade::content::ReadCreditsText(*bytes),renegade::content::ControlStyle::enabled.text)
                    : std::expected<std::vector<engine::gui::w3d::MarqueeLine>,std::string>(std::unexpected("credits.txt missing"));
                if(!lines || lines->empty()) {std::fprintf(stderr,"retail credits text is unavailable\n");result=1;}
                else {menu.credits_lines=*lines;menu.credits_layout.reset();menu.credits_captured=false;}
            }
            if(renegade::presentation::IsPopupResource(id) && menu.displayed_dialog!=id) {
                const auto& sound=menu.style.sounds[3];const auto voice=play_sound(sound.filename,sound.volume/100.0f);
                if(voice) ++menu.popup_voice_starts;
                if(validate_menu) {
                    std::printf("popup opening feedback: voice %llu\n",static_cast<unsigned long long>(voice));
                    if(!voice) {std::fprintf(stderr,"retail popup feedback failed\n");result=1;}
                }
            }
            if(id==166 && menu.displayed_dialog!=166) menu.multiplayer_options->Open();
            if(menu.displayed_dialog==166 && id!=166) {
                menu.multiplayer_options->Apply(preferences);save_settings();
                std::printf("multiplayer options applied: player_names=%u\n",unsigned(menu.multiplayer_options->Applied()));
            }
            if(menu.displayed_dialog==169 && id!=169) {
                apply_previews(menu.audio_preview.Close());menu.preview_pcm={};
                if(apply_performance) apply_performance();
                // Original Tech options apply every child when the dialog closes.
                save_settings();std::printf("audio settings applied and saved\n");
            }
            if(menu.displayed_dialog==136 && id!=136 && id!=209 && id!=210 && id!=216) {
                menu.controls_settings->Apply();preferences.Set("CameraLockedToTurret",menu.controls_settings->camera_locked.checked.Get());
                if(const auto saved=menu.profile_store->SaveCurrent(menu.controls_settings->Configuration());!saved) {
                    std::fprintf(stderr,"save input profile: %s\n",saved.error().c_str());result=1;
                }
                std::printf("input profile saved: filename=%s entries=%zu\n",menu.profile_store->Catalog().current.c_str(),menu.profile_store->Catalog().profiles.size());
                save_settings();std::printf("controls applied and saved: sensitivity=%d\n",menu.controls_settings->sensitivity.position.Get());
            }
            if(id==136 && menu.displayed_dialog!=136 && menu.displayed_dialog!=209 && menu.displayed_dialog!=210 && menu.displayed_dialog!=216) menu.controls_tabs.Select(0);
            if(id==169 && menu.displayed_dialog!=169) menu.performance_settings->Open();
            if(id==169 && menu.displayed_dialog!=169) for(unsigned index=0;index<4;++index) {
                const auto filename=renegade::presentation::AudioPreview::filenames[index];
                if(auto bytes=files.Read(filename)) if(auto decoded=engine::audio::DecodeAll(std::move(*bytes),mixer.SampleRate()))
                    menu.preview_pcm[index]=std::make_shared<engine::audio::PcmBuffer>(std::move(*decoded));
                const auto& pcm=menu.preview_pcm[index];
                const bool valid=pcm && pcm->Frames()>0 && pcm->sampleRate==mixer.SampleRate() &&
                    std::ranges::all_of(pcm->samples,[](float sample) {return std::isfinite(sample);}) &&
                    std::ranges::any_of(pcm->samples,[](float sample) {return std::abs(sample)>0.0001f;});
                menu.preview_assets_valid &= valid;
                std::printf("audio preview %.*s: %zu PCM frames, %s\n",int(filename.size()),filename.data(),
                    pcm ? pcm->Frames() : 0,valid ? "decoded non-silent audio" : "unavailable");
            }
            menu.requested_focus=menu.navigation.Focus();menu.focus_pending=true;
            menu.input={};menu.entries.clear();
            if(menu.displayed_dialog==128 && id!=128 && id!=129) {
                menu.pending_dialog=id;menu.transition.Start(renegade::presentation::TransitionDirection::Out);
                play_sound(menu.transition.Sound(),0.8f);
            } else {
                const auto previous=menu.displayed_dialog;menu.displayed_dialog=id;
                if(id==128 && previous!=128 && previous!=129) { menu.transition.Start(renegade::presentation::TransitionDirection::In);play_sound(menu.transition.Sound(),0.8f); }
            }
            std::printf("menu dialog %u\n",id);std::fflush(stdout);
        });
        if(!Graphics::Register_Frame_Draw_Executor(InitRenderers,DrawFrame)) { std::fprintf(stderr,"scene renderers failed\n");result=1; }
        auto& assets=*Assets::Try_Get_Asset_Cache(); auto& gpu=*Graphics::Shared_Frame_Device();
        apply_performance=[&] {
            const auto applied=menu.performance_settings->Apply();auto& quality=Graphics::Get_Texture_Quality_Settings();
            if(quality.mip_reduction!=applied.texture_reduction) {
                quality.mip_reduction=applied.texture_reduction;
                if(!menu.backdrop.Rebind(gpu,assets) || !menu.logo.Rebind(gpu,assets) ||
                    !menu.title.Rebind(gpu,assets) || !menu.gizmo.Rebind(gpu,assets)) result=1;
                else {++menu.performance_texture_replacements;std::printf("retail model texture quality replaced: reduction=%d, four asset bindings, retained poses\n",quality.mip_reduction);}
            }
            log_performance("applied",applied);
        };
        menu.button_texture=assets.Request_Texture(renegade::content::ButtonStyle::texture);assets.Wait(menu.button_texture);
        const auto* button_asset=assets.Try_Get_Texture(menu.button_texture);
        if(!button_asset || !button_asset->Has_Pixels() || button_asset->Width()!=256 || button_asset->Height()!=256) {
            std::fprintf(stderr,"retail component-button atlas failed\n");result=1;
        } else std::printf("retail component-button atlas: %u x %u decoded pixels\n",button_asset->Width(),button_asset->Height());
        menu.scrollbar_texture=assets.Request_Texture(renegade::content::ScrollStyle::texture);assets.Wait(menu.scrollbar_texture);
        const auto* scrollbar_asset=assets.Try_Get_Texture(menu.scrollbar_texture);
        if(!scrollbar_asset || !scrollbar_asset->Has_Pixels() || scrollbar_asset->Width()!=256 || scrollbar_asset->Height()!=256) {
            std::fprintf(stderr,"retail scrollbar atlas failed\n");result=1;
        } else std::printf("retail scrollbar atlas: %u x %u decoded pixels\n",scrollbar_asset->Width(),scrollbar_asset->Height());
        const auto load_fonts=[&](unsigned height) {
            const auto load=[&](renegade::content::FontRole role,engine::gui::text::FontFace& face) {
                const auto spec=menu.style.Font(role,height);
                const auto handle=assets.Request_Font(spec.family,spec.points,spec.bold);assets.Wait(handle);
                return assets.Try_Get_Font(handle) && face.Build(*assets.Try_Get_Font(handle));
            };
            menu.credits_layout.reset();menu.gallery_layout.reset();menu.profile_layout.reset();menu.profile_edit_layout.reset();
            menu.gallery_scroll_layout.reset();menu.profile_scroll_layout.reset();
            menu.gallery.list.scrollbar.CaptureLost();menu.control_profiles->list.scrollbar.CaptureLost();
            return load(renegade::content::FontRole::Menu,menu.font) && load(renegade::content::FontRole::SmallMenu,menu.small_font) &&
                load(renegade::content::FontRole::Controls,menu.control_font) && load(renegade::content::FontRole::Title,menu.title_font) &&
                load(renegade::content::FontRole::LargeControls,menu.large_control_font) && load(renegade::content::FontRole::Tooltips,menu.tooltip_font) &&
                load(renegade::content::FontRole::Header,menu.header_font) && load(renegade::content::FontRole::Lists,menu.list_font) &&
                load(renegade::content::FontRole::Credits,menu.credits_fonts[0]) && load(renegade::content::FontRole::CreditsBold,menu.credits_fonts[1]) &&
                load(renegade::content::FontRole::InGame,menu.loading_font) && load(renegade::content::FontRole::InGameBig,menu.loading_large_font);
        };
        if(!load_fonts(initial_pixels.height)) { std::fprintf(stderr,"retail style fonts failed\n");result=1; }
        if(!result && (!menu.backdrop.Load(gpu,assets,renegade::content::MenuAssets::backdrop,true) ||
            !menu.logo.Load(gpu,assets,renegade::content::MenuAssets::logo,false) ||
            !menu.title.Load(gpu,assets,renegade::content::MenuAssets::transition,true) ||
            !menu.gizmo.Load(gpu,assets,renegade::content::MenuAssets::gizmo,true))) result=1;
        menu.camera.Set_Position({0,0,800});menu.camera.Set_Clip_Planes(5,12000);
        const auto update_menu_camera=[&](unsigned width,unsigned height) {
            const auto angles=renegade::content::MenuBackdropStyle::ViewAngles(width,height);
            if(!angles) return false;
            menu.camera.Set_View_Plane((*angles)[0],(*angles)[1]);return true;
        };
        if(!update_menu_camera(initial_pixels.width,initial_pixels.height)) result=1;
        Graphics::RenderTransform camera_bone;
        if(menu.backdrop.pose.Bone_Transform(menu.backdrop.pose.Bone_Index("CAMERA"),camera_bone)) {
            const Graphics::RenderTransform conversion{{0,0,-1,0,-1,0,0,0,0,1,0,0,0,0,0,1}};
            menu.camera.Set_Transform(Graphics::Multiply_Affine(camera_bone,conversion));
        }
        menu.transition.Start(renegade::presentation::TransitionDirection::In);
        if(!result && !start_movie()) result=1;
        bool menu_started=false;
        const auto clock_start=std::chrono::steady_clock::now();auto last=clock_start;bool running=!result;
        std::optional<renegade::presentation::AudioPreferences> gallery_audio;
        unsigned gallery_starts{},gallery_natural{},gallery_escapes{},gallery_restorations{};
        std::uint64_t gallery_pcm_start{};
        auto gallery_output_start=mixer.OutputStats();
        const auto restore_gallery_audio=[&] {
            if(!gallery_audio) return;
            constexpr std::array buses{engine::audio::Bus::Effects,engine::audio::Bus::Music,engine::audio::Bus::Speech,engine::audio::Bus::Cinematic};
            for(unsigned i=0;i<4;++i) mixer.SetBusGain(buses[i],gallery_audio->enabled[i] ? gallery_audio->volumes[i]/100.0f : 0);
            const float effects=gallery_audio->enabled[0] ? gallery_audio->volumes[0]/100.0f : 0;
            mixer.SetBusGain(engine::audio::Bus::Interface,effects);mixer.SetBusGain(engine::audio::Bus::Ambient,effects);
            // Allow_Music(true) restarts the original music track from its beginning.
            engine::audio::VoiceStart voice;voice.buffer=music_pcm;voice.bus=engine::audio::Bus::Music;voice.loop=true;
            music_voice=mixer.Play(std::move(voice));gallery_audio.reset();
            ++gallery_restorations;
            std::printf("movie gallery: gameplay audio restored, menu music restarted\n");
        };
        const auto stop_gallery=[&] {
            if(gallery_audio && validate_gallery) {
                const auto queued=movie_sound.Queued()-gallery_pcm_start;
                const auto delivered=mixer.OutputStats().nonSilentFrames-gallery_output_start.nonSilentFrames;
                std::printf("gallery audio: %llu queued stereo PCM frames, %llu non-silent stereo frames delivered to audio output\n",
                    static_cast<unsigned long long>(queued),static_cast<unsigned long long>(delivered));
                if(queued<1000 || delivered<1000) {std::fprintf(stderr,"gallery produced insufficient decoded/output audio\n");result=1;running=false;}
            }
            movie.Close();movie_source.reset();menu.gallery.Complete();restore_gallery_audio();
        };
        const auto control_sound=[&](renegade::presentation::InterfaceControlKind kind,renegade::presentation::InterfaceInteraction interaction,std::uint32_t id) {
            if(const auto index=renegade::presentation::InterfaceSound(kind,interaction)) {
                const auto& sound=menu.style.sounds[*index];
                const auto voice=play_sound(sound.filename,sound.volume/100.0f);
                if(voice) ++menu.interface_sound_starts[id];
                if(validate_menu) {
                    std::printf("interface sound %u control %u interaction %u: voice %llu\n",*index,id,unsigned(interaction),static_cast<unsigned long long>(voice));
                    if(!voice) {std::fprintf(stderr,"retail interface sound failed\n");result=1;running=false;}
                }
            }
        };
        const auto control_kind=[&](std::uint32_t id) {
            const auto found=menu.interface_kinds.find(id);
            return found==menu.interface_kinds.end() ? renegade::presentation::InterfaceControlKind::Other : found->second;
        };
        const auto profile_popup=[&](unsigned resource,std::u16string title,std::u16string message) {
            auto& definition=menu.dialogs.at(resource);definition.title.text=std::move(title);
            for(auto& control:definition.controls) if(control.id==1313) control.title.text=message;
            menu.navigation.RememberFocus(menu.input.focused);menu.navigation.Open(resource);
        };
        const auto profile_error=[&](const std::string& error) {
            const auto text=engine::gui::w3d::DecodeEditText(error);
            profile_popup(210,menu.strings.Lookup("IDS_MENU_CANT_SAVE_CONFIG"),text ? *text : u"Input profile file error");
            std::fprintf(stderr,"input profile operation: %s\n",error.c_str());
        };
        const auto process_profile=[&](const renegade::presentation::ProfileRequest& request) {
            using A=renegade::presentation::ProfileAction;
            switch(request.action) {
            case A::None:return;
            case A::NeedName:profile_popup(210,menu.strings.Lookup("IDS_MENU_CANT_SAVE_CONFIG"),menu.strings.Lookup("IDS_MENU_CONFIG_NEEDS_NAME"));return;
            case A::PromptOverwrite:
                profile_popup(209,menu.strings.Lookup("IDS_MENU_CONTROLS_OVERWRITE_PROMPT_TITLE"),menu.strings.Lookup("IDS_MENU_CONTROLS_OVERWRITE_PROMPT_MSG"));return;
            case A::PromptDelete: {
                auto message=menu.strings.Lookup("IDS_MENU_DELETE_SAVE_MSG");const auto marker=message.find(u"%s");
                if(marker!=message.npos) message.replace(marker,2,request.name);
                profile_popup(209,menu.strings.Lookup("IDS_MENU_DELETE_SAVE_TITLE"),std::move(message));return;
            }
            case A::Create:case A::Save: {
                const auto saved=request.action==A::Create ? menu.profile_store->Create(request.name,menu.controls_settings->Configuration()) :
                    menu.profile_store->Save(request.filename,request.name,menu.controls_settings->Configuration());
                if(!saved) {profile_error(saved.error());return;}
                std::printf("input profile saved: filename=%s entries=%zu\n",menu.profile_store->Catalog().current.c_str(),menu.profile_store->Catalog().profiles.size());
                menu.navigation.back.Execute();return;
            }
            case A::Load: {
                const auto loaded=menu.profile_store->Load(request.filename);if(!loaded) {profile_error(loaded.error());return;}
                menu.controls_settings->Defaults(*loaded);
                std::printf("input profile loaded: filename=%s sensitivity=%d\n",menu.profile_store->Catalog().current.c_str(),menu.controls_settings->sensitivity.position.Get());
                menu.navigation.back.Execute();return;
            }
            case A::Delete: {
                const auto row=menu.control_profiles->list.selected.Get();
                const auto removed=menu.profile_store->Delete(request.filename);if(!removed) {profile_error(removed.error());return;}
                if(*removed) {const auto current=menu.profile_store->Current();if(!current) {profile_error(current.error());return;}menu.controls_settings->Defaults(*current);}
                menu.control_profiles->Refresh();menu.control_profiles->list.Select(row);RefreshProfileRows(menu);
                std::printf("input profile deleted: filename=%s current=%s entries=%zu\n",request.filename.c_str(),menu.profile_store->Catalog().current.c_str(),menu.profile_store->Catalog().profiles.size());return;
            }
            }
        };
        std::unique_ptr<renegade::presentation::PlayerInput> player_input;
        const auto start_level=[&](const renegade::presentation::SinglePlayerStart& request)->std::expected<void,std::string> {
            auto view=std::make_unique<renegade::presentation::LevelView>(renegade::presentation::LevelViewServices{
                gpu,assets,files,Graphics::Get_Prop_Renderer(),Graphics::Get_Prop_Submission(),Graphics::Get_Resource_Load_Queue(),menu.strings,mixer,!skip_mission_cinematics});
            if(!view->BeginLoad(request.map,request.difficulty)) return std::unexpected(view->Error());
            menu.level_view=std::move(view);menu.input.pressed.reset();menu.dragging_slider.reset();
            menu.level_ready_frames=0;player_input.reset();mixer.Stop(music_voice);
            std::printf("single player start requested: %s, difficulty %d, %s\n",request.map.c_str(),request.difficulty,request.tutorial ? "tutorial" : "campaign");
            return {};
        };
        const auto campaign_flow=renegade::content::LoadCampaignFlow(files);
        if(!campaign_flow) {std::fprintf(stderr,"campaign flow: %s\n",campaign_flow.error().c_str());result=1;running=false;}
        renegade::presentation::SinglePlayerMenu single_player(menu.navigation,campaign_flow ? *campaign_flow : renegade::content::CampaignFlow{},start_level);
        const auto leave_level=[&] {
            platform.input().set_relative_mouse_mode(window->id(),false);platform.input().show_cursor(true);
            player_input.reset();menu.level_view.reset();mixer.Stop(music_voice);menu_started=false;single_player.ReturnToMenu();menu.input={};
            std::printf("level preview returned to menu after confirmed teardown\n");
        };
        menu.pause=std::make_unique<renegade::presentation::PauseMenu>(menu.navigation,[&](bool paused) {
            if(menu.level_view) menu.level_view->SetPaused(paused);
            if(player_input) {player_input->Configure(menu.controls_settings->Configuration());player_input->SetFocused(window->has_focus());}
            if(paused) {platform.input().set_relative_mouse_mode(window->id(),false);platform.input().show_cursor(true);}
            else {mixer.Stop(music_voice);menu.input={};menu.entries.clear();}
            menu_started=false;
            std::printf("single player %s, session retained\n",paused ? "paused" : "resumed");
        },leave_level);
        const auto activate=[&](std::uint32_t id) {
            menu.navigation.RememberFocus(menu.input.focused);
            if(menu.displayed_dialog==209 && (id==6 || id==7)) {
                if(menu.pause->ExitPending()) menu.pause->ConfirmExit(id==6);
                else if(menu.control_profiles->Pending()) {
                    const auto request=menu.control_profiles->Confirm(id==6);menu.navigation.back.Execute();process_profile(request);
                } else {menu.controls_settings->Confirm(id==6);menu.navigation.back.Execute();}
            } else if(menu.displayed_dialog==210 && id==1) menu.navigation.back.Execute();
            else if(menu.displayed_dialog==216 && id==1299) process_profile(menu.control_profiles->Save());
            else if(menu.displayed_dialog==216 && id==1298) process_profile(menu.control_profiles->Delete());
            else if(menu.displayed_dialog==216 && id==1300) process_profile(menu.control_profiles->Load());
            else if(menu.displayed_dialog==136 && id==1339) menu.controls_settings->Defaults(*default_controls);
            else if(menu.displayed_dialog==136 && id>=0x10000 && id<0x10005) {
                menu.controls_tabs.Select(id-0x10000);menu.input.focused=1337;menu.input.pressed.reset();menu.dragging_slider.reset();
            } else if(menu.displayed_dialog==153 && id>=0x10000 && id<0x10007) {
                menu.pause->SelectTab(id-0x10000);menu.input.focused=40002;menu.input.pressed.reset();
            } else if(menu.displayed_dialog==170 && id==1032) menu.gallery.play.Execute();
            else if(menu.displayed_dialog==169 && id>=0x10000 && id<0x10003) {
                menu.tech_options.Select(id-0x10000);menu.input.focused=1223;menu.input.pressed.reset();menu.dragging_slider.reset();
            } else if(menu.displayed_dialog==169 && menu.tech_options.Resource()==231 && id==1339) menu.audio_settings->defaults_command.Execute();
            else if(auto* check=ControlCheck(menu,id)) check->Toggle();
            else menu.navigation.For(id).Execute();
            if(menu.navigation.quitting.Get()) running=false;
        };
		menu.verify_level_return=validate_level_preview;
        if(!level_preview.empty()) {
            const auto started=start_level({level_preview,1,false});
            if(!started) {std::fprintf(stderr,"level preview: %s\n",started.error().c_str());result=1;running=false;}
        }
        bool text_input_active{};
        std::optional<std::uint32_t> previous_widget_focus;
        const auto focus_entries=[&] {
            if(menu.input.focused!=previous_widget_focus) {
                if(auto* list=ActiveList(menu)) list->scrollbar.CaptureLost();
                previous_widget_focus=menu.input.focused;
            }
            const bool editing=menu.displayed_dialog==216 && menu.input.focused==1310 && menu.control_profiles->name.enabled.Get();
            menu.control_profiles->name.Focus(editing,menu.preview_milliseconds);
            menu.control_profiles->name.Tick(menu.preview_milliseconds);
            if(editing && !text_input_active) {
                if(!platform.text_input().start(window->id())) {std::fprintf(stderr,"SDL3 text input failed: %s\n",platform.last_error());result=1;running=false;}
                else text_input_active=true;
            } else if(!editing && text_input_active) {platform.text_input().stop(window->id());text_input_active=false;}
            if(editing && menu.profile_edit_layout) {
                const auto logical=window->size(),pixels=window->drawable_size();const auto& client=menu.profile_edit_layout->client;
                if(logical.width>0 && logical.height>0 && pixels.width>0 && pixels.height>0) platform.text_input().set_area(window->id(),
                    {int(client.left*logical.width/pixels.width),int(client.top*logical.height/pixels.height),
                     int((client.right-client.left)*logical.width/pixels.width),int((client.bottom-client.top)*logical.height/pixels.height),0});
            }
            menu.controls_settings->Focus(MenuPage(menu),menu.displayed_dialog==136 ? menu.input.focused : std::nullopt,menu.preview_milliseconds);
            for(auto& [id,entry]:menu.entries) if(entry->state.Focus(menu.input.focused==id,menu.preview_milliseconds))
                control_sound(renegade::presentation::InterfaceControlKind::Entry,renegade::presentation::InterfaceInteraction::Focus,id);
        };
        const auto press_entry=[&](std::uint32_t id,bool pointer=false) {
            if(menu.entries.at(id)->state.Press(menu.preview_milliseconds,pointer))
                control_sound(renegade::presentation::InterfaceControlKind::Entry,renegade::presentation::InterfaceInteraction::Press,id);
        };
        const auto move_group=[&](int direction,bool center_mouse=false) {
            using namespace engine::gui::w3d;
            const auto& definition=menu.displayed_dialog==128 ? menu.dialog : menu.dialogs.at(menu.displayed_dialog);
            std::vector<const DialogControlDefinition*> controls;
            for(const auto& control:definition.controls) controls.push_back(&control);
            if(const auto page=MenuPage(menu)) for(const auto& control:menu.dialogs.at(page).controls) controls.push_back(&control);
            std::vector<GroupControl> groups;std::optional<std::size_t> current;
            for(std::size_t i=0;i<controls.size();++i) {
                const auto& control=*controls[i];
                bool enabled=(control.style&0x08000000u)==0;
                if(menu.displayed_dialog==128 && i<menu.controls.size()) enabled &= menu.controls[i].enabled;
                else if(control.kind.ordinal==0x80 || control.kind.text==u"Button") {
                    const auto kind=renegade::presentation::InterfaceKind(control);
                    if(kind==renegade::presentation::InterfaceControlKind::Entry) enabled &= MenuCommandEnabled(menu,control.id);
                }
                if(menu.displayed_dialog==169 && menu.tech_options.Resource()==231)
                    if(const auto slider=renegade::presentation::AudioSettings::SliderIndex(control.id)) enabled &= menu.audio_settings->volumes[*slider].enabled.Get();
                if(menu.displayed_dialog==216 && control.id==1310) enabled &= menu.control_profiles->name.enabled.Get();
                if(auto* check=ControlCheck(menu,control.id)) enabled &= check->enabled.Get();
                if(menu.displayed_dialog==166 && renegade::presentation::MultiplayerOptions::ServiceOnlyControl(control.id)) enabled=false;
                groups.push_back({enabled,(control.style&0x20000u)!=0});
                if(menu.input.focused==control.id) current=i;
            }
            const auto next=FindGroupControl(groups,current,direction);if(!next) return;
            const auto& control=*controls[*next];menu.input.focused=control.id;
            if(center_mouse && menu.entries.contains(control.id)) {
                const auto target=std::ranges::find_if(menu.targets,[&](const auto& item) {return item.id==control.id;});
                const auto formatted=renegade::content::FormatControlText(control.title.text,
                    menu.displayed_dialog==128 ? renegade::content::FontRole::Menu : (control.style&15)==11 ? renegade::content::FontRole::SmallMenu : renegade::content::FontRole::Menu);
                const auto marker=formatted.text.find(u"IDS_");
                const auto caption=marker==std::u16string::npos ? formatted.text :
                    menu.strings.Lookup(std::string(formatted.text.begin()+marker,formatted.text.end()));
                const auto extent=Measure_Text(ControlFont(menu,formatted.font),caption);
                const auto point=target==menu.targets.end() || !extent ? std::nullopt :
                    MenuEntryCursor(target->bounds,(*extent)[0],(control.style&0xf00)==0x100);
                const auto logical=window->size(),pixels=window->drawable_size();
                if(!point || logical.width<=0 || logical.height<=0 || pixels.width<=0 || pixels.height<=0 ||
                    !platform.input().warp_mouse(window->id(),{(*point)[0]*logical.width/pixels.width,(*point)[1]*logical.height/pixels.height})) {
                    std::fprintf(stderr,"SDL3 menu cursor centering failed\n");result=1;running=false;return;
                }
                if(validate_menu) {
                    const auto actual=platform.input().mouse_state().position;
                    const float x=(*point)[0]*logical.width/pixels.width,y=(*point)[1]*logical.height/pixels.height;
                    if(std::abs(actual.x-x)>1 || std::abs(actual.y-y)>1) {std::fprintf(stderr,"SDL3 cursor did not reach the source anchor\n");result=1;running=false;}
                    std::printf("SDL3 menu wheel cursor passed: control %u at %.1f,%.1f\n",control.id,x,y);
                }
            }
        };
        const auto move_slider=[&](std::uint32_t id,float x) {
            auto* model=ControlSlider(menu,id);
            const auto hit=std::ranges::find_if(menu.targets,[&](const auto& target) {return target.id==id;});
            if(model && hit!=menu.targets.end()) model->Pointer(x,hit->bounds);
        };
        // Runtime verification drives the shared SDL3 event queue. It never
        // invokes a view-model command directly or uses native OS APIs.
        using engine::platform::KeyCode;
        struct InputStep { std::uint32_t dialog,control; KeyCode key;unsigned delay_milliseconds{};
            bool verify_check_press{};int expected_music_value{-1};bool repeat{},verify_button_press{};
            std::uint32_t expected_focus{};int expected_tab{-1};float wheel{};bool flipped{};
            std::uint8_t clicks{1};std::optional<int> expected_list_selection;std::optional<renegade::presentation::VideoLevels> expected_video;
            std::optional<renegade::presentation::PerformanceValues> expected_performance;
            std::optional<bool> expected_expert;int expected_performance_level{-1};
            std::string_view controls_function;std::optional<renegade::content::FunctionBinding> expected_binding;
            int expected_controls_tab{-1},expected_sensitivity{-1};
            std::string_view text;std::optional<unsigned> expected_profile_count;
            std::optional<std::string_view> expected_profile_current;std::optional<bool> expected_name_enabled;
            std::optional<bool> expected_player_names;bool allow_disabled{}; };
        std::vector playback{
            InputStep{128,11003,KeyCode::unknown}, InputStep{135,11014,KeyCode::unknown},
            InputStep{169,1021,KeyCode::unknown},InputStep{169,1022,KeyCode::unknown},
            InputStep{169,0,KeyCode::end,0,false,100},InputStep{169,0,KeyCode::home,0,false,0},
            InputStep{169,0,KeyCode::right,0,false,1},InputStep{169,0,KeyCode::left,0,false,0},
            InputStep{169,0,KeyCode::down,0,false,0},InputStep{169,0,KeyCode::up,0,false,1},
            InputStep{169,0,KeyCode::up,0,false,2,true},
            InputStep{169,1026,KeyCode::unknown},InputStep{169,1028,KeyCode::unknown},
            InputStep{169,1024,KeyCode::unknown,2105,true},InputStep{169,0,KeyCode::space},
            InputStep{169,1339,KeyCode::unknown,0,false,-1,false,true},InputStep{169,1024,KeyCode::unknown},
            InputStep{169,0,KeyCode::space},InputStep{169,1022,KeyCode::up,0,false,32},
            InputStep{169,0x10000,KeyCode::right,0,false,-1,false,false,1223,1},
            InputStep{169,0,KeyCode::end,0,false,-1,false,false,1223,2},
            InputStep{169,0,KeyCode::home,0,false,-1,false,false,1223,0},InputStep{169,0,KeyCode::escape},
            InputStep{135,11017,KeyCode::unknown},InputStep{172,0,KeyCode::escape,6500},InputStep{135,11034,KeyCode::unknown},
            InputStep{128,0,KeyCode::unknown,0,false,-1,false,false,11030,-1,1},
            InputStep{128,0,KeyCode::unknown,0,false,-1,false,false,11000,-1,-1,true},
            InputStep{128,0,KeyCode::space},InputStep{130,0,KeyCode::down,0,false,-1,false,false,11006},
            InputStep{130,0,KeyCode::down,0,false,-1,false,false,11004},InputStep{130,0,KeyCode::up,0,false,-1,false,false,11006},
            InputStep{130,0,KeyCode::enter},
            InputStep{145,0,KeyCode::escape}, InputStep{130,0,KeyCode::escape},
            InputStep{128,0,KeyCode::escape}, InputStep{129,1179,KeyCode::unknown}};
        if(validate_menu) {
            std::vector<InputStep> prepare_audio;
            constexpr std::array checks{1023u,1024u,1025u,1027u};
            bool reset_volumes=false;
            for(unsigned i=0;i<checks.size();++i) {
                if(!menu.audio_settings->enabled[i].checked.Get())
                    prepare_audio.push_back({169,checks[i],KeyCode::unknown});
                reset_volumes |= menu.audio_settings->volumes[i].position.Get()!=(*defaults)[i];
            }
            if(reset_volumes) prepare_audio.push_back({169,1339,KeyCode::unknown});
            playback.insert(playback.begin()+2,prepare_audio.begin(),prepare_audio.end());
        }
        if(validate_credits) playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11017,KeyCode::unknown},
            InputStep{172,0,KeyCode::escape},InputStep{135,11034,KeyCode::unknown}};
        if(validate_gallery) {
            const auto list_key=[](KeyCode key,int expected,unsigned delay=0) {
                InputStep step{170,0,key,delay};step.expected_list_selection=expected;return step;
            };
            InputStep double_click{170,1206,KeyCode::unknown,400};double_click.clicks=2;double_click.expected_list_selection=0;
            playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11015,KeyCode::unknown},
                list_key(KeyCode::enter,-1),list_key(KeyCode::down,0),InputStep{170,1032,KeyCode::unknown},
                double_click,list_key(KeyCode::escape,0,2000),list_key(KeyCode::enter,0,400),
                list_key(KeyCode::escape,0,2000),InputStep{170,0,KeyCode::escape,400},InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_video) {
            const auto video_step=[](std::uint32_t control,KeyCode key,renegade::presentation::VideoLevels levels,bool repeat=false) {
                InputStep step{169,control,key};step.expected_video=levels;step.repeat=repeat;return step;
            };
            playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11014,KeyCode::unknown},
                InputStep{169,0x10001,KeyCode::unknown,0,false,-1,false,false,1223,1},
                video_step(1385,KeyCode::home,{60,0,130}),video_step(0,KeyCode::end,{210,0,130}),video_step(0,KeyCode::left,{209,0,130},true),
                video_step(1386,KeyCode::home,{209,-45,130}),video_step(0,KeyCode::end,{209,45,130}),video_step(0,KeyCode::left,{209,44,130},true),
                video_step(1387,KeyCode::home,{209,44,50}),video_step(0,KeyCode::end,{209,44,200}),video_step(0,KeyCode::left,{209,44,199},true),
                video_step(1385,KeyCode::unknown,{101,44,199}),video_step(1386,KeyCode::unknown,{101,-12,199}),video_step(1387,KeyCode::unknown,{101,-12,123}),
                InputStep{169,0,KeyCode::escape},InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_performance) {
            using V=renegade::presentation::PerformanceValues;
            const auto step=[](unsigned control,KeyCode key,V values,bool expert,int level,bool repeat=false) {
                InputStep item{169,control,key};item.expected_performance=values;item.expected_expert=expert;
                item.expected_performance_level=level;item.repeat=repeat;return item;
            };
            const V initial{2,2,1,2,1,1,0},low{0,0,0,0,0,0,0},medium{1,1,0,0,0,0,0},high{3,2,2,2,2,1,0},final{2,1,2,0,1,0,0};
            playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11014,KeyCode::unknown},
                step(0x10002,KeyCode::unknown,initial,false,2),step(1008,KeyCode::home,low,false,0),
                step(0,KeyCode::right,medium,false,1),step(0,KeyCode::right,{2,2,1,1,1,1,0},false,2,true),step(0,KeyCode::end,high,false,3),
                step(1007,KeyCode::unknown,high,true,3),
                step(1010,KeyCode::home,{0,2,2,2,2,1,0},true,3),step(0,KeyCode::end,high,true,3),step(0,KeyCode::left,{2,2,2,2,2,1,0},true,3,true),
                step(1012,KeyCode::home,{2,0,2,2,2,1,0},true,3),step(0,KeyCode::end,{2,2,2,2,2,1,0},true,3),step(0,KeyCode::left,{2,1,2,2,2,1,0},true,3),
                step(1013,KeyCode::home,{2,1,0,2,2,1,0},true,3),step(0,KeyCode::end,{2,1,2,2,2,1,0},true,3),
                step(1015,KeyCode::home,{2,1,2,0,2,1,0},true,3),step(0,KeyCode::end,{2,1,2,2,2,1,0},true,3),step(0,KeyCode::home,{2,1,2,0,2,1,0},true,3),
                step(1017,KeyCode::home,{2,1,2,0,0,1,0},true,3),step(0,KeyCode::end,{2,1,2,0,2,1,0},true,3),step(0,KeyCode::left,{2,1,2,0,1,1,0},true,3),
                step(1009,KeyCode::unknown,final,true,3),step(1007,KeyCode::unknown,final,false,3),step(0,KeyCode::space,final,true,3),
                InputStep{169,0x10001,KeyCode::unknown},step(0x10002,KeyCode::unknown,final,true,3),
                InputStep{169,0,KeyCode::escape},InputStep{135,11014,KeyCode::unknown},step(0x10002,KeyCode::unknown,final,false,1),
                step(1007,KeyCode::unknown,final,true,1),InputStep{169,0,KeyCode::escape},InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_controls) {
            using B=renegade::content::FunctionBinding;
            const auto bind=[](unsigned dialog,unsigned control,KeyCode key,std::string_view function,B binding,unsigned delay=0) {
                InputStep step{dialog,control,key,delay};step.controls_function=function;step.expected_binding=binding;return step;
            };
            const auto tab=[](unsigned index) {InputStep step{136,0x10000+index,KeyCode::unknown};step.expected_controls_tab=index;return step;};
            const auto sensitivity=[](KeyCode key,int value) {InputStep step{136,1222,key};step.expected_sensitivity=value;return step;};
            const auto f12=unsigned(KeyCode::f12),s=unsigned(KeyCode::s),w=unsigned(KeyCode::w),up=unsigned(KeyCode::up),down=unsigned(KeyCode::down);
            playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11011,KeyCode::unknown},
                bind(136,1220,KeyCode::f12,"MoveForward",{f12,up}),
                bind(136,1223,KeyCode::f12,"MoveBackward",{s,down}),InputStep{209,7,KeyCode::unknown},
                bind(136,1223,KeyCode::f12,"MoveBackward",{s,down}),InputStep{209,6,KeyCode::unknown},
                bind(136,1223,KeyCode::del,"MoveBackward",{0,down}),
                bind(136,1220,KeyCode::left_control,"MoveForward",{0,up}),
                InputStep{136,1339,KeyCode::unknown},
                bind(136,1220,KeyCode::unknown,"MoveForward",{w,up}),
                bind(136,1220,KeyCode::unknown,"MoveForward",{w,up},505),InputStep{209,7,KeyCode::unknown},
                tab(2),bind(136,1231,KeyCode::f12,"SelectWeapon0",{f12,0}),
                tab(4),bind(136,1227,KeyCode::f11,"MapScreen",{unsigned(KeyCode::f11),0}),
                tab(1),InputStep{136,1179,KeyCode::unknown},
                tab(3),sensitivity(KeyCode::home,0),sensitivity(KeyCode::end,100),
                InputStep{136,1180,KeyCode::unknown},InputStep{136,1181,KeyCode::unknown},InputStep{136,1182,KeyCode::unknown},
                InputStep{136,0,KeyCode::escape},InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_profiles) {
            const auto step=[](unsigned dialog,unsigned control,KeyCode key,unsigned count,std::string_view current,
                std::string_view text={},std::optional<bool> edit={}) {
                InputStep result{dialog,control,key};result.expected_profile_count=count;result.expected_profile_current=current;
                result.text=text;result.expected_name_enabled=edit;return result;
            };
            const auto bind=[](KeyCode key) {
                InputStep result{136,1220,key};result.controls_function="MoveForward";
                result.expected_binding=renegade::content::FunctionBinding{unsigned(key),unsigned(KeyCode::up)};return result;
            };
            playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11011,KeyCode::unknown},bind(KeyCode::f12),
                InputStep{136,0x10003,KeyCode::unknown},InputStep{136,1222,KeyCode::home},
                step(136,1338,KeyCode::unknown,1,"DEFAULT_INPUT.CFG",{},true),InputStep{216,1299,KeyCode::unknown},InputStep{210,1,KeyCode::unknown},
                step(216,1310,KeyCode::enter,2,"input01.cfg","alpha"),
                step(136,1338,KeyCode::unknown,2,"input01.cfg",{},true),step(216,1206,KeyCode::home,2,"input01.cfg",{},false),
                step(216,0,KeyCode::down,2,"input01.cfg",{},true),step(216,1310,KeyCode::enter,2,"input01.cfg","Renamed"),
                step(209,7,KeyCode::unknown,2,"input01.cfg"),InputStep{216,1310,KeyCode::enter},step(209,6,KeyCode::unknown,2,"input01.cfg"),
                step(136,1338,KeyCode::unknown,2,"input01.cfg"),step(216,1206,KeyCode::home,2,"input01.cfg",{},false),
                step(216,1299,KeyCode::unknown,2,"input01.cfg"),step(216,1298,KeyCode::unknown,2,"input01.cfg"),step(216,1300,KeyCode::unknown,2,"DEFAULT_INPUT.CFG"),
                InputStep{136,0x10000,KeyCode::unknown},bind(KeyCode::f11),InputStep{136,1338,KeyCode::unknown},
                step(216,1310,KeyCode::enter,3,"input02.cfg","beta"),step(136,1338,KeyCode::unknown,3,"input02.cfg"),
                step(216,1206,KeyCode::home,3,"input02.cfg",{},false),step(216,0,KeyCode::down,3,"input02.cfg",{},true),
                InputStep{216,1298,KeyCode::unknown},step(209,7,KeyCode::unknown,3,"input02.cfg"),InputStep{216,1298,KeyCode::unknown},
                step(209,6,KeyCode::unknown,2,"DEFAULT_INPUT.CFG"),step(216,1300,KeyCode::unknown,2,"input01.cfg"),
                step(136,11034,KeyCode::unknown,2,"input01.cfg"),InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_multiplayer_options) {
            const auto step=[](unsigned dialog,unsigned control,KeyCode key,bool names) {
                InputStep result{dialog,control,key};result.expected_player_names=names;
                result.allow_disabled=dialog==166 && renegade::presentation::MultiplayerOptions::ServiceOnlyControl(control);return result;
            };
            playback={InputStep{128,11003,KeyCode::unknown},step(135,11027,KeyCode::unknown,true),
                step(166,0,KeyCode::space,false),step(166,0,KeyCode::enter,false),step(166,1214,KeyCode::unknown,true),
                step(166,1215,KeyCode::unknown,true),step(166,1214,KeyCode::unknown,false),step(166,11034,KeyCode::unknown,false),
                step(135,11027,KeyCode::unknown,false),step(166,0,KeyCode::space,true),step(166,0,KeyCode::escape,true),
                step(135,11027,KeyCode::unknown,true),step(166,0,KeyCode::space,false),step(166,11034,KeyCode::unknown,false),
                InputStep{135,11034,KeyCode::unknown}};
        }
        if(validate_scrollbars) playback={InputStep{128,11003,KeyCode::unknown},InputStep{135,11011,KeyCode::unknown},
            InputStep{136,1338,KeyCode::unknown},InputStep{216,0,KeyCode::escape},InputStep{136,11034,KeyCode::unknown},
            InputStep{135,11034,KeyCode::unknown}};
        unsigned scroll_phase{},scroll_checks{},scroll_hold_renders{},scroll_parent_positions{},scroll_parent_pages{};
        bool scroll_validation_complete{};int scroll_page_expected{};
        std::size_t playback_step=0;unsigned posted_frame=0;std::uint64_t posted_milliseconds=0;bool playback_complete=false;
        struct CheckPress {unsigned index,frame;bool before,released{};};
        std::optional<CheckPress> check_press;
        struct ButtonPress {std::uint32_t id;unsigned frame,sounds;int music;engine::platform::Point2D position;bool released{};};
        std::optional<ButtonPress> button_press;
        std::optional<int> expected_music_value;
        std::optional<std::uint32_t> expected_focus;
        std::optional<unsigned> expected_tab;
        std::optional<int> expected_list_selection;
        std::optional<renegade::presentation::VideoLevels> expected_video;
        std::optional<renegade::presentation::PerformanceValues> expected_performance;
        std::optional<bool> expected_expert;int expected_performance_level=-1;unsigned performance_input_checks{};
        std::string_view expected_controls_function;std::optional<renegade::content::FunctionBinding> expected_binding;
        int expected_controls_tab=-1,expected_sensitivity=-1;unsigned controls_input_checks{};
        std::optional<unsigned> expected_profile_count;std::optional<std::string_view> expected_profile_current;
        std::optional<bool> expected_name_enabled;unsigned profile_input_checks{};
        std::optional<bool> expected_player_names;unsigned multiplayer_input_checks{};
        std::set<std::string> resize_requests,resize_completed;
        std::optional<std::string> pending_resize;
        const auto request_resize=[&](std::string label,engine::platform::Extent2D size) {
            if(!window->set_size(size)) {std::fprintf(stderr,"SDL3 validation resize failed\n");return false;}
            resize_requests.insert(label);pending_resize=std::move(label);
            // Let the ordinary event path rebuild rendering and input bounds
            // before posting the next control interaction.
            posted_frame=menu.frames;return true;
        };
        std::set<std::string> skip_requests;
        unsigned accepted_skips=0,blocked_skips=0;bool skip_validation_complete=false;
        unsigned quit_step{};
		bool level_return_posted = false;
        unsigned pause_probe{};std::uint64_t pause_tick{},pause_hash{},pause_posted{};
        unsigned cinematic_stage{};bool cinematic_capture_pending{},cinematic_complete{};std::array<float,3> cinematic_first_camera{};std::uint64_t cinematic_audio_start{};
        bool tutorial_started{},tutorial_complete{},tutorial_silence_checked{},tutorial_closed_gates{};std::uint64_t tutorial_audio_start{};
        unsigned tutorial_control_phase{};std::uint64_t tutorial_control_tick{};
        Engine::Math::FixedVector3 tutorial_control_position;Engine::Math::TurnAngle tutorial_control_facing;
        unsigned course_phase{};bool course_complete{},course_locked{},course_animation{},course_relocated{},course_help_captured{};
        std::uint64_t course_start_tick{},course_audio_start{},course_locked_tick{};Engine::Math::Fixed course_distance;
        Engine::Math::FixedVector3 course_logan_start,course_player_locked;Engine::Math::TurnAngle course_player_facing;
        Engine::Math::FixedVector3 course_ladder_exit;std::size_t course_ladder_motion_start{};
        unsigned course_exit_frames{},course_descent_frames{};bool course_exit_capture{},course_descent_capture{};
        std::uint64_t course_placement_tick{};
        std::optional<float> course_exit_body_start,course_descent_body_start;
        float course_exit_body_motion{},course_descent_body_motion{},course_camera_weight{};
        const auto script_look_alignment=[](const renegade::presentation::LevelView& view) {
            const auto origin=view.CameraPosition(),forward=view.CameraForward();const auto target=view.LastCameraLookTarget();
            const std::array<float,3> direction{Engine::Math::ToFloat(target.x)-origin[0],Engine::Math::ToFloat(target.y)-origin[1],Engine::Math::ToFloat(target.z)-origin[2]};
            const auto length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
            return length>0 ? (direction[0]*forward[0]+direction[1]*forward[1]+direction[2]*forward[2])/length : 0.f;
        };
        const auto facing_alignment=[](const ecs::World& world,ecs::Entity subject,ecs::Entity target) {
            using namespace Engine::Math;const auto* actor=world.Get<engine::gameplay::Transform>(subject);const auto* point=world.Get<engine::gameplay::Transform>(target);
            const auto direction=Normalize(point->position.XY()-actor->position.XY());return ToFloat(direction.x*Cos(actor->facing)+direction.y*Sin(actor->facing));
        };
        constexpr std::array<unsigned,9> cinematic_checkpoints_ms{3000,23500,25000,36000,40250,40750,48000,48750,55000};
        std::size_t cinematic_projectile_frames{},cinematic_particle_frames{},cinematic_peak_particles{},cinematic_trail_frames{},cinematic_peak_trail_particles{};
        unsigned motion_phase{};std::uint64_t motion_tick{};bool motion_complete{},motion_jump_released{};
        Engine::Math::FixedVector3 motion_start,motion_jump_start;Engine::Math::Fixed motion_distance,motion_peak;
        Engine::Math::TurnAngle motion_facing;
        float motion_forward_z{};std::uint64_t motion_audio_start{};
        unsigned walk_captures{};bool walk_capture_pending{},jump_clip_seen{},land_clip_seen{};
        Engine::Math::Fixed walk_frame;
        bool frame_timing_started{},frame_timing_complete{};
        std::chrono::steady_clock::time_point frame_timing_start;
        std::uint64_t frame_timing_tick{},timing_drawn{},timing_culled{};
        std::vector<double> frame_durations;
        unsigned single_player_menu_step{};
        std::uint64_t single_player_menu_posted{};
        std::set<std::string> credits_checkpoints;
        auto drawable=initial_pixels;
        auto last_loading_frame=std::chrono::steady_clock::now();
        double maximum_loading_gap_ms{};
        const auto load_deadline=last_loading_frame+std::chrono::seconds(validate_mission_cinematic || validate_tutorial_course ? 180 : 90);
        while(running && ((!frames || menu.frames<frames) || (validate_level_preview && !level_return_posted))) {
            const auto frame_start=std::chrono::steady_clock::now();
            if(validate_level_preview && !level_return_posted && std::chrono::steady_clock::now()>load_deadline) {std::fprintf(stderr,"menu entry or level loading timed out\n");result=1;break;}
            if(!validate_single_player_menu.empty() && single_player_menu_step<(validate_single_player_menu=="tutorial" ? 2u : 3u) &&
                !menu.level_view && !menu.pending_dialog && menu.transition.Done() && menu.frames>3 &&
                menu.preview_milliseconds-single_player_menu_posted>=400) {
                const unsigned resource=single_player_menu_step==0 ? 128 : single_player_menu_step==1 ? 130 : 131;
                const unsigned control=single_player_menu_step==0 ? 11000 : single_player_menu_step==1 ?
                    (validate_single_player_menu=="tutorial" ? 11005 : 11004) : 11008;
                if(menu.displayed_dialog==resource && menu.targets_dialog==resource) {
                    const auto target=std::ranges::find_if(menu.targets,[&](const auto& item){return item.id==control && item.enabled && item.visible;});
                    if(target==menu.targets.end()) {std::fprintf(stderr,"single-player menu control %u unavailable\n",control);result=1;break;}
                    const auto logical=window->size(),pixels=window->drawable_size();
                    engine::platform::PlatformEvent input;input.window=window->id();input.code=1;
                    input.position={(target->bounds.left+target->bounds.right)*.5f*logical.width/pixels.width,
                        (target->bounds.top+target->bounds.bottom)*.5f*logical.height/pixels.height};
                    input.type=engine::platform::EventType::mouse_moved;bool posted=platform.events().post(input);
                    input.type=engine::platform::EventType::mouse_button_down;posted=platform.events().post(input) && posted;
                    input.type=engine::platform::EventType::mouse_button_up;posted=platform.events().post(input) && posted;
                    if(!posted) {std::fprintf(stderr,"SDL3 menu launch input failed\n");result=1;break;}
                    std::printf("SDL3 single-player menu click: dialog=%u control=%u\n",resource,control);
                    ++single_player_menu_step;single_player_menu_posted=menu.preview_milliseconds;
                }
            }
            if(menu.level_view && !menu.level_view->Ready()) {
                const auto now=std::chrono::steady_clock::now();
                if(menu.loading_frames) maximum_loading_gap_ms=std::max(maximum_loading_gap_ms,std::chrono::duration<double,std::milli>(now-last_loading_frame).count());
                last_loading_frame=now;++menu.loading_frames;
                if(validate_level_preview && now>load_deadline) {std::fprintf(stderr,"level loading timed out\n");result=1;break;}
                if(validate_level_preview && menu.loading_frames%5==0) {
                    engine::platform::PlatformEvent event;event.window=window->id();event.type=engine::platform::EventType::key_down;event.key=KeyCode::f12;
                    if(!platform.events().post(event)) {result=1;break;}
                    event.type=engine::platform::EventType::key_up;if(!platform.events().post(event)) {result=1;break;}
                }
            }
            menu.preview_milliseconds=platform.clock().monotonic_nanoseconds()/1000000;
            for(const auto finished:mixer.TakeFinished()) for(unsigned index=0;index<4;++index)
                if(menu.preview_voices[index]==finished) {
                    menu.preview_voices[index]=0;menu.audio_preview.Finished(static_cast<renegade::presentation::AudioCategory>(index));
                }
            const auto expired_previews=menu.audio_preview.Frame(menu.preview_milliseconds);
            for(const auto& command:expired_previews) ++menu.preview_timeouts[static_cast<unsigned>(command.category)];
            apply_previews(expired_previews);
            std::optional<engine::platform::Extent2D> resized;
            if(check_press && menu.frames>check_press->frame) {
                if(menu.audio_settings->enabled[check_press->index].checked.Get()==check_press->before) {
                    std::fprintf(stderr,"checkbox did not toggle before pointer release\n");result=1;break;
                }
                engine::platform::PlatformEvent release;release.window=window->id();release.code=1;
                release.type=engine::platform::EventType::mouse_button_up;release.position={-10,-10};
                if(!platform.events().post(release)) {result=1;break;}
                check_press->released=true;
            }
            if(button_press && menu.frames>button_press->frame) {
                if(menu.input.pressed!=button_press->id || menu.audio_settings->volumes[1].position.Get()!=button_press->music ||
                    menu.interface_sound_starts[button_press->id]!=button_press->sounds+1) {
                    std::fprintf(stderr,"native button must click on down and defer its command until release\n");result=1;break;
                }
                engine::platform::PlatformEvent release;release.window=window->id();release.code=1;
                release.type=engine::platform::EventType::mouse_button_up;release.position=button_press->position;
                if(!platform.events().post(release)) {result=1;break;}button_press->released=true;
            }
            const bool menu_ready=!intro.Playing() && !menu.gallery.playing.Get() && !menu.pending_dialog &&
                (menu.displayed_dialog!=128 || menu.transition.Done());
            if(menu_ready && menu.focus_pending && menu.targets_dialog==menu.displayed_dialog) {
                engine::gui::w3d::ActivateDialog(menu.input,menu.targets,menu.requested_focus);
                if(menu.input.focused) {
                    menu.focus_pending=false;menu.navigation.RememberFocus(menu.input.focused);focus_entries();
                    if(validate_menu || validate_credits || validate_gallery || validate_video || validate_performance || validate_controls || validate_profiles || validate_multiplayer_options || validate_scrollbars) std::printf("dialog focus %u: %u (%s)\n",menu.displayed_dialog,*menu.input.focused,
                        menu.requested_focus==menu.input.focused ? "restored" : "default");
                }
            }
            if(validate_quit && menu_ready && !menu.focus_pending && quit_step<2) {
                const auto resource=quit_step ? 129u : 128u;
                const auto control=quit_step ? 1000u : 11000u;
                if(menu.displayed_dialog!=resource || menu.input.focused!=control) {
                    std::fprintf(stderr,"quit keyboard validation requires dialog %u default focus %u\n",resource,control);result=1;break;
                }
                engine::platform::PlatformEvent key;key.window=window->id();
                key.key=quit_step ? KeyCode::enter : KeyCode::escape;key.type=engine::platform::EventType::key_down;
                const bool down=platform.events().post(key);key.type=engine::platform::EventType::key_up;
                if(!down || !platform.events().post(key)) {std::fprintf(stderr,"SDL3 quit key posting failed\n");result=1;break;}
                std::printf("SDL3 quit keyboard step %u: dialog %u default focus %u\n",quit_step,resource,control);++quit_step;
            }
            if(validate_menu && !pending_resize) {
                std::optional<std::pair<std::string,engine::platform::Extent2D>> request;
                if(intro.Playing()) if(const auto* frame=movie.Current_Frame();frame && frame->presentation_time_us>=1000000) {
                    if(intro.Movie()=="EA_WW.BIK" && !resize_requests.contains("movie-EA_WW"))
                        request={{"movie-EA_WW",{1280,720}}};
                    else if(intro.Movie()=="R_Intro.BIK" && !resize_requests.contains("movie-R_Intro"))
                        request={{"movie-R_Intro",{1024,768}}};
                    else if(intro.Movie()=="R_Intro.BIK" && frame->presentation_time_us>=6000000 && !resize_requests.contains("movie-restore"))
                        request={{"movie-restore",config.size}};
                }
                if(menu_ready && menu.displayed_dialog==169 && !resize_requests.contains("menu-audio"))
                    request={{"menu-audio",{1366,768}}};
                else if(menu_ready && menu.displayed_dialog==135 && resize_requests.contains("menu-audio") && !resize_requests.contains("menu-restore"))
                    request={{"menu-restore",config.size}};
                if(request && !request_resize(request->first,request->second)) {result=1;break;}
            }
            if(validate_video && menu_ready && menu.displayed_dialog==169 && menu.tech_options.Resource()==233 && !pending_resize &&
                !resize_requests.contains("video-calibration"))
                if(!request_resize("video-calibration",{1296,720})) {result=1;break;}
            if(validate_intro_skip && intro.Playing() && menu.frames>3 && !skip_requests.contains(std::string(intro.Movie()))) {
                engine::platform::PlatformEvent input;input.window=window->id();input.key=KeyCode::escape;
                input.type=engine::platform::EventType::key_down;const bool down=platform.events().post(input);
                input.type=engine::platform::EventType::key_up;
                if(!platform.events().post(input) || !down) {result=1;break;}
                skip_requests.emplace(intro.Movie());
            }
            const bool credits_cycle_ready=!validate_credits || menu.displayed_dialog!=172 ||
                (menu.credits_scroll.Cycles()>0 && menu.credits_scroll.position.Get()>=menu.credits_fonts[0].Height()*10);
            if(validate_scrollbars && playback_step==3 && !scroll_validation_complete && !menu.pending_dialog &&
                !pending_resize && menu.frames>posted_frame+2 && menu.displayed_dialog==216 && ActiveScrollLayout(menu)) {
                auto& list=menu.control_profiles->list;auto& bar=list.scrollbar;const auto shape=*ActiveScrollLayout(menu);
                const auto logical=window->size(),pixels=window->drawable_size();
                const auto pointer=[&](engine::platform::EventType type,float x,float y) {
                    engine::platform::PlatformEvent input;input.window=window->id();input.type=type;input.code=1;
                    input.position={x*logical.width/pixels.width,y*logical.height/pixels.height};return platform.events().post(input);
                };
                const auto key=[&](KeyCode code) {
                    engine::platform::PlatformEvent input;input.window=window->id();input.type=engine::platform::EventType::key_down;input.key=code;
                    if(!platform.events().post(input)) return false;input.type=engine::platform::EventType::key_up;return platform.events().post(input);
                };
                const auto check=[&](bool valid,const char* name) {
                    if(!valid) {std::fprintf(stderr,"SDL3 scrollbar validation failed: phase=%u %s selected=%d top=%d bar=%d capture=%u focus=%u\n",
                        scroll_phase,name,list.selected.Get(),list.scroll.Get(),bar.position.Get(),unsigned(bar.Captured()),menu.input.focused.value_or(0));result=1;return false;}
                    ++scroll_checks;menu.capture_step=100+scroll_phase;
                    std::printf("SDL3 scrollbar state passed: phase=%u %s selected=%d top=%d bar=%d capture=%u focus=%u\n",
                        scroll_phase,name,list.selected.Get(),list.scroll.Get(),bar.position.Get(),unsigned(bar.Captured()),menu.input.focused.value_or(0));return true;
                };
                const float x=(shape.bounds.left+shape.bounds.right)*.5f;
                using E=engine::platform::EventType;bool posted=true;
                switch(scroll_phase) {
                case 0:
                    if(list.Count()<60 || !menu.profile_layout || !menu.profile_layout->scrollbar) {result=1;break;}
                    bar.ParentNotifications([&](int){++scroll_parent_positions;},[&](int){++scroll_parent_pages;});
                    posted=pointer(E::mouse_button_down,menu.profile_layout->text.left+2,menu.profile_layout->text.top+2) &&
                        pointer(E::mouse_button_up,-10,-10) && key(KeyCode::home);++scroll_phase;break;
                case 1:
                    if(!check(list.selected.Get()==0 && list.scroll.Get()==0 && bar.position.Get()==0 && menu.input.focused==1206,"Home and silent reverse sync")) break;
                    posted=pointer(E::mouse_button_down,x,(shape.geometry.next.top+shape.geometry.next.bottom)*.5f);++scroll_phase;break;
                case 2:
                    if(scroll_hold_renders<3) break;
                    posted=pointer(E::mouse_button_up,-10,-10);++scroll_phase;break;
                case 3: {
                    if(!check(!bar.Captured() && list.scroll.Get()==3 && list.selected.Get()==0 && scroll_parent_positions==3 && menu.input.focused==1206,"three render hold and outside release")) break;
                    const float height=menu.profile_layout->heights.front();
                    if(!std::ranges::all_of(menu.profile_layout->heights,[&](float value){return value==height;})) {result=1;break;}
                    scroll_page_expected=std::min(list.LastPageTop(),3+std::max(0,int(list.PageHeight()/height)-1));
                    posted=pointer(E::mouse_button_down,x,(shape.geometry.thumb.bottom+shape.geometry.next.top)*.5f) && pointer(E::mouse_button_up,-10,-10);
                    ++scroll_phase;break;}
                case 4:
                    if(!check(list.scroll.Get()==scroll_page_expected && bar.position.Get()==scroll_page_expected && list.selected.Get()==0 && scroll_parent_pages==1,"zero-sized page delegates variable-height page")) break;
                    posted=pointer(E::mouse_button_down,x,shape.geometry.thumb.top+4);++scroll_phase;break;
                case 5:
                    if(!check(bar.Captured() && bar.Interaction()==engine::gui::w3d::ScrollInteraction::Drag && list.selected.Get()==0,"thumb captures without taking list focus")) break;
                    posted=pointer(E::mouse_moved,-10,shape.geometry.track.bottom+300);++scroll_phase;break;
                case 6:
                    if(!check(list.scroll.Get()==list.LastPageTop() && bar.position.Get()==list.LastPageTop() && list.selected.Get()==0,"drag outside clamps at last page")) break;
                    posted=pointer(E::mouse_moved,-10,shape.geometry.track.top-300);++scroll_phase;break;
                case 7:
                    if(!check(list.scroll.Get()==0 && bar.position.Get()==0 && list.selected.Get()==0,"drag outside clamps at first page")) break;
                    posted=pointer(E::mouse_button_up,-10,-10);++scroll_phase;break;
                case 8:
                    if(!check(!bar.Captured() && !bar.Pressed() && menu.input.focused==1206,"outside release clears thumb capture")) break;
                    posted=pointer(E::mouse_button_down,x,shape.geometry.thumb.top+4) && request_resize("scrollbar-list",{960,720});++scroll_phase;break;
                case 9: {
                    if(!resize_completed.contains("scrollbar-list")) break;
                    if(!check(!bar.Captured() && !bar.Pressed() && list.selected.Get()==0 && list.scroll.Get()==bar.position.Get(),"resize clears held capture and remeasures list")) break;
                    engine::platform::PlatformEvent input;input.window=window->id();input.type=E::mouse_wheel;input.y=-1;
                    posted=platform.events().post(input);++scroll_phase;break;}
                case 10:
                    if(!check(list.scroll.Get()==1 && bar.position.Get()==1 && list.selected.Get()==0,"wheel scrolls without selection change")) break;
                    posted=key(KeyCode::end);++scroll_phase;break;
                case 11:
                    if(!check(list.selected.Get()==list.Count()-1 && list.scroll.Get()==bar.position.Get(),"End reveals selection and silently synchronizes bar")) break;
                    posted=key(KeyCode::home);++scroll_phase;break;
                case 12:
                    if(!check(list.selected.Get()==0 && list.scroll.Get()==0 && bar.position.Get()==0 && !bar.Captured(),"Home resets view after resize")) break;
                    posted=pointer(E::mouse_button_down,x,shape.geometry.thumb.top+4) && key(KeyCode::tab);++scroll_phase;break;
                case 13:
                    if(!check(!bar.Captured() && !bar.Pressed() && menu.input.focused!=1206,"Tab releases captured nonfocusable scrollbar")) break;
                    posted=pointer(E::mouse_moved,-10,shape.geometry.track.bottom+300) && pointer(E::mouse_button_up,-10,-10);++scroll_phase;break;
                case 14:
                    if(!check(list.scroll.Get()==0 && bar.position.Get()==0 && !bar.Captured(),"movement after focus loss cannot continue drag")) break;
                    posted=pointer(E::mouse_button_down,menu.profile_layout->text.left+2,menu.profile_layout->text.top+2) &&
                        pointer(E::mouse_button_up,-10,-10) && key(KeyCode::home);++scroll_phase;break;
                case 15:
                    if(!check(menu.input.focused==1206 && list.selected.Get()==0 && list.scroll.Get()==0 && bar.position.Get()==0,"list focus restores without stale capture")) break;
                    scroll_validation_complete=true;++scroll_phase;break;
                }
                if(!posted) result=1;
                if(result) break;
            }
            const bool gallery_escape_ready=validate_gallery && menu.gallery.playing.Get() && playback_step<playback.size() &&
                playback[playback_step].key==KeyCode::escape;
            if((validate_menu || validate_credits || validate_gallery || validate_video || validate_performance || validate_controls || validate_profiles || validate_multiplayer_options || validate_scrollbars) && !playback_complete && !check_press && !button_press && !pending_resize &&
                !(validate_scrollbars && playback_step==3 && !scroll_validation_complete) &&
                !expected_profile_count && !expected_profile_current && !expected_name_enabled && !expected_player_names &&
                (menu_ready || gallery_escape_ready) && menu.frames>posted_frame+2 && credits_cycle_ready &&
                (playback_step<playback.size() || menu.displayed_dialog==128)) {
                if(playback_step==playback.size()) {
                    if(menu.displayed_dialog!=128) { result=1;break; }
                    playback_complete=true;
                    // Capture the frame that proves the return flow completed.
                    frames=menu.frames+60;
                    if(validate_menu && (menu.audio_settings->volumes[1].position.Get()!=std::min((*defaults)[1]+1,100) || !menu.audio_settings->enabled[1].checked.Get())) {
                        std::fprintf(stderr,"audio settings playback did not increment the restored default and enabled state\n");result=1;break;
                    }
                    if(validate_menu && (!menu.preview_assets_valid || std::ranges::any_of(menu.preview_starts,[](unsigned count) {return count==0;}) ||
                        std::ranges::any_of(menu.preview_timeouts,[](unsigned count) {return count==0;}))) {
                        std::fprintf(stderr,"retail audio preview playback or timeouts incomplete\n");result=1;break;
                    }
                    if(validate_menu) {
                        std::printf("Retail audio previews passed: four non-silent decoded assets, mixer voices and source timeout actions\n");
                        std::printf("SDL3 menu playback passed: Options, Audio volume/mute/defaults, Video/Performance tabs, Single Player, Load Game, back stack, Quit, cancel\n");
                    }
                } else if(menu.displayed_dialog==playback[playback_step].dialog &&
                    menu.preview_milliseconds-posted_milliseconds>=playback[playback_step].delay_milliseconds) {
                    const auto step=playback[playback_step];
                    if(step.dialog==172) {
                        if(!menu.credits_layout || menu.credits_scroll.position.Get()<menu.credits_fonts[0].Height()*8) {
                            std::fprintf(stderr,"credits did not advance through the source marquee\n");result=1;break;
                        }
                        std::printf("SDL3 credits scrolling passed: %zu wrapped rows, two fonts, %.1f pixels before return\n",
                            menu.credits_layout->rows.size(),menu.credits_scroll.position.Get());
                    }
                    if(!capture.empty()) menu.capture_step=playback_step;
                    engine::platform::PlatformEvent input;input.window=window->id();
                    expected_player_names=step.expected_player_names;
                    expected_music_value=step.expected_music_value<0 ? std::nullopt : std::optional(step.expected_music_value);
                    expected_focus=step.expected_focus ? std::optional(step.expected_focus) : std::nullopt;
                    expected_tab=step.expected_tab<0 ? std::nullopt : std::optional(static_cast<unsigned>(step.expected_tab));
                    expected_controls_function=step.controls_function;expected_binding=step.expected_binding;
                    expected_controls_tab=step.expected_controls_tab;expected_sensitivity=step.expected_sensitivity;
                    expected_profile_count=step.expected_profile_count;expected_profile_current=step.expected_profile_current;expected_name_enabled=step.expected_name_enabled;
                    expected_list_selection=step.expected_list_selection;
                    expected_video=step.expected_video;
                    expected_performance=step.expected_performance;expected_expert=step.expected_expert;expected_performance_level=step.expected_performance_level;
                    input.clicks=step.clicks;
                    bool posted=true;
                    if(step.control) {
                        const auto target=std::ranges::find_if(menu.targets,[&](const auto& item) { return item.id==step.control && (item.enabled || step.allow_disabled) && item.visible; });
                        if(target==menu.targets.end()) { std::fprintf(stderr,"playback: enabled control %u missing\n",step.control);result=1;break; }
                        const auto logical=window->size(),pixels=window->drawable_size();
                        input.position={(target->bounds.left+target->bounds.right)*0.5f*logical.width/pixels.width,
                            (target->bounds.top+target->bounds.bottom)*0.5f*logical.height/pixels.height};
                        if(step.dialog==170 && step.control==1206 && menu.gallery_layout && !menu.gallery_layout->heights.empty())
                            input.position.y=(menu.gallery_layout->text.top+menu.gallery_layout->heights[0]*0.5f)*logical.height/pixels.height;
                        if(step.key!=KeyCode::unknown || step.expected_video) if(const auto* slider=ControlSlider(menu,step.control)) {
                            const auto& model=*slider;
                            const int thumb_width=(static_cast<int>(target->bounds.bottom-target->bounds.top)-2)/2;
                            const int level=step.key==KeyCode::unknown && step.expected_video ?
                                (*step.expected_video)[*renegade::presentation::VideoSettings::SliderIndex(step.control)] : model.position.Get();
                            const float percent=float(level-model.Minimum())/std::max(1,model.Maximum()-model.Minimum());
                            input.position.x=(target->bounds.left+percent*(target->bounds.right-target->bounds.left)-
                                thumb_width*0.5f+0.25f)*logical.width/pixels.width;
                        }
                        input.type=engine::platform::EventType::mouse_moved;posted=platform.events().post(input);
                        input.code=1;input.type=engine::platform::EventType::mouse_button_down;posted=platform.events().post(input) && posted;
                        if(step.verify_check_press) {
                            const auto index=*renegade::presentation::AudioSettings::CheckIndex(step.control);
                            check_press=CheckPress{index,menu.frames,menu.audio_settings->enabled[index].checked.Get()};
                        } else if(step.verify_button_press) {
                            button_press=ButtonPress{step.control,menu.frames,menu.interface_sound_starts[step.control],
                                menu.audio_settings->volumes[1].position.Get(),input.position};
                        } else {
                            input.type=engine::platform::EventType::mouse_button_up;posted=platform.events().post(input) && posted;
                        }
                        if(step.key!=KeyCode::unknown && step.text.empty()) {
                            input.key=step.key;input.repeat=step.repeat;input.type=engine::platform::EventType::key_down;posted=platform.events().post(input) && posted;
                            input.type=engine::platform::EventType::key_up;posted=platform.events().post(input) && posted;
                        }
                    } else if(step.wheel) {
                        input.type=engine::platform::EventType::mouse_wheel;input.y=step.wheel;input.flipped=step.flipped;
                        posted=platform.events().post(input);
                    } else {
                        input.key=step.key;input.repeat=step.repeat;input.type=engine::platform::EventType::key_down;posted=platform.events().post(input);
                        input.type=engine::platform::EventType::key_up;posted=platform.events().post(input) && posted;
                    }
                    if(!step.text.empty()) {
                        if(step.text.size()>=sizeof(input.text)) {std::fprintf(stderr,"profile input fixture text exceeds SDL event capacity\n");result=1;break;}
                        input.key=KeyCode::home;input.modifiers=0;input.type=engine::platform::EventType::key_down;posted=platform.events().post(input) && posted;
                        input.type=engine::platform::EventType::key_up;posted=platform.events().post(input) && posted;
                        input.key=KeyCode::end;input.modifiers=engine::platform::modifier_shift;input.type=engine::platform::EventType::key_down;posted=platform.events().post(input) && posted;
                        input.type=engine::platform::EventType::key_up;posted=platform.events().post(input) && posted;
                        input.modifiers=0;input.type=engine::platform::EventType::text_input;
                        std::memcpy(input.text,step.text.data(),step.text.size());input.text[step.text.size()]=0;posted=platform.events().post(input) && posted;
                        if(step.key!=KeyCode::unknown) {
                            input.key=step.key;input.type=engine::platform::EventType::key_down;posted=platform.events().post(input) && posted;
                            input.type=engine::platform::EventType::key_up;posted=platform.events().post(input) && posted;
                        }
                    }
                    if(!posted) { std::fprintf(stderr,"SDL3 input playback failed: %s\n",platform.last_error());result=1;break; }
                    posted_frame=menu.frames;posted_milliseconds=menu.preview_milliseconds;++playback_step;
                }
            }
            if(validate_player_motion && !motion_complete && menu.level_view && menu.level_view->Ready() && player_input) {
                using namespace Engine::Math;
                const auto& simulation=menu.level_view->Simulation();const auto player=simulation.World().Resource<renegade::SceneState>().player;
                const auto* pose=simulation.World().Get<engine::gameplay::Transform>(player);const auto* state=simulation.World().Get<renegade::HumanState>(player);
                const auto key=[&](std::string_view function,engine::platform::EventType type) {
                    const auto binding=menu.controls_settings->Configuration().Get(function);engine::platform::PlatformEvent event;event.window=window->id();event.type=type;
                    if(!binding.primary || binding.primary>=unsigned(KeyCode::count)) return false;event.key=static_cast<KeyCode>(binding.primary);return platform.events().post(event);
                };
                if(motion_phase==0 && simulation.Tick()>=61) {
                    if(!state->grounded) {std::fprintf(stderr,"retail player failed to reach ground: z.raw=%lld contacts=%u\n",static_cast<long long>(pose->position.z.Raw()),simulation.World().Get<engine::gameplay::SweepContacts>(player)->overlapping);result=1;break;}
                    motion_start=pose->position;motion_tick=simulation.Tick();motion_phase=1;
                    motion_audio_start=mixer.OutputStats().nonSilentFrames;
                    if(!key("FirstPersonToggle",engine::platform::EventType::key_down) || !key("FirstPersonToggle",engine::platform::EventType::key_up) ||
                        !key("MoveForward",engine::platform::EventType::key_down)) {result=1;break;}
                } else if(motion_phase==1 && walk_capture_pending && menu.captured) {
                    const auto* playback=simulation.World().Get<engine::gameplay::ClipPlayback>(player);
                    if(!playback || playback->clip!=2 || !menu.level_view->BodyPartsDrawn() || walk_captures && playback->frame==walk_frame) {
                        std::fprintf(stderr,"retail running animation did not advance through rendered player body\n");result=1;break;
                    }
                    walk_frame=playback->frame;++walk_captures;walk_capture_pending=false;
                    std::printf("SDL3 retail locomotion capture: run-forward clip=%u frame.raw=%lld body_parts=%zu capture=%u\n",playback->clip,static_cast<long long>(playback->frame.Raw()),menu.level_view->BodyPartsDrawn(),walk_captures);
                } else if(motion_phase==1 && walk_captures<2 && simulation.Tick()>=motion_tick+15+walk_captures*12) {
                    menu.capture=capture.parent_path()/(capture.stem().string()+"-walk-"+std::to_string(walk_captures+1)+".png");menu.captured=false;walk_capture_pending=true;
                } else if(motion_phase==1 && simulation.Tick()>=motion_tick+45) {
                    motion_distance=Length(FixedVector2{pose->position.x-motion_start.x,pose->position.y-motion_start.y});
                    std::printf("retail movement checkpoint: position.raw=%lld,%lld,%lld grounded=%u velocity.raw=%lld,%lld,%lld\n",static_cast<long long>(pose->position.x.Raw()),static_cast<long long>(pose->position.y.Raw()),static_cast<long long>(pose->position.z.Raw()),state->grounded,static_cast<long long>(state->velocity.x.Raw()),static_cast<long long>(state->velocity.y.Raw()),static_cast<long long>(state->velocity.z.Raw()));
                    if(motion_distance<Fixed::FromRatio(3,10)) {std::fprintf(stderr,"SDL3 movement did not advance retail player: distance.raw=%lld\n",static_cast<long long>(motion_distance.Raw()));result=1;break;}
                    motion_jump_start=pose->position;motion_peak={};motion_tick=simulation.Tick();motion_phase=2;
                    if(walk_captures!=2 || !key("FirstPersonToggle",engine::platform::EventType::key_down) || !key("FirstPersonToggle",engine::platform::EventType::key_up) ||
                        !key("MoveForward",engine::platform::EventType::key_up) || !key("Jump",engine::platform::EventType::key_down)) {result=1;break;}
                } else if(motion_phase==2) {
                    const auto* playback=simulation.World().Get<engine::gameplay::ClipPlayback>(player);
                    if(playback) {jump_clip_seen|=playback->clip>=10 && playback->clip<=14;land_clip_seen|=playback->clip>=15 && playback->clip<=19;}
                    motion_peak=std::max(motion_peak,pose->position.z-motion_jump_start.z);
                    if(!motion_jump_released && simulation.Tick()>motion_tick) {motion_jump_released=true;if(!key("Jump",engine::platform::EventType::key_up)) {result=1;break;}}
                    if(simulation.Tick()>=motion_tick+90) {
                        if(motion_peak<Fixed::FromRatio(1,10) || !state->grounded) {
                            const auto* contact=simulation.World().Get<engine::gameplay::SweepContacts>(player);const auto* hull=simulation.World().Get<engine::gameplay::SweptHull>(player);
                            std::fprintf(stderr,"retail jump/landing failed: peak.raw=%lld grounded=%u start_z.raw=%lld final.raw=%lld,%lld,%lld velocity.raw=%lld,%lld,%lld overlaps=%u contacts=%u proxy.raw=%lld,%lld,%lld offset.raw=%lld,%lld,%lld\n",static_cast<long long>(motion_peak.Raw()),state->grounded,static_cast<long long>(motion_jump_start.z.Raw()),static_cast<long long>(pose->position.x.Raw()),static_cast<long long>(pose->position.y.Raw()),static_cast<long long>(pose->position.z.Raw()),static_cast<long long>(state->velocity.x.Raw()),static_cast<long long>(state->velocity.y.Raw()),static_cast<long long>(state->velocity.z.Raw()),contact->overlapping,contact->count,static_cast<long long>(hull->extent.x.Raw()),static_cast<long long>(hull->extent.y.Raw()),static_cast<long long>(hull->extent.z.Raw()),static_cast<long long>(hull->offset.x.Raw()),static_cast<long long>(hull->offset.y.Raw()),static_cast<long long>(hull->offset.z.Raw()));result=1;break;
                        }
                        if(!jump_clip_seen || !land_clip_seen || !playback || playback->clip!=1) {std::fprintf(stderr,"retail jump, landing and idle clips did not follow player motion\n");result=1;break;}
                        motion_facing=pose->facing;motion_forward_z=menu.level_view->CameraForwardZ();motion_tick=simulation.Tick();motion_phase=3;
                        engine::platform::PlatformEvent look;look.window=window->id();look.type=engine::platform::EventType::mouse_moved;look.x=37;look.y=-18;
                        if(!platform.events().post(look)) {result=1;break;}
                    }
                } else if(motion_phase==3 && simulation.Tick()>=motion_tick+2) {
                    if(pose->facing.units==motion_facing.units || simulation.World().Get<engine::gameplay::SweepContacts>(player)->overlapping) {std::fprintf(stderr,"retail look or final collision state failed\n");result=1;break;}
                    if(menu.level_view->CameraForwardZ()<=motion_forward_z) {std::fprintf(stderr,"upward SDL3 mouse motion failed to turn camera upward\n");result=1;break;}
                    std::printf("SDL3 retail vertical look passed: upward mouse delta, camera forward z %.5f -> %.5f\n",motion_forward_z,menu.level_view->CameraForwardZ());
                    motion_tick=simulation.Tick();motion_phase=4;
                    if(!key("FirstPersonToggle",engine::platform::EventType::key_down) || !key("FirstPersonToggle",engine::platform::EventType::key_up)) {result=1;break;}
                } else if(motion_phase==4 && simulation.Tick()>=motion_tick+2) {
                    if(menu.level_view->FirstPerson() || simulation.World().Get<engine::gameplay::DrawHidden>(player)) {std::fprintf(stderr,"third-person body visibility did not change\n");result=1;break;}
                    menu.capture=capture.parent_path()/(capture.stem().string()+"-body.png");menu.captured=false;motion_phase=5;
                } else if(motion_phase==5 && menu.captured) {
                    if(!menu.level_view->BodyPartsDrawn()) {std::fprintf(stderr,"third-person player body did not reach the shared renderer\n");result=1;break;}
                    std::printf("SDL3 retail camera toggle passed: third-person player body, %zu visible parts, GPU capture\n",menu.level_view->BodyPartsDrawn());
                    motion_tick=simulation.Tick();motion_phase=6;
                    if(!key("FirstPersonToggle",engine::platform::EventType::key_down) || !key("FirstPersonToggle",engine::platform::EventType::key_up)) {result=1;break;}
                } else if(motion_phase==6 && simulation.Tick()>=motion_tick+2) {
                    if(!menu.level_view->FirstPerson() || !simulation.World().Get<engine::gameplay::DrawHidden>(player)) {std::fprintf(stderr,"first-person body hiding did not return\n");result=1;break;}
                    motion_complete=true;menu.capture=capture.parent_path()/(capture.stem().string()+"-motion.png");menu.captured=false;
                    std::printf("SDL3 retail locomotion passed: two run-forward GPU captures, airborne clip, landing clip, standing recovery\n");
                    if(validate_level_audio) {
                        const auto delivered=mixer.OutputStats().nonSilentFrames-motion_audio_start;
                        if(!menu.level_view->PlayingSounds() || delivered<4800) {std::fprintf(stderr,"retail scene audio failed: playing=%zu delivered=%llu\n",menu.level_view->PlayingSounds(),static_cast<unsigned long long>(delivered));result=1;break;}
                        std::printf("SDL3 retail scene audio passed: %zu owned emitters, %llu non-silent stereo frames delivered to audio output during movement\n",menu.level_view->PlayingSounds(),static_cast<unsigned long long>(delivered));
                    }
                    std::printf("SDL3 retail player motion passed: distance.raw=%lld jump_peak.raw=%lld grounded=%u relative_look=1 fixed_ticks=%llu\n",static_cast<long long>(motion_distance.Raw()),static_cast<long long>(motion_peak.Raw()),state->grounded,static_cast<unsigned long long>(simulation.Tick()));
                }
            }
            if(validate_tutorial_intro && menu.level_view && menu.level_view->Ready() && !tutorial_complete) {
                auto& view=*menu.level_view;
                if(!tutorial_started) {tutorial_started=true;tutorial_audio_start=mixer.OutputStats().nonSilentFrames;}
                const auto& world=view.Simulation().World();const auto& scene=world.Resource<renegade::SceneState>();
                const auto gate_open=[&](std::uint64_t id) {
                    const auto entity=scene.authored_entities.at(id);const auto* clip=world.Get<engine::gameplay::ClipPlayback>(entity);
                    const auto* collider=world.Get<engine::gameplay::KinematicCollider>(entity);return clip && collider && clip->frame==Engine::Math::Fixed::FromInt(2);
                };
                const auto gate_hit=[&](std::uint64_t id) {
                    using namespace Engine::Math;const auto entity=scene.authored_entities.at(id);const auto& pose=world.Get<engine::gameplay::AffinePose>(entity)->transform;
                    const auto start=pose.Point({{},Fixed::FromInt(-2),Fixed::One()});const auto end=pose.Point({{},Fixed::FromInt(2),Fixed::One()});
                    return world.Resource<engine::gameplay::CollisionGeometry>().scene->Cast({start,{Fixed::FromRatio(1,5),Fixed::FromRatio(1,5),Fixed::FromRatio(9,10)}},end-start,1);
                };
                if(!tutorial_closed_gates && view.Simulation().Tick()>=180) {
                    const auto logan=scene.authored_entities.at(400005);const auto alignment=script_look_alignment(view);
                    const auto face=facing_alignment(world,logan,scene.player);
                    if(view.ScriptCameraLooks()!=1 || view.LastCameraLookTick()!=122 || alignment<0.999f || face<0.999f) {
                        std::fprintf(stderr,"source intro camera/facing failed: looks=%zu tick=%llu camera=%.6f actor=%.6f\n",view.ScriptCameraLooks(),static_cast<unsigned long long>(view.LastCameraLookTick()),alignment,face);result=1;break;
                    }
                    std::printf("SDL3 tutorial source camera/facing passed: actor toward player, camera toward original actor height offset, alignment=%.6f\n",alignment);
                    for(const auto id:{400142ull,400143ull,400144ull,400146ull}) {
                        const auto hit=gate_hit(id);if(gate_open(id) || !hit || hit->subject!=id) {std::fprintf(stderr,"tutorial closed gate collision failed: %llu\n",id);result=1;break;}
                    }
                    if(result) break;tutorial_closed_gates=true;
                    std::printf("SDL3 tutorial closed gates passed: all four source IDs block the entrance with posed W3D collision\n");
                }
                const auto control_message=[&](std::int64_t parameter) {
                    engine::gameplay::BehaviorMessage message;message.recipient=scene.player;message.event=std::uint32_t(renegade::content::MissionEvent::Custom);
                    message.arguments={1,1,parameter,400005};message.argument_count=4;message.due_tick=view.Simulation().Tick()+1;
                    view.QueueBehaviorMessage(message);
                };
                const auto control_key=[&](engine::platform::EventType type) {
                    engine::platform::PlatformEvent event;event.type=type;event.key=KeyCode::w;event.window=window->id();
                    return platform.events().post(event);
                };
                const auto* player_pose=world.Get<engine::gameplay::Transform>(scene.player);
                if(tutorial_closed_gates && !tutorial_control_phase) {
                    if(view.InitializedBehaviors(scene.player,scene.start_script)!=1 || view.InitializedBehaviors(scene.player,"MTU_Commando")!=1) {
                        std::fprintf(stderr,"saved tutorial startup did not attach exactly one initialized commando observer\n");result=1;break;
                    }
                    std::printf("SDL3 tutorial player startup passed: saved MTU_Commando_Startup attached one initialized MTU_Commando observer in shared SoA\n");
                    control_message(2);tutorial_control_phase=1;
                } else if(tutorial_control_phase==1 && !view.InputAllowed()) {
                    tutorial_control_tick=view.Simulation().Tick();tutorial_control_position=player_pose->position;tutorial_control_facing=player_pose->facing;
                    if(!control_key(engine::platform::EventType::key_down)) {result=1;break;}tutorial_control_phase=2;
                } else if(tutorial_control_phase==2) {
                    if(view.InputAllowed() || player_pose->position.x!=tutorial_control_position.x || player_pose->position.y!=tutorial_control_position.y || player_pose->facing!=tutorial_control_facing) {
                        std::fprintf(stderr,"script control permission failed with actual SDL3 held movement\n");result=1;break;
                    }
                    if(view.Simulation().Tick()>=tutorial_control_tick+12) {
                        std::printf("SDL3 tutorial script control lock passed: 12 fixed ticks with SDL3 movement held and unchanged planar pose/facing\n");
                        control_message(1);tutorial_control_phase=3;
                    }
                } else if(tutorial_control_phase==3 && view.InputAllowed()) {
                    tutorial_control_tick=view.Simulation().Tick();tutorial_control_position=player_pose->position;
                    if(!control_key(engine::platform::EventType::key_down)) {result=1;break;}tutorial_control_phase=4;
                } else if(tutorial_control_phase==4 && view.Simulation().Tick()>=tutorial_control_tick+12) {
                    using namespace Engine::Math;const auto distance=Length(FixedVector2{player_pose->position.x-tutorial_control_position.x,player_pose->position.y-tutorial_control_position.y});
                    if(!view.InputAllowed() || distance<Fixed::FromRatio(1,10) || !control_key(engine::platform::EventType::key_up)) {
                        std::fprintf(stderr,"script control enable did not restore SDL3 movement through real scene collision\n");result=1;break;
                    }
                    std::printf("SDL3 tutorial script control enable passed: 12 fixed ticks, actual planar displacement.raw=%lld\n",static_cast<long long>(distance.Raw()));tutorial_control_phase=5;
                }
                if(!tutorial_silence_checked && view.ConversationLines()==1 && view.Simulation().Tick()>=180) {
                    if(mixer.OutputStats().nonSilentFrames!=tutorial_audio_start) {std::fprintf(stderr,"tutorial dialogue isolation failed before first voiced remark\n");result=1;break;}
                    tutorial_silence_checked=true;std::printf("SDL3 tutorial dialogue isolation passed: original silent intro remark, muted music/effects/ambient/cinematic output\n");
                }
                if(tutorial_control_phase==5 && view.ConversationsFinished() && !view.HelpText().text.Get().empty() && view.HelpTextDrawn() && gate_open(400142) && gate_open(400143)) {
                    if(!tutorial_closed_gates || gate_hit(400142) || gate_hit(400143) || gate_open(400144) || gate_open(400146)) {std::fprintf(stderr,"tutorial entrance gate opening/collision failed\n");result=1;break;}
                    std::printf("SDL3 tutorial opened gates passed: original Logan completion opens 400142/400143 to frame 2 at 5fps; entrance sweeps clear, remaining gates held closed\n");
                    const auto delivered=mixer.OutputStats().nonSilentFrames-tutorial_audio_start;
                    if(!tutorial_silence_checked || !view.PreparedConversationLines("MTU_LOGAN_START") || view.ConversationLines()!=view.PreparedConversationLines("MTU_LOGAN_START") || delivered<4800) {
                        std::fprintf(stderr,"tutorial intro speech incomplete: lines=%zu/%zu delivered=%llu\n",view.ConversationLines(),view.PreparedConversationLines("MTU_LOGAN_START"),static_cast<unsigned long long>(delivered));result=1;break;
                    }
                    std::printf("SDL3 retail tutorial speech passed: %zu original remarks, %llu non-silent output frames, shared Lua timers/custom events and ECS audio; remaining tutorial behavior is incomplete\n",view.ConversationLines(),static_cast<unsigned long long>(delivered));
                    std::printf("SDL3 tutorial HUD help passed: original localized help-screen instruction, source font, half-opacity sentence above reticle\n");
                    menu.capture=capture;menu.captured=false;menu.check_tutorial_gates=true;tutorial_complete=true;
                }
            }
            if(validate_tutorial_course && menu.level_view && menu.level_view->Ready() && !course_complete) {
                using namespace Engine::Math;using namespace engine::gameplay;
                auto& view=*menu.level_view;auto& simulation=view.Simulation();auto& world=simulation.World();const auto& state=world.Resource<renegade::SceneState>();
                const auto logan=state.authored_entities.at(400005),zone=state.authored_entities.at(400015);
                const auto key=[&](engine::platform::EventType type,KeyCode button=KeyCode::w) {engine::platform::PlatformEvent event;event.type=type;event.key=button;event.window=window->id();return platform.events().post(event);};
                const auto* logan_pose=world.Get<Transform>(logan);const auto* action=world.Get<renegade::HumanGoto>(logan);
                const auto* player_pose=world.Get<Transform>(state.player);
                if(!course_phase && view.ConversationsFinished()==1 && world.Get<ClipPlayback>(state.authored_entities.at(400142))->frame==Fixed::FromInt(2)) {
                    const auto& volumes=world.Resource<TriggerVolumes>().volumes;const auto trigger=std::ranges::find(volumes,zone,&TriggerVolume::subject);
                    if(trigger==volumes.end() || !world.IsAlive(zone)) {std::fprintf(stderr,"retail jump trigger is unavailable before course fixture\n");result=1;break;}
                    // Explicit fixture placement only. All following callbacks,
                    // NPC movement, collision and speech run through the game.
                    auto* pose=world.Get<Transform>(state.player);pose->position=trigger->bounds.center;
                    auto& affine=world.Get<AffinePose>(state.player)->transform.elements;affine[3]=pose->position.x;affine[7]=pose->position.y;affine[11]=pose->position.z;
                    *world.Get<renegade::HumanState>(state.player)=renegade::HumanState{};player_input->ClearHeld();
                    course_start_tick=simulation.Tick();course_audio_start=mixer.OutputStats().nonSilentFrames;course_logan_start=logan_pose->position;course_phase=1;
                    std::printf("SDL3 tutorial course fixture: player placed inside retail zone 400015; no lesson callback or NPC completion injected, tick=%llu\n",static_cast<unsigned long long>(course_start_tick));
                } else if(course_phase) {
                    if(simulation.Tick()>course_start_tick+6000) {
                        const auto* contacts=world.Get<SweepContacts>(logan);const auto* route=world.Get<RouteTraversal>(logan);
                        const auto* player_state=world.Get<renegade::HumanState>(state.player);const auto* player_contacts=world.Get<SweepContacts>(state.player);
                        std::fprintf(stderr,"tutorial course timeout: phase=%u action=%u active=%u curve=%u parameter.raw=%lld Logan=(%lld,%lld,%lld) contacts=%u overlap=%u player=(%.3f,%.3f,%.3f) grounded=%u contacts=%u\n",course_phase,action->action,action->enabled,route->curve,static_cast<long long>(route->parameter.Raw()),static_cast<long long>(logan_pose->position.x.Raw()),static_cast<long long>(logan_pose->position.y.Raw()),static_cast<long long>(logan_pose->position.z.Raw()),contacts ? contacts->count : 0,contacts ? contacts->overlapping : 0,ToFloat(player_pose->position.x),ToFloat(player_pose->position.y),ToFloat(player_pose->position.z),player_state->grounded,player_contacts->count);result=1;break;
                    }
                    if(course_phase==1 && action->enabled && action->action==1 && !view.InputAllowed()) {
                        if(world.IsAlive(zone)) {std::fprintf(stderr,"one-shot jump zone survived its callback\n");result=1;break;}
                        course_locked=true;course_locked_tick=simulation.Tick();course_player_locked=player_pose->position;course_player_facing=player_pose->facing;
                        if(!key(engine::platform::EventType::key_down)) {result=1;break;}course_phase=2;
                        std::printf("SDL3 tutorial course trigger passed: actual volume entry destroyed zone and started Logan action 1 with persistent player lock\n");
                    }
                    if(course_phase==2) {
                        const auto distance=Length(logan_pose->position.XY()-course_logan_start.XY());if(action->enabled && action->action==1) course_distance=(std::max)(course_distance,distance);
                        const auto* clip=world.Get<ClipPlayback>(logan);course_animation|=clip && clip->clip!=1;
                        if(simulation.Tick()<=course_locked_tick+12 && (view.InputAllowed() || player_pose->position.x!=course_player_locked.x || player_pose->position.y!=course_player_locked.y || player_pose->facing!=course_player_facing)) {
                            std::fprintf(stderr,"lesson-driven player lock failed with SDL3 movement\n");result=1;break;
                        }
                        const auto x=*Fixed::ParseDecimal("-53.697"),y=*Fixed::ParseDecimal("-5.334");
                        if(!action->enabled && Length(logan_pose->position.XY()-FixedVector2{x,y})<Fixed::FromRatio(1,50)) {
                            if(course_distance<Fixed::One() || !course_animation || simulation.Tick()<course_locked_tick+12) {std::fprintf(stderr,"Logan route did not move and animate before its source relocation\n");result=1;break;}
                            if(!key(engine::platform::EventType::key_up)) {result=1;break;}
                            const auto alignment=script_look_alignment(view);const auto face=facing_alignment(world,logan,state.player);
                            const FixedVector3 expected{x,y,Fixed::FromRatio(7,2)};const auto look=view.LastCameraLookTarget();
                            if(view.ScriptCameraLooks()!=2 || look!=expected || view.InputAllowed() || player_pose->facing!=course_player_facing || player_input->Orientation().heading==player_pose->facing || alignment<0.999f || face<0.999f) {
                                std::fprintf(stderr,"locked source camera/facing failed: looks=%zu camera=%.6f actor=%.6f input=%u\n",view.ScriptCameraLooks(),alignment,face,unsigned(view.InputAllowed()));result=1;break;
                            }
                            std::printf("SDL3 tutorial locked camera/facing passed: alignment=%.6f body_heading_held=1 source_target.raw=(%lld,%lld,%lld)\n",alignment,static_cast<long long>(look.x.Raw()),static_cast<long long>(look.y.Raw()),static_cast<long long>(look.z.Raw()));
                            course_relocated=true;course_phase=3;
                            std::printf("SDL3 tutorial course route passed: natural action 1 completion, source relocation, locomotion clip, distance.raw=%lld\n",static_cast<long long>(course_distance.Raw()));
                        }
                    }
                    if(course_phase==3 && action->action==2 && world.Get<ClipPlayback>(state.authored_entities.at(400144))->frame==Fixed::FromInt(2) && view.InputAllowed() && view.HelpTextDrawn()) {
                        const auto expected=menu.strings.Lookup("IDS_MTUDSGN_DSGN0383I1DSGN_TXT");
                        if(view.HelpText().text.Get()!=expected) {std::fprintf(stderr,"jump speech did not publish its original localized help\n");result=1;break;}
                        menu.capture=capture;menu.captured=false;course_help_captured=true;course_phase=4;
                        std::printf("SDL3 tutorial course speech passed: jump remarks finished, player enabled, EVA walk requested, third gate open, original HUD help rendered\n");
                    }
                    if(course_phase==4 && menu.captured && !action->enabled && action->action==2) {
                        const auto remarks=view.PreparedConversationLines("MTU_LOGAN_START")+view.PreparedConversationLines("MTU_LOGAN_JUMP_TEST");
                        const auto delivered=mixer.OutputStats().nonSilentFrames-course_audio_start;
                        if(view.ConversationsFinished()!=2 || view.ConversationLines()!=remarks || delivered<4800 || !course_locked || !course_relocated || !course_help_captured || !view.ObjectsDrawn()) {
                            std::fprintf(stderr,"tutorial course speech/render proof incomplete: lines=%zu/%zu finished=%zu output=%llu\n",view.ConversationLines(),remarks,view.ConversationsFinished(),static_cast<unsigned long long>(delivered));result=1;break;
                        }
                        ecs::Query<ecs::Read<VolumeContact>> contacts(world);bool stale{};
                        contacts.ForEachChunk([&](auto chunk) {for(const auto& contact:chunk.template Get<VolumeContact>()) stale|=world.Resource<TriggerVolumes>().volumes.at(contact.volume).subject==zone;});
                        if(stale) {std::fprintf(stderr,"destroyed retail jump zone retained contacts\n");result=1;break;}
                        std::printf("SDL3 retail tutorial course passed: original jump zone, natural Logan jump and EVA routes, source relocation, %zu jump remarks, %llu non-silent output frames, HUD and gate; placement fixture, remaining tutorial incomplete\n",view.PreparedConversationLines("MTU_LOGAN_JUMP_TEST"),static_cast<unsigned long long>(delivered));
                        const auto& portals=world.Resource<renegade::LadderPortals>().portals;
                        if(portals.size()!=8 || portals[0].style!=renegade::content::TransitionStyle::LadderEnterBottom || portals[3].style!=renegade::content::TransitionStyle::LadderExitTop) {std::fprintf(stderr,"retail tutorial ladder portals changed\n");result=1;break;}
                        // Explicit setup placement and upright state, not an
                        // injected transition. E/W go through SDL3 and the DI
                        // tick command; both poses come from shared level data.
                        auto* pose=world.Get<Transform>(state.player);pose->position=portals[0].geometry.bounds.center;
                        world.Get<AffinePose>(state.player)->transform=Engine::Math::FixedAffineTransform3::From_Translation(pose->position);
                        auto* placed_human=world.Get<renegade::HumanState>(state.player);const auto sequence=placed_human->transition_sequence;
                        *placed_human=renegade::HumanState{};placed_human->grounded=1;placed_human->transition_sequence=sequence;
                        player_input->ClearHeld();player_input->SetOrientation({Atan2(portals[0].geometry.destination.elements[4],portals[0].geometry.destination.elements[0]),Fixed{}});
                        if(!view.FirstPerson()) view.ToggleFirstPerson();
                        const auto& ending=portals[3].geometry.destination.elements;course_ladder_exit={ending[3],ending[7],ending[11]};course_ladder_motion_start=view.LadderMotionTicks();
                        window->raise();
                        engine::platform::PlatformEvent event;event.window=window->id();event.type=engine::platform::EventType::focus_gained;
                        if(!platform.events().post(event)) {result=1;break;}
                        event.key=KeyCode::e;event.type=engine::platform::EventType::key_down;
                        const auto down=platform.events().post(event);event.type=engine::platform::EventType::key_up;
                        if(!down || !platform.events().post(event)) {result=1;break;}
                        course_phase=5;std::printf("SDL3 tutorial ladder fixture: player placed upright in original bottom portal; SDL3 focus restoration and E posted, no transition callback injected\n");
                    }
                    const auto* human=world.Get<renegade::HumanState>(state.player);
                    const auto* blend=world.Get<ClipTransition>(state.player);
                    if(course_phase==5 && human->ladder && !human->transition && view.LadderView() && view.FirstPerson() && view.BodyPartsDrawn() && blend->elapsed>=blend->duration) {
                        const auto* clip=world.Get<ClipPlayback>(state.player);const auto& bank=world.Resource<renegade::HumanAnimationCatalog>().banks.at(world.Get<renegade::HumanAnimation>(state.player)->bank-1);
                        if(clip->clip!=bank[21].clip || human->velocity.z!=Fixed{}) {std::fprintf(stderr,"retail ladder entry did not select idle pose with reset velocity\n");result=1;break;}
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder-idle.png");menu.captured=false;course_phase=6;
                    }
                    if(course_phase==6 && menu.captured) {
                        // Default first-person preference stays retained while
                        // ladder view reveals the body; use third person only
                        // for the separate authored exit-animation GPU proof.
                        view.ToggleFirstPerson();
                        if(!key(engine::platform::EventType::key_down)) {result=1;break;}course_phase=7;
                    }
                    if(course_phase==7 && human->transition==1) {
                        ++course_exit_frames;const auto bone=view.PlayerBonePosition(1);if(!bone) {result=1;break;}
                        if(!course_exit_body_start) course_exit_body_start=(*bone)[2];
                        course_exit_body_motion=(std::max)(course_exit_body_motion,(*bone)[2]-*course_exit_body_start);
                        course_camera_weight=(std::max)(course_camera_weight,view.CameraTransitionWeight());
                        const auto frame=world.Get<ClipPlayback>(state.player)->frame;
                        if(!course_exit_capture && frame>=Fixed::FromInt(4) && frame<Fixed::FromInt(8) && view.BodyPartsDrawn()) {
                            menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder-exit-motion.png");menu.captured=false;menu.check_player_body=true;course_exit_capture=true;
                        }
                    }
                    if(course_phase==7 && !human->ladder && !human->transition && view.LadderMotionTicks()>course_ladder_motion_start) {
                        if(!key(engine::platform::EventType::key_up)) {result=1;break;}
                        const auto& bank=world.Resource<renegade::HumanAnimationCatalog>().banks.at(world.Get<renegade::HumanAnimation>(state.player)->bank-1);
                        const auto distance=Length(player_pose->position-course_ladder_exit);
                        if(view.LastLadderMotionClip()!=bank[22].clip || distance>Fixed::FromRatio(1,5) || !view.BodyPartsDrawn() || !course_exit_capture || course_exit_frames<10 || course_exit_body_motion<1.2f || course_camera_weight<.5f) {std::fprintf(stderr,"retail ladder ascent/automatic exit failed: distance.raw=%lld clip=%u transition_frames=%u body_motion=%f\n",static_cast<long long>(distance.Raw()),view.LastLadderMotionClip(),course_exit_frames,course_exit_body_motion);result=1;break;}
                        std::printf("SDL3 retail ladder animated exit passed: frames=%u body_ascent=%.6f camera_previous_weight=%.6f, intermediate original GPU pose captured\n",course_exit_frames,course_exit_body_motion,course_camera_weight);
                        std::printf("SDL3 retail tutorial ladder passed: SDL3 E entry, rendered original idle pose, SDL3 W ascent, %zu fixed climbing ticks with source up clip, natural velocity-gated top exit, distance.raw=%lld; placement fixture\n",view.LadderMotionTicks()-course_ladder_motion_start,static_cast<long long>(distance.Raw()));
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder-exit.png");menu.captured=false;course_phase=8;
                    }
                    const auto& ladder_portals=world.Resource<renegade::LadderPortals>().portals;
                    if(course_phase==8 && menu.captured) {
                        if(ladder_portals[6].style!=renegade::content::TransitionStyle::LadderEnterTop) {result=1;break;}
                        auto* pose=world.Get<Transform>(state.player);pose->position=ladder_portals[6].geometry.bounds.center;
                        world.Get<AffinePose>(state.player)->transform=Engine::Math::FixedAffineTransform3::From_Translation(pose->position);
                        auto* placed_human=world.Get<renegade::HumanState>(state.player);const auto sequence=placed_human->transition_sequence;
                        *placed_human=renegade::HumanState{};placed_human->grounded=1;placed_human->transition_sequence=sequence;
                        player_input->ClearHeld();const auto& destination=ladder_portals[6].geometry.destination.elements;
                        if(!view.FirstPerson()) view.ToggleFirstPerson();
                        player_input->SetOrientation({Atan2(destination[4],destination[0]),Fixed{}});
                        const auto& ending=ladder_portals[5].geometry.destination.elements;course_ladder_exit={ending[3],ending[7],ending[11]};course_ladder_motion_start=view.LadderMotionTicks();course_phase=9;course_placement_tick=simulation.Tick();
                    }
                    if(course_phase==9 && simulation.Tick()>course_placement_tick+2 && human->grounded && !human->just_jumped) {
                        // Publish/render the setup position before E. Otherwise
                        // the camera blends across the unrelated fixture warp.
                        if(!key(engine::platform::EventType::key_down,KeyCode::e) || !key(engine::platform::EventType::key_up,KeyCode::e)) {result=1;break;}
                        course_phase=10;
                        std::printf("SDL3 tutorial second ladder fixture: player placed upright in original top portal; SDL3 E posted, no transition callback injected\n");
                    }
                    if(course_phase==10 && human->transition==3) {
                        ++course_descent_frames;const auto bone=view.PlayerBonePosition(1);if(!bone) {result=1;break;}
                        if(!course_descent_body_start) course_descent_body_start=(*bone)[2];
                        course_descent_body_motion=(std::max)(course_descent_body_motion,*course_descent_body_start-(*bone)[2]);
                        const auto frame=world.Get<ClipPlayback>(state.player)->frame;
                        if(!course_descent_capture && frame>=Fixed::FromInt(4) && frame<Fixed::FromInt(8) && view.BodyPartsDrawn()) {
                            menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder2-entry-motion.png");menu.captured=false;menu.check_player_body=true;course_descent_capture=true;
                        }
                    }
                    if(course_phase==10 && human->ladder && !human->transition && view.LadderView() && view.FirstPerson() && view.BodyPartsDrawn() && blend->elapsed>=blend->duration) {
                        const auto& bank=world.Resource<renegade::HumanAnimationCatalog>().banks.at(world.Get<renegade::HumanAnimation>(state.player)->bank-1);
                        if(world.Get<ClipPlayback>(state.player)->clip!=bank[21].clip) {std::fprintf(stderr,"second ladder entry idle pose failed\n");result=1;break;}
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder2-idle.png");menu.captured=false;course_phase=11;
                    }
                    if(course_phase==11 && menu.captured) {
                        if(!key(engine::platform::EventType::key_down,KeyCode::s)) {result=1;break;}course_phase=12;
                    }
                    if(course_phase==12 && !human->ladder && !human->transition && view.LadderMotionTicks()>course_ladder_motion_start) {
                        if(!key(engine::platform::EventType::key_up,KeyCode::s)) {result=1;break;}
                        const auto& bank=world.Resource<renegade::HumanAnimationCatalog>().banks.at(world.Get<renegade::HumanAnimation>(state.player)->bank-1);
                        const auto distance=Length(player_pose->position-course_ladder_exit);
                        if(view.LastLadderMotionClip()!=bank[23].clip || distance>Fixed::FromRatio(1,5) || view.LadderView() || !view.FirstPerson() || !course_descent_capture || course_descent_frames<10 || course_descent_body_motion<1.2f || !world.Get<DrawHidden>(state.player)) {std::fprintf(stderr,"second ladder descent/automatic exit failed\n");result=1;break;}
                        std::printf("SDL3 retail ladder animated descent passed: frames=%u body_descent=%.6f, intermediate original GPU pose captured, first-person preference and body hiding restored\n",course_descent_frames,course_descent_body_motion);
                        std::printf("SDL3 retail tutorial second ladder passed: SDL3 E entry, settled original idle GPU pose, SDL3 S descent, %zu fixed climbing ticks with source down clip, natural bottom exit, distance.raw=%lld; placement fixture\n",view.LadderMotionTicks()-course_ladder_motion_start,static_cast<long long>(distance.Raw()));
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-ladder2-exit.png");menu.captured=false;course_phase=13;
                    }
                    if(course_phase==13 && menu.captured) {
                        const auto eva_zone=state.authored_entities.at(400016);const auto& volumes=world.Resource<TriggerVolumes>().volumes;const auto trigger=std::ranges::find(volumes,eva_zone,&TriggerVolume::subject);
                        if(trigger==volumes.end() || !world.IsAlive(eva_zone)) {std::fprintf(stderr,"retail EVA zone missing\n");result=1;break;}
                        auto* pose=world.Get<Transform>(state.player);pose->position=trigger->bounds.center;auto& affine=world.Get<AffinePose>(state.player)->transform.elements;affine[3]=pose->position.x;affine[7]=pose->position.y;affine[11]=pose->position.z;
                        player_input->ClearHeld();course_phase=14;
                        std::printf("SDL3 tutorial EVA fixture: player placed inside original zone 400016; no objective or conversation callbacks injected\n");
                    }
                    if(course_phase==14 && view.ConversationsFinished()==3 && view.Objectives().Current() && view.Objectives().Current()->age>=Fixed::FromInt(2) && view.ObjectivesDrawn()) {
                        const auto& objective=*view.Objectives().Current();const auto& value=objective.value;
                        if(value.id!=1 || value.status!=renegade::ObjectiveStatus::Pending || value.priority!=Fixed::FromInt(99) || value.hud_message!=7384 || world.Resource<renegade::ObjectiveVocabulary>().tokens.at(value.pog)!="POG_M00_1_01.tga" || world.IsAlive(state.authored_entities.at(400016))) {std::fprintf(stderr,"source EVA objective state failed\n");result=1;break;}
                        const FixedVector3 expected{*Fixed::ParseDecimal("-12.39"),*Fixed::ParseDecimal("20.78"),*Fixed::ParseDecimal("1.81")};if(Length(objective.position-expected)>Fixed::FromRatio(1,1000)) {std::fprintf(stderr,"source EVA objective position failed\n");result=1;break;}
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-objective.png");menu.captured=false;menu.check_objective_hud=true;course_phase=15;
                        std::printf("SDL3 tutorial EVA objective passed: id=1 status=0 priority=99 hud_message=7384 texture=POG_M00_1_01.tga; natural EVA completion, destroyed source zone, ECS marker and localized HUD\n");
                    }
                    if(course_phase==15 && menu.captured && view.ConversationsFinished()==4 && view.InputAllowed() && view.ObjectivesDrawn()) {
                        const auto expected=view.PreparedConversationLines("MTU_LOGAN_START")+view.PreparedConversationLines("MTU_LOGAN_JUMP_TEST")+view.PreparedConversationLines("MTU_LOGAN_EVA")+view.PreparedConversationLines("MTU_LOGAN_POKE");
                        if(view.ConversationLines()!=expected || !view.HelpTextDrawn()) {std::fprintf(stderr,"EVA/poke speech or restored controls incomplete\n");result=1;break;}
                        std::printf("SDL3 tutorial EVA/poke speech passed: %zu EVA remarks, %zu poke remarks, original help and controls restored; pistol selection effect remains pending\n",view.PreparedConversationLines("MTU_LOGAN_EVA"),view.PreparedConversationLines("MTU_LOGAN_POKE"));
                        menu.capture=capture.parent_path()/(capture.stem().string()+"-objective-settled.png");menu.captured=false;course_phase=16;
                    }
                    if(course_phase==16 && menu.captured) course_complete=true;
                }
            }
            if(validate_mission_cinematic && menu.level_view && menu.level_view->Ready() && !cinematic_complete) {
                auto& view=*menu.level_view;const auto now=view.CinematicTime();
                cinematic_projectile_frames+=view.ProjectilePartsDrawn()!=0;cinematic_particle_frames+=view.ParticleCount()!=0;
                cinematic_peak_particles=std::max(cinematic_peak_particles,view.ParticleCount());
                cinematic_trail_frames+=view.ProjectileParticleCount()!=0;cinematic_peak_trail_particles=std::max(cinematic_peak_trail_particles,view.ProjectileParticleCount());
                if(cinematic_capture_pending && menu.captured) {
                    if(!view.ObjectsDrawn()) {std::fprintf(stderr,"cinematic GPU frame contains no submitted world objects\n");result=1;break;}
                    const auto position=view.CameraPosition();
                    std::printf("SDL3 retail cinematic capture: stage=%u time.raw=%lld camera=(%.3f,%.3f,%.3f) drawn=%zu projectile_parts=%zu particles=%zu trail_particles=%zu\n",cinematic_stage,static_cast<long long>(now.Raw()),position[0],position[1],position[2],view.ObjectsDrawn(),view.ProjectilePartsDrawn(),view.ParticleCount(),view.ProjectileParticleCount());
                    cinematic_capture_pending=false;++cinematic_stage;
                }
                if(cinematic_stage<cinematic_checkpoints_ms.size() && !cinematic_capture_pending && now>=Engine::Math::Fixed::FromInt(cinematic_checkpoints_ms[cinematic_stage])/Engine::Math::Fixed::FromInt(1000)) {
                    if(!view.CinematicActive() || view.CinematicActors()!=37) {std::fprintf(stderr,"original cinematic camera/actor composition is missing\n");result=1;break;}
                    if(!cinematic_stage) {cinematic_first_camera=view.CameraPosition();cinematic_audio_start=mixer.OutputStats().nonSilentFrames;}
                    else if(view.CameraPosition()==cinematic_first_camera) {std::fprintf(stderr,"retail cinematic camera did not animate\n");result=1;break;}
                    menu.capture=capture.parent_path()/(capture.stem().string()+"-intro-ms-"+std::to_string(cinematic_checkpoints_ms[cinematic_stage])+".png");menu.captured=false;cinematic_capture_pending=true;
                    menu.check_cinematic_effects=cinematic_stage>=4 && cinematic_stage<=7;
                    menu.check_speech_pose=cinematic_stage==1;
                }
                if(cinematic_stage==cinematic_checkpoints_ms.size() && view.CinematicFinished()) {
                    if(view.CinematicActive() || view.CinematicCommandsSeen()!=250 || now<Engine::Math::Fixed::FromInt(75)) {std::fprintf(stderr,"authored cinematic release/completion boundary failed\n");result=1;break;}
                    const auto delivered=mixer.OutputStats().nonSilentFrames-cinematic_audio_start;
                    if(delivered<4800 || view.ConversationLines()!=25 || view.ConversationsFinished()!=25) {std::fprintf(stderr,"cinematic dialogue did not complete all 25 available authored conversations or reach audio output: %zu lines, %zu finished\n",view.ConversationLines(),view.ConversationsFinished());result=1;break;}
                    if(!cinematic_projectile_frames || !cinematic_particle_frames || !cinematic_trail_frames) {std::fprintf(stderr,"cinematic projectile rendering failed: mesh_frames=%zu particle_frames=%zu peak_particles=%zu trail_frames=%zu\n",cinematic_projectile_frames,cinematic_particle_frames,cinematic_peak_particles,cinematic_trail_frames);result=1;break;}
                    std::printf("SDL3 retail cinematic shots passed: %zu projectile mesh frames, %zu particle frames, %zu peak live particles through shared GPU submission\n",cinematic_projectile_frames,cinematic_particle_frames,cinematic_peak_particles);
                    std::printf("SDL3 retail cinematic trails passed: %zu projectile emitter frames, %zu peak projectile particles; detached particles expire after projectile destruction\n",cinematic_trail_frames,cinematic_peak_trail_particles);
                    std::printf("SDL3 retail cinematic speech passed: 26 original conversation attachments, 25 available remarks and natural completions, %llu non-silent dialogue output frames; absent MX0_GDITROOPER4_HIT6 retained without substitution\n",static_cast<unsigned long long>(delivered));
                    std::printf("SDL3 retail cinematic passed: Lua Created startup, 37 prepared actors, nine moving-camera GPU captures, fixed 60Hz playback, 71-second control release, 75-second timeline completion; remaining combat script effects remain incomplete\n");
                    menu.capture=capture;menu.captured=false;cinematic_capture_pending=true;
                }
                if(cinematic_stage==cinematic_checkpoints_ms.size()+1) cinematic_complete=true;
            }
            const auto pause_key=[&] {
                engine::platform::PlatformEvent input;input.window=window->id();input.key=KeyCode::escape;input.type=engine::platform::EventType::key_down;
                const bool posted=platform.events().post(input);input.type=engine::platform::EventType::key_up;
                return platform.events().post(input) && posted;
            };
            const auto pause_click=[&](unsigned id) {
                const auto target=std::ranges::find_if(menu.targets,[&](const auto& item) {return item.id==id && item.visible && item.enabled;});
                if(target==menu.targets.end()) return false;
                const auto logical=window->size(),pixels=window->drawable_size();engine::platform::PlatformEvent input;input.window=window->id();input.code=1;
                input.position={(target->bounds.left+target->bounds.right)*.5f*logical.width/pixels.width,(target->bounds.top+target->bounds.bottom)*.5f*logical.height/pixels.height};
                input.type=engine::platform::EventType::mouse_moved;bool posted=platform.events().post(input);
                input.type=engine::platform::EventType::mouse_button_down;posted=platform.events().post(input) && posted;
                input.type=engine::platform::EventType::mouse_button_up;posted=platform.events().post(input) && posted;
                pause_posted=menu.preview_milliseconds;return posted;
            };
            if(validate_level_preview && !level_return_posted && pause_probe && menu.preview_milliseconds>=pause_posted+500 && !menu.pending_dialog) {
                if(menu.level_view && menu.pause->active.Get() && (menu.level_view->Simulation().Tick()!=pause_tick || menu.level_view->Simulation().World().StateHash()!=pause_hash)) {
                    std::fprintf(stderr,"paused authoritative mission advanced or changed\n");result=1;break;
                }
                const auto page=[&](unsigned id) {return menu.displayed_dialog==id && menu.targets_dialog==id;};
                bool posted=true;
                switch(pause_probe) {
                case 1:if(page(153)) {menu.capture=capture.parent_path()/(capture.stem().string()+"-pause.png");menu.captured=false;pause_probe=2;}break;
                case 2:if(page(153) && menu.captured) {posted=pause_click(11003);pause_probe=3;}break;
                case 3:if(page(135)) {posted=pause_click(11034);pause_probe=4;}break;
                case 4:if(page(153)) {posted=pause_click(11034);pause_probe=5;}break;
                case 5:if(!menu.pause->active.Get() && menu.level_view->Simulation().Tick()>=pause_tick+3) {
                    pause_tick=menu.level_view->Simulation().Tick();pause_hash=menu.level_view->Simulation().World().StateHash();posted=pause_key();pause_posted=menu.preview_milliseconds;pause_probe=6;
                }break;
                case 6:if(page(153)) {posted=pause_click(11020);pause_probe=7;}break;
                case 7:if(page(209)) {posted=pause_click(7);pause_probe=8;}break;
                case 8:if(page(153)) {posted=pause_click(11020);pause_probe=9;}break;
                case 9:if(page(209)) {posted=pause_click(6);pause_probe=10;}break;
                case 10:if(!menu.level_view && page(128)) {
                    std::printf("SDL3 retail pause passed: EVA GPU capture, frozen ECS tick and state hash, nested options, resume, cancelled exit, confirmed teardown\n");
                    menu.capture=capture.parent_path()/(capture.stem().string()+"-menu.png");menu.captured=false;level_return_posted=true;frames=menu.frames+180;
                }break;
                }
                if(!posted) {std::fprintf(stderr,"SDL3 pause menu control input failed at step %u\n",pause_probe);result=1;break;}
            }
			if(validate_level_preview && !level_return_posted && !pause_probe && (!validate_player_motion || motion_complete) && (!validate_frame_timing || frame_timing_complete) && (!validate_mission_cinematic || cinematic_complete) && (!validate_tutorial_intro || tutorial_complete) && (!validate_tutorial_course || course_complete) &&
                (validate_level_cancel ? (menu.level_view && !menu.level_view->Ready() && menu.loading_frames>=10 && menu.loading_captured && menu.loading_events>0)
                                       : (menu.level_ready_frames>=3 && menu.captured))) {
				engine::platform::PlatformEvent key; key.window=window->id(); key.key=KeyCode::escape;
				key.type=engine::platform::EventType::key_down;
				const bool down=platform.events().post(key);
				key.type=engine::platform::EventType::key_up;
				if(!down || !platform.events().post(key)) {std::fprintf(stderr,"SDL3 level return post failed\n");result=1;break;}
                if(validate_level_cancel) {menu.capture=capture.parent_path()/(capture.stem().string()+"-menu.png");menu.captured=false;level_return_posted=true;frames=menu.frames+180;}
                else {pause_probe=1;pause_tick=menu.level_view->Simulation().Tick();pause_hash=menu.level_view->Simulation().World().StateHash();pause_posted=menu.preview_milliseconds;}
			}
            engine::platform::PlatformEvent event;
            while(platform.events().poll(event)) {
                if(event.type==engine::platform::EventType::quit || event.type==engine::platform::EventType::window_close_requested) running=false;
                if(event.window && event.window!=window->id()) continue;
                if(event.type==engine::platform::EventType::window_resized && event.size.width>0 && event.size.height>0)
                    resized=event.size;
                const auto logical=window->size(),pixels=window->drawable_size();
                if(logical.width>0 && logical.height>0) {
                    event.position.x*=float(pixels.width)/logical.width;
                    event.position.y*=float(pixels.height)/logical.height;
                }
                if(intro.Playing()) {
                    if(event.type==engine::platform::EventType::key_down && !event.repeat && event.key==engine::platform::KeyCode::escape) {
                        const std::string name(intro.Movie());const bool accepted=intro.Skip();
                        if(validate_intro_skip) {if(accepted) ++accepted_skips;else ++blocked_skips;}
                        std::printf("intro Escape %s: %s\n",accepted ? "accepted" : "blocked",name.c_str());
                        if(accepted && !start_movie()) { result=1;running=false; }
                    }
                    continue;
                }
                if(menu.level_view) {
                    if(!menu.level_view->Ready() && event.type==engine::platform::EventType::key_down && event.key==KeyCode::f12) ++menu.loading_events;
                    if(event.type==engine::platform::EventType::key_down && !event.repeat && event.key==KeyCode::escape) {
                        if(!menu.level_view->Ready()) {leave_level();continue;}
                        if(!menu.pause->active.Get()) {menu.pause->Open();continue;}
                        if(menu.displayed_dialog==153) {menu.pause->Resume();continue;}
                        if(menu.pause->ExitPending()) {menu.pause->ConfirmExit(false);continue;}
                    }
                    if(!menu.pause->active.Get()) {
                        if(player_input && menu.level_view->Ready()) {
                            player_input->SetControlAllowed(menu.level_view->InputAllowed());
                            player_input->Handle(event);if(player_input->TakeViewToggle()) menu.level_view->ToggleFirstPerson();
                            if(player_input->TakeObjectiveCycle()) menu.level_view->CycleObjective();
                        }
                        continue;
                    }
                }
                if(menu.gallery.playing.Get()) {
                    if(event.type==engine::platform::EventType::key_down && !event.repeat && event.key==KeyCode::escape) {
                        ++gallery_escapes;stop_gallery();std::printf("SDL3 gallery Escape stopped movie and retained gallery selection %d\n",menu.gallery.list.selected.Get());
                    }
                    continue;
                }
                if(menu.pending_dialog || (menu.displayed_dialog==128 && !menu.transition.Done())) continue;
                using namespace engine::gui::w3d;
                if(event.type==engine::platform::EventType::focus_lost) {
                    if(auto* list=ActiveList(menu)) list->scrollbar.CaptureLost();
                }
                if(auto* list=ActiveList(menu)) if(const auto* layout=ActiveScrollLayout(menu)) {
                    auto& bar=list->scrollbar;
                    if(event.type==engine::platform::EventType::mouse_moved && bar.Captured()) {
                        bar.PointerMove(event.position.y,layout->geometry);continue;
                    }
                    if(event.type==engine::platform::EventType::mouse_button_up && event.code==1 && bar.Captured()) {
                        bar.PointerUp();menu.input.pressed.reset();continue;
                    }
                    if(event.type==engine::platform::EventType::mouse_button_down && event.code==1 &&
                        HitTest(menu.targets,event.position.x,event.position.y)==list_scrollbar_target) {
                        bar.PointerDown(event.position.x,event.position.y,layout->geometry);menu.input.pressed.reset();continue;
                    }
                }
                if(menu.displayed_dialog==216 && menu.input.focused==1310) {
                    if(event.type==engine::platform::EventType::text_input) {menu.control_profiles->name.InsertUtf8(event.text,menu.preview_milliseconds);continue;}
                    if(event.type==engine::platform::EventType::text_editing) {menu.control_profiles->name.ComposeUtf8(event.text,event.text_start,event.text_length);continue;}
                }
                if(event.type==engine::platform::EventType::mouse_wheel) {
                    const float delta=event.flipped ? -event.y : event.y;
                    if(std::isfinite(delta) && delta!=0) {
                        if(menu.displayed_dialog==170 && menu.input.focused==1206) menu.gallery.list.Wheel(delta>0 ? -1 : 1);
                        else if(menu.displayed_dialog==216 && menu.input.focused==1206) menu.control_profiles->list.Wheel(delta>0 ? -1 : 1);
                        else if(menu.input.focused && menu.entries.contains(*menu.input.focused)) move_group(delta>0 ? -1 : 1,true);
                        else if(menu.displayed_dialog==136 && menu.input.focused && ControlCapture(menu,*menu.input.focused))
                            menu.controls_settings->Pointer(MenuPage(menu),*menu.input.focused,CapturePointer::Wheel,
                                renegade::content::InputFromName(delta>0 ? "Mouse_Wheel_Forward" : "Mouse_Wheel_Backward"),menu.preview_milliseconds);
                        else if(menu.displayed_dialog==136 && menu.input.focused==1337)
                            menu.controls_tabs.Key(delta>0 ? SelectionAction::Previous : SelectionAction::Next);
                        else if(menu.displayed_dialog==169 && menu.input.focused==1223)
                            menu.tech_options.Key(delta>0 ? ValueKey::Up : ValueKey::Down);
                        focus_entries();
                    }
                }
                if(event.type==engine::platform::EventType::mouse_moved) {
                    PointerMove(menu.input,menu.targets,float(event.position.x),float(event.position.y));
                    if(menu.input.pressed && menu.entries.contains(*menu.input.pressed)) menu.input.focused=menu.input.pressed;
                    else if(menu.input.hovered && menu.entries.contains(*menu.input.hovered)) menu.input.focused=menu.input.hovered;
                    if(menu.dragging_slider) move_slider(*menu.dragging_slider,event.position.x);
                    if(menu.displayed_dialog==216 && menu.input.pressed==1310 && menu.profile_edit_layout)
                        menu.control_profiles->name.Place(TextEditHit(*menu.profile_edit_layout,event.position.x),true,menu.preview_milliseconds);
                }
                if(event.type==engine::platform::EventType::mouse_button_down && event.code==1) {
                    PointerPress(menu.input,menu.targets,float(event.position.x),float(event.position.y));
                    focus_entries();
                    if(menu.displayed_dialog==216 && menu.input.pressed==1310 && menu.profile_edit_layout) {
                        menu.control_profiles->name.Place(TextEditHit(*menu.profile_edit_layout,event.position.x),false,menu.preview_milliseconds);
                        continue;
                    }
                    if(menu.displayed_dialog==216 && menu.input.pressed==1206 && menu.profile_layout) {
                        if(const auto row=menu.control_profiles->list.HitRow(event.position.y,menu.profile_layout->text.top)) menu.control_profiles->list.Select(*row);
                        menu.input.pressed.reset();continue;
                    }
                    if(menu.input.pressed && ControlCapture(menu,*menu.input.pressed)) {
                        menu.controls_settings->Pointer(MenuPage(menu),*menu.input.pressed,CapturePointer::Primary,
                            renegade::content::InputFromName("Left_Mouse_Button"),menu.preview_milliseconds);
                        menu.input.pressed.reset();continue;
                    }
                    if(menu.displayed_dialog==170 && menu.input.pressed==1206 && menu.gallery_layout) {
                        if(const auto row=menu.gallery.list.HitRow(event.position.y,menu.gallery_layout->text.top)) {
                            menu.gallery.list.Select(*row);
                            if(event.clicks>=2) menu.gallery.Begin();
                        }
                        menu.input.pressed.reset();continue;
                    }
                    if(menu.input.pressed && menu.entries.contains(*menu.input.pressed)) press_entry(*menu.input.pressed,true);
                    else if(menu.input.pressed) control_sound(control_kind(*menu.input.pressed),renegade::presentation::InterfaceInteraction::PointerDown,*menu.input.pressed);
                    if(menu.input.pressed && ControlCheck(menu,*menu.input.pressed)) {
                        // CheckboxCtrl toggles on down, including a later release
                        // outside its caption. Do not activate it a second time.
                        activate(*menu.input.pressed);menu.input.pressed.reset();
                    }
                    if(menu.input.pressed && ControlSlider(menu,*menu.input.pressed)) {
                        menu.dragging_slider=menu.input.pressed;move_slider(*menu.dragging_slider,event.position.x);
                    }
                }
                if(event.type==engine::platform::EventType::mouse_button_down && (event.code==2 || event.code==3) &&
                    menu.input.focused && ControlCapture(menu,*menu.input.focused)) {
                    menu.controls_settings->Pointer(MenuPage(menu),*menu.input.focused,event.code==2 ? CapturePointer::Middle : CapturePointer::Secondary,
                        renegade::content::InputFromName(event.code==2 ? "Center_Mouse_Button" : "Right_Mouse_Button"),menu.preview_milliseconds);
                }
                if(event.type==engine::platform::EventType::mouse_button_up && event.code==1) {
                    const auto pressed=menu.input.pressed;
                    if(pressed && menu.entries.contains(*pressed)) menu.entries.at(*pressed)->state.Release(menu.preview_milliseconds);
                    if(menu.dragging_slider) {menu.dragging_slider.reset();menu.input.pressed.reset();}
                    else if(auto id=PointerRelease(menu.input,menu.targets,float(event.position.x),float(event.position.y))) {
                        if(!menu.entries.contains(*id)) activate(*id);
                    }
                }
                if(event.type==engine::platform::EventType::key_down) {
                    if(!event.repeat && event.key==engine::platform::KeyCode::escape) {
                        if(menu.displayed_dialog==209) {
                            if(menu.control_profiles->Pending()) menu.control_profiles->Confirm(false);else menu.controls_settings->Confirm(false);
                        }
                        menu.navigation.RememberFocus(menu.input.focused);menu.navigation.back.Execute();menu.dragging_slider.reset();continue;
                    }
                    if(event.key!=KeyCode::tab && menu.input.focused && ControlCapture(menu,*menu.input.focused)) {
                        menu.controls_settings->QueueKeyboard(MenuPage(menu),*menu.input.focused,unsigned(event.key));continue;
                    }
                    if(menu.displayed_dialog==216 && menu.input.focused==1310) {
                        std::optional<EditAction> action;
                        switch(event.key) {
                        case KeyCode::backspace:action=EditAction::Backspace;break;case KeyCode::del:action=EditAction::Delete;break;
                        case KeyCode::home:action=EditAction::Home;break;case KeyCode::end:action=EditAction::End;break;
                        case KeyCode::left:action=EditAction::Left;break;case KeyCode::right:action=EditAction::Right;break;
                        case KeyCode::enter:case KeyCode::keypad_enter:action=EditAction::Enter;break;
                        case KeyCode::a:if(event.modifiers&engine::platform::modifier_control) action=EditAction::SelectAll;break;
                        default:break;
                        }
                        if(action) {
                            const auto handled=menu.control_profiles->name.Key(*action,event.modifiers&engine::platform::modifier_shift,
                                event.modifiers&engine::platform::modifier_control,menu.preview_milliseconds);
                            if(handled.submit) process_profile(menu.control_profiles->Save());
                            if(handled.handled) continue;
                        }
                    }
                    if(menu.displayed_dialog==136 && menu.input.focused==1337) {
                        using A=SelectionAction;std::optional<A> action;
                        switch(event.key) {
                        case KeyCode::left:case KeyCode::up:action=A::Previous;break;
                        case KeyCode::right:case KeyCode::down:action=A::Next;break;
                        case KeyCode::home:action=A::First;break;case KeyCode::end:action=A::Last;break;default:break;
                        }
                        if(action) {menu.controls_tabs.Key(*action);continue;}
                    }
                    if(menu.displayed_dialog==153 && menu.input.focused==40002) {
                        using A=SelectionAction;std::optional<A> action;
                        switch(event.key) {
                        case KeyCode::left:case KeyCode::up:action=A::Previous;break;
                        case KeyCode::right:case KeyCode::down:action=A::Next;break;
                        case KeyCode::home:action=A::First;break;case KeyCode::end:action=A::Last;break;default:break;
                        }
                        if(action) {menu.pause->tabs.Key(*action);continue;}
                    }
                    if((menu.displayed_dialog==170 || menu.displayed_dialog==216) && menu.input.focused==1206) {
                        std::optional<ListAction> action;
                        switch(event.key) {
                        case KeyCode::left:case KeyCode::up:action=ListAction::Previous;break;
                        case KeyCode::right:case KeyCode::down:action=ListAction::Next;break;
                        case KeyCode::home:action=ListAction::First;break;case KeyCode::end:action=ListAction::Last;break;
                        case KeyCode::page_up:action=ListAction::PagePrevious;break;case KeyCode::page_down:action=ListAction::PageNext;break;
                        case KeyCode::space:case KeyCode::enter:case KeyCode::keypad_enter:action=ListAction::Activate;break;
                        default:break;
                        }
                        if(action) {
                            if(menu.displayed_dialog==170) {if(menu.gallery.list.Key(*action).activate) menu.gallery.Begin();}
                            else menu.control_profiles->list.Key(*action);
                            continue;
                        }
                    }
                    if(menu.displayed_dialog==169 && menu.input.focused==1223) {
                        if(menu.tech_options.Key(TranslateValueKey(event.key))) continue;
                    }
                    if(menu.input.focused) {
                        const auto key=TranslateValueKey(event.key);
                        if(auto* model=ControlSlider(menu,*menu.input.focused)) if(HandleValueKey(*model,key)) continue;
                        if(auto* model=ControlCheck(menu,*menu.input.focused))
                            if(HandleValueKey(*model,key) || key==engine::gui::w3d::ValueKey::Enter) continue;
                    }
                    if(event.repeat) continue;
                    if(event.key==engine::platform::KeyCode::tab) MoveFocus(menu.input,menu.targets,(event.modifiers & engine::platform::modifier_shift)!=0);
                    if(menu.input.focused && menu.entries.contains(*menu.input.focused) &&
                        (event.key==engine::platform::KeyCode::left || event.key==engine::platform::KeyCode::right || event.key==KeyCode::up || event.key==KeyCode::down))
                        move_group(event.key==KeyCode::left || event.key==KeyCode::up ? -1 : 1);
                    if(event.key==engine::platform::KeyCode::enter || event.key==engine::platform::KeyCode::keypad_enter || event.key==engine::platform::KeyCode::space)
                        if(auto id=ActivateFocused(menu.input,menu.targets)) {
                            if(menu.entries.contains(*id)) press_entry(*id);
                            else {const auto kind=control_kind(*id);activate(*id);control_sound(kind,renegade::presentation::InterfaceInteraction::KeyboardCommand,*id);}
                        }
                }
                focus_entries();
            }
            menu.controls_settings->FlushKeyboard();focus_entries();
            if(!menu.focus_pending) menu.navigation.RememberFocus(menu.input.focused);
            if(expected_player_names && std::ranges::none_of(menu.entries,
                [](const auto& item){return item.second->state.phase.Get()==engine::gui::w3d::MenuEntryPhase::Pressed;})) {
                if(menu.multiplayer_options->player_names.checked.Get()!=*expected_player_names) {
                    std::fprintf(stderr,"SDL3 multiplayer-options state failed at step %zu\n",playback_step);result=1;break;
                }
                ++multiplayer_input_checks;std::printf("SDL3 multiplayer-options state passed: step=%zu player_names=%u applied=%u\n",
                    playback_step,unsigned(*expected_player_names),unsigned(menu.multiplayer_options->Applied()));expected_player_names.reset();
            }
            if((expected_profile_count || expected_profile_current || expected_name_enabled) && std::ranges::none_of(menu.entries,
                [](const auto& item){return item.second->state.phase.Get()==engine::gui::w3d::MenuEntryPhase::Pressed;})) {
                const auto& catalog=menu.profile_store->Catalog();
                if((expected_profile_count && catalog.profiles.size()!=*expected_profile_count) ||
                    (expected_profile_current && catalog.current!=*expected_profile_current) ||
                    (expected_name_enabled && menu.control_profiles->name.enabled.Get()!=*expected_name_enabled)) {
                    std::fprintf(stderr,"SDL3 profile state failed at step %zu: current=%s entries=%zu edit=%u\n",playback_step,catalog.current.c_str(),catalog.profiles.size(),unsigned(menu.control_profiles->name.enabled.Get()));result=1;break;
                }
                ++profile_input_checks;std::printf("SDL3 profile state passed: step=%zu current=%s entries=%zu edit=%u\n",playback_step,catalog.current.c_str(),catalog.profiles.size(),unsigned(menu.control_profiles->name.enabled.Get()));
                expected_profile_count.reset();expected_profile_current.reset();expected_name_enabled.reset();
            }
            if(expected_binding || expected_controls_tab>=0 || expected_sensitivity>=0) {
                bool valid=true;
                if(expected_binding) valid &= menu.controls_settings->Configuration().Get(expected_controls_function)==*expected_binding;
                if(expected_controls_tab>=0) valid &= menu.controls_tabs.position.Get()==unsigned(expected_controls_tab);
                if(expected_sensitivity>=0) valid &= menu.controls_settings->sensitivity.position.Get()==expected_sensitivity;
                if(!valid) {std::fprintf(stderr,"SDL3 controls state failed at step %zu function %.*s\n",playback_step,
                    int(expected_controls_function.size()),expected_controls_function.data());result=1;break;}
                ++controls_input_checks;std::printf("SDL3 controls state passed: step=%zu function=%.*s tab=%u sensitivity=%d conflict=%u\n",
                    playback_step,int(expected_controls_function.size()),expected_controls_function.data(),
                    menu.controls_tabs.position.Get(),menu.controls_settings->sensitivity.position.Get(),unsigned(bool(menu.controls_settings->conflict.Get())));
                expected_binding.reset();expected_controls_tab=-1;expected_sensitivity=-1;
            }
            if(expected_focus) {
                if(menu.input.focused!=expected_focus) {std::fprintf(stderr,"SDL3 keyboard focus expected %u\n",*expected_focus);result=1;break;}
                std::printf("SDL3 keyboard focus passed: %u\n",*expected_focus);expected_focus.reset();
            }
            if(expected_tab) {
                if(menu.tech_options.selected_tab.Get()!=*expected_tab) {std::fprintf(stderr,"SDL3 tab selection expected %u\n",*expected_tab);result=1;break;}
                std::printf("SDL3 tab keyboard passed: %u with control focus 1223\n",*expected_tab);expected_tab.reset();
            }
            if(expected_list_selection) {
                if(menu.displayed_dialog!=170 || menu.gallery.list.selected.Get()!=*expected_list_selection ||
                    (*expected_list_selection==-1 && menu.gallery.playing.Get())) {
                    std::fprintf(stderr,"SDL3 gallery selection expected %d in the retained gallery\n",*expected_list_selection);result=1;break;
                }
                std::printf("SDL3 gallery list selection passed: %d\n",*expected_list_selection);expected_list_selection.reset();
            }
            if(expected_performance) {
                const auto& settings=*menu.performance_settings;
                if(menu.displayed_dialog!=169 || menu.tech_options.Resource()!=232 || settings.Values()!=*expected_performance ||
                    settings.expert.checked.Get()!=*expected_expert || settings.level.position.Get()!=expected_performance_level) {
                    const auto values=settings.Values();std::fprintf(stderr,"SDL3 performance values differ: %d,%d,%d,%d,%d,%d,%d expert=%u level=%d\n",
                        values[0],values[1],values[2],values[3],values[4],values[5],values[6],unsigned(settings.expert.checked.Get()),settings.level.position.Get());result=1;break;
                }
                if(menu.performance_texture_replacements==0 && settings.Applied()!=renegade::presentation::PerformanceApplication{}) {
                    std::fprintf(stderr,"performance changes applied before Tech options closed\n");result=1;break;
                }
                const auto values=settings.Values();std::printf("SDL3 performance values passed: %d,%d,%d,%d,%d,%d,%d expert=%u level=%d\n",
                    values[0],values[1],values[2],values[3],values[4],values[5],values[6],unsigned(settings.expert.checked.Get()),settings.level.position.Get());
                ++performance_input_checks;menu.check_performance_frame=true;expected_performance.reset();
            }
            if(expected_video) {
                if(menu.displayed_dialog!=169 || menu.tech_options.Resource()!=233 || menu.video_settings->Levels()!=*expected_video) {
                    const auto levels=menu.video_settings->Levels();std::fprintf(stderr,"SDL3 video levels differ: gamma=%d brightness=%d contrast=%d\n",levels[0],levels[1],levels[2]);result=1;break;
                }
                std::printf("SDL3 video levels passed: gamma=%d brightness=%d contrast=%d\n",(*expected_video)[0],(*expected_video)[1],(*expected_video)[2]);
                menu.check_video_frame=true;expected_video.reset();
            }
            if(expected_music_value) {
                if(menu.audio_settings->volumes[1].position.Get()!=*expected_music_value || menu.input.focused!=1022) {
                    std::fprintf(stderr,"SDL3 slider key expected music=%d with retained focus, got %d\n",
                        *expected_music_value,menu.audio_settings->volumes[1].position.Get());result=1;break;
                }
                std::printf("SDL3 slider key passed: music=%d with retained focus\n",*expected_music_value);
                expected_music_value.reset();
            }
            if(check_press && check_press->released) {
                if(menu.audio_settings->enabled[check_press->index].checked.Get()==check_press->before) {
                    std::fprintf(stderr,"outside release reversed the checkbox press\n");result=1;break;
                }
                std::printf("SDL3 checkbox press passed: toggled before release and retained after outside release\n");
                check_press.reset();
            }
            if(button_press && button_press->released) {
                if(menu.input.pressed || menu.audio_settings->volumes[1].position.Get()!=(*defaults)[1] ||
                    menu.interface_sound_starts[button_press->id]!=button_press->sounds+1) {
                    std::fprintf(stderr,"native button release did not execute once without duplicate feedback\n");result=1;break;
                }
                std::printf("SDL3 native button press passed: click before release, command on release, no duplicate\n");
                button_press.reset();
            }
            if(resized && (resized->width!=drawable.width || resized->height!=drawable.height)) {
                std::printf("SDL3 drawable resize: %d x %d -> %d x %d\n",drawable.width,drawable.height,resized->width,resized->height);
                if(!Graphics::Resize_Frame_Device(resized->width,resized->height,false)) { std::fprintf(stderr,"graphics resize failed\n");result=1;break; }
                drawable=*resized;
                if(!update_menu_camera(drawable.width,drawable.height)) { std::fprintf(stderr,"resized menu camera failed\n");result=1;break; }
                if(!load_fonts(drawable.height)) { std::fprintf(stderr,"resized fonts failed\n");result=1;break; }
                if(pending_resize) {
                    std::printf("SDL3 resize validation %s: drawable %d x %d, fonts rebuilt\n",pending_resize->c_str(),drawable.width,drawable.height);
                    resize_completed.insert(*pending_resize);pending_resize.reset();
                }
            }
            const auto now=std::chrono::steady_clock::now();
            // This independent runtime fixture advances the ordinary marquee
            // in quarter-second steps; normal gameplay and real-time tests retain their clocks.
            const bool interactive_clock=!frames || validate_frame_timing && menu.level_view && menu.level_view->Ready() && !frame_timing_complete;
            const auto frame_elapsed=interactive_clock ? std::chrono::duration_cast<std::chrono::nanoseconds>(now-last) : std::chrono::nanoseconds{16'666'667};
            const float elapsed=validate_credits && menu.displayed_dialog==172 ? 0.25f : std::chrono::duration<float>(frame_elapsed).count();last=now;
            if(const auto requested=menu.gallery.TakeRequest()) {
                movie.Close();auto bytes=files.Read(std::string_view(*requested).substr(5));
                if(!bytes) {std::fprintf(stderr,"gallery movie is unavailable: %s\n",requested->c_str());menu.gallery.Complete();}
                else {
                    movie_source=std::make_unique<Engine::Video::MemorySource>(std::move(*bytes));
                    if(!movie.Open(*movie_source)) {std::fprintf(stderr,"gallery movie cannot decode: %s\n",requested->c_str());stop_gallery();}
                    else {
                        gallery_audio=renegade::presentation::CaptureAudioPreferences(*menu.audio_settings);
                        for(const auto bus:{engine::audio::Bus::Effects,engine::audio::Bus::Music,engine::audio::Bus::Speech,
                            engine::audio::Bus::Cinematic,engine::audio::Bus::Interface,engine::audio::Bus::Ambient}) mixer.SetBusGain(bus,0);
                        mixer.Stop(music_voice,0);music_voice=0;
                        gallery_pcm_start=movie_sound.Queued();gallery_output_start=mixer.OutputStats();++gallery_starts;
                        movie.Set_Mode(Engine::Video::PlaybackMode::Once);movie.Play();
                        std::printf("movie gallery playing: %s; gameplay audio suppressed\n",requested->c_str());
                    }
                }
            }
            if(menu.gallery.playing.Get()) {
                movie.Update(elapsed);
                if(movie.State()==Engine::Video::PlaybackState::Error) {std::fprintf(stderr,"gallery movie decode failed\n");stop_gallery();result=1;break;}
                if(movie.State()==Engine::Video::PlaybackState::Finished) {
                    ++gallery_natural;
                    if(validate_gallery && menu.presented_movie_frames["gallery"].size()!=movie.Info().frame_count) {
                        std::fprintf(stderr,"natural gallery playback did not present every decoded frame\n");result=1;running=false;
                    }
                    stop_gallery();std::printf("movie gallery naturally completed\n");
                }
            }
            else if(intro.Playing()) {
                movie.Update(elapsed);
                if(movie.State()==Engine::Video::PlaybackState::Error) { std::fprintf(stderr,"movie decode failed\n");result=1;break; }
                if(movie.State()==Engine::Video::PlaybackState::Finished) {
                    const auto stats=mixer.OutputStats();
                    const auto non_silent=stats.nonSilentFrames-movie_output_start.nonSilentFrames;
                    std::printf("movie %.*s: %llu non-silent stereo frames delivered to audio output\n",
                        int(intro.Movie().size()),intro.Movie().data(),static_cast<unsigned long long>(non_silent));
                    if(validate_menu && realtime && non_silent<1000) {
                        std::fprintf(stderr,"intro produced insufficient non-silent audio output\n");result=1;break;
                    }
                    std::printf("intro naturally completed: %.*s\n",int(intro.Movie().size()),intro.Movie().data());
                    intro.Complete();if(!start_movie()) { result=1;break; }
                }
            }
            menu.movie_visible=intro.Playing() || menu.gallery.playing.Get();
            if(!menu.movie_visible && ((menu.displayed_dialog==170 && menu.gallery.list.selected.Get()!=-1) ||
                (menu.displayed_dialog==216 && menu.control_profiles->list.selected.Get()!=-1))) {
                menu.gallery_pulse+=menu.gallery_pulse_direction*renegade::content::ListStyle::pulse_rate*elapsed;
                if(menu.gallery_pulse<0 || menu.gallery_pulse>1) {menu.gallery_pulse_direction=-menu.gallery_pulse_direction;menu.gallery_pulse=std::clamp(menu.gallery_pulse,0.f,1.f);}
            }
            if(!intro.Playing() && menu.displayed_dialog==172 && menu.credits_layout)
                menu.credits_scroll.Advance(elapsed);
            if(validate_credits && menu.displayed_dialog==172 && menu.credits_layout) {
                const float fraction=menu.credits_scroll.position.Get()/menu.credits_scroll.Extent();
                std::string checkpoint;
                if(menu.credits_scroll.Cycles()>0) checkpoint="restart";
                else if(fraction>=0.90f) checkpoint="final";
                else if(fraction>=0.50f) checkpoint="middle";
                if(!checkpoint.empty() && credits_checkpoints.insert(checkpoint).second) menu.credits_checkpoint=checkpoint;
            }
            if(!intro.Playing() && !menu.pending_dialog) {
                std::optional<std::uint32_t> due;
                for(const auto& target:menu.targets) {
                    const auto found=menu.entries.find(target.id);if(found==menu.entries.end()) continue;
                    const auto id=target.id;auto& entry=found->second;
                    const auto elapsed=entry->state.Elapsed(menu.preview_milliseconds);
                    const bool pushed=entry->state.Tick(menu.preview_milliseconds);
                    entry->visual=renegade::presentation::MenuEntryAppearance(entry->state.phase.Get(),
                        entry->state.Elapsed(menu.preview_milliseconds),pushed);
                    if(pushed) {
                        std::printf("menu entry %u command after %llu ms\n",id,static_cast<unsigned long long>(elapsed));
                        due=id;break;
                    }
                }
                if(due) activate(*due);
            }
            if(!intro.Playing()) {
                if(!menu_started && (!menu.level_view || menu.pause->active.Get())) { engine::audio::VoiceStart voice;voice.buffer=music_pcm;voice.bus=engine::audio::Bus::Music;voice.loop=true;
                    music_voice=mixer.Play(std::move(voice));play_sound(menu.transition.Sound(),0.8f);menu_started=true; }
                menu.seconds+=elapsed;menu.transition.Advance(elapsed,menu.title.rate);
                if(menu.level_view) {
                    menu.level_view->Poll();
                    if(menu.level_view->Failed()) {
                        const auto error=menu.level_view->Error();std::fprintf(stderr,"level preview: %s\n",error.c_str());
                        if(validate_level_preview) {result=1;break;}
                        leave_level();const auto message=engine::gui::w3d::DecodeEditText(error);
                        profile_popup(210,u"Unable to load mission",message ? *message : u"Mission content error");
                    }
                    if(menu.level_view && menu.level_view->Ready()) {
                        if(!player_input) {
                            player_input=std::make_unique<renegade::presentation::PlayerInput>(menu.controls_settings->Configuration());
                            const auto& simulation=menu.level_view->Simulation();const auto player=simulation.World().Resource<renegade::SceneState>().player;
                            player_input->Reset(simulation.World().Get<engine::gameplay::Transform>(player)->facing);
                        }
                        player_input->SetControlAllowed(menu.level_view->InputAllowed());
                        menu.level_view->Advance(frame_elapsed,renegade::presentation::LevelInput{
                            [&] {player_input->SetControlAllowed(menu.level_view->InputAllowed());return player_input->Sample();},[&] {return player_input->Orientation();},
                            [&](Engine::Math::LookAngles angles) {player_input->SetOrientation(angles);},[&] {return player_input->SampleWeapon();}});
                        ++menu.level_ready_frames;
                    }
                    else if(validate_level_preview && menu.level_view && !menu.loading_captured && menu.level_view->LoadingScreenReady() && menu.loading_frames>=2) menu.capture_loading=true;
                    if(validate_level_preview && !validate_level_cancel && !menu.level_view->Ready() && menu.loading_captured &&
                        !menu.loading_progress_captured && menu.level_view->Loading().progress.Get()>=70) menu.capture_loading_progress=true;
                }
                if(menu.pending_dialog && menu.transition.Done()) { menu.displayed_dialog=*menu.pending_dialog;menu.pending_dialog.reset(); }
                if(validate_intro_skip && !skip_validation_complete && menu.transition.Done()) {
                    skip_validation_complete=true;frames=menu.frames+3;
                    std::printf("SDL3 intro skip playback passed: accepted=%u blocked=%u menu=%u\n",
                        accepted_skips,blocked_skips,menu.displayed_dialog);
                }
            }
            menu.capture_next=(!capture.empty() || validate_menu) && ((menu.frames+1==frames && (!validate_level_preview || level_return_posted)) ||
				(validate_level_preview && pause_probe==2 && !menu.captured) ||
				(validate_mission_cinematic && cinematic_capture_pending && !menu.captured) ||
                (validate_tutorial_intro && tutorial_complete && !menu.captured && menu.level_view && !menu.pause->active.Get()) ||
                (validate_tutorial_course && course_help_captured && !menu.captured && menu.level_view && !menu.pause->active.Get()) ||
				(validate_level_preview && !validate_mission_cinematic && !level_return_posted && menu.level_view && menu.level_view->Ready() && (menu.level_ready_frames==3 || validate_player_motion && (motion_complete || motion_phase==5 || walk_capture_pending) && !menu.captured)));
            if(!menu.movie_visible && !menu.pending_dialog && ActiveScrollLayout(menu))
                if(auto* list=ActiveList(menu)) {
                    list->scrollbar.AdvanceRender();
                    if(validate_scrollbars && scroll_phase==2 && list->scrollbar.Captured()) {
                        ++scroll_hold_renders;
                        if(scroll_hold_renders>3 || list->scroll.Get()!=int(scroll_hold_renders) || list->selected.Get()!=0) {
                            std::fprintf(stderr,"SDL3 scrollbar render-repeat count failed\n");result=1;break;
                        }
                    }
                }
            if(!validate_level_preview || validate_frame_timing) {
                const bool desired=menu.level_view && menu.level_view->Ready() && player_input && window->has_focus() && !menu.movie_visible && !menu.pause->active.Get();
                if(platform.input().relative_mouse_mode(window->id())!=desired) {
                    if(platform.input().set_relative_mouse_mode(window->id(),desired)) {
                        platform.input().show_cursor(!desired);
                        std::printf("SDL3 relative pointer ownership: window=%u active=%u\n",window->id(),unsigned(desired));
                    }
                }
            }
            Graphics::Get_Renderer2D().Begin(float(drawable.width),float(drawable.height));
            if(menu.movie_visible) if(const auto* picture=movie.Current_Frame()) {
                menu.movie_frame=*picture;menu.movie_name=menu.gallery.playing.Get() ? "gallery" : std::filesystem::path(intro.Movie()).stem().string();
                menu.expected_movie_frames[menu.movie_name]=movie.Info().frame_count;
                if(movie.Is_Frame_Ready()) movie.Clear_Frame_Ready();
                if((validate_menu || validate_gallery) && picture->presentation_time_us>=1000000*(validate_gallery ? 1 : 3) &&
                    !menu.captured_movies.contains(menu.movie_name) && MovieImageVisible(*picture))
                    menu.capture_movie=menu.movie_name;
            }
            if(!Graphics::Graphics_Begin_Frame() || !Graphics::Graphics_Execute_Queued_Draws() ||
                !Graphics::Graphics_End_Frame() || !Graphics::Graphics_Present()) { std::fprintf(stderr,"frame failed at %u\n",menu.frames);result=1;break; }
            if(validate_frame_timing && menu.level_view && menu.level_view->Ready() && !frame_timing_complete && menu.captured) {
                const auto end=std::chrono::steady_clock::now();
                if(!frame_timing_started && menu.level_view->Simulation().Tick()>=61) {
                    frame_timing_started=true;frame_timing_start=end;frame_timing_tick=menu.level_view->Simulation().Tick();
                } else if(frame_timing_started) {
                    frame_durations.push_back(std::chrono::duration<double,std::milli>(end-frame_start).count());
                    timing_drawn+=menu.level_view->ObjectsDrawn();timing_culled+=menu.level_view->ObjectsCulled();
                    const auto seconds=std::chrono::duration<double>(end-frame_timing_start).count();
                    if(seconds>=8) {
                        const auto ticks=menu.level_view->Simulation().Tick()-frame_timing_tick;
                        std::ranges::sort(frame_durations);
                        const bool captured=platform.input().relative_mouse_mode(window->id());
                        std::printf("SDL3 retail frame timing: frames=%zu seconds=%.6f render_fps=%.3f fixed_ticks=%llu logic_hz=%.3f median_ms=%.3f p99_ms=%.3f drawn=%.1f culled=%.1f pending_ticks=%llu relative_capture=%u focused=%u\n",
                            frame_durations.size(),seconds,frame_durations.size()/seconds,static_cast<unsigned long long>(ticks),ticks/seconds,
                            frame_durations[frame_durations.size()/2],frame_durations[(frame_durations.size()-1)*99/100],
                            double(timing_drawn)/frame_durations.size(),double(timing_culled)/frame_durations.size(),
                            static_cast<unsigned long long>(menu.level_view->PendingTicks()),unsigned(captured),unsigned(window->has_focus()));
                        if(std::abs(double(ticks)-seconds*renegade::Session::TicksPerSecond)>3 || menu.level_view->PendingTicks() ||
                            !timing_culled || window->has_focus() && !captured) {
                            std::fprintf(stderr,"independent render/fixed simulation timing or SDL3 pointer ownership failed\n");result=1;break;
                        }
                        frame_timing_complete=true;
                    }
                }
            }
            // Only finite automated playback requests a paced frame clock.
            // Interactive rendering runs independently of the fixed simulation.
            if(realtime && frames && !interactive_clock) std::this_thread::sleep_until(frame_start+std::chrono::nanoseconds{16'666'667});
        }
		if(validate_level_preview) {
            if(validate_mission_cinematic && !cinematic_complete) {std::fprintf(stderr,"retail cinematic playback validation incomplete\n");result=1;}
            if(validate_tutorial_intro && !tutorial_complete) {std::fprintf(stderr,"retail tutorial speech validation incomplete\n");result=1;}
            if(validate_tutorial_course && !course_complete) {std::fprintf(stderr,"retail tutorial course validation incomplete\n");result=1;}
            if(validate_frame_timing && !frame_timing_complete) {std::fprintf(stderr,"retail frame timing validation incomplete\n");result=1;}
            if(validate_player_motion && !motion_complete) {std::fprintf(stderr,"retail player motion validation incomplete\n");result=1;}
            if(!validate_single_player_menu.empty()) {
                if(single_player_menu_step!=(validate_single_player_menu=="tutorial" ? 2u : 3u)) {
                    std::fprintf(stderr,"single-player menu launch route incomplete\n");result=1;
                } else if(!result && level_return_posted && !menu.level_view && menu.captured && menu.displayed_dialog==128)
                    std::printf("SDL3 single-player menu launch passed: %s, retail buttons, level rendering and main menu return\n",validate_single_player_menu.c_str());
            }
			if(!level_return_posted || menu.level_view || !menu.captured || menu.displayed_dialog!=128 || !menu.transition.Done() ||
                menu.loading_frames<5 || !menu.loading_events || !menu.loading_captured || (!validate_level_cancel && !menu.loading_progress_captured)) {
				std::fprintf(stderr,"level preview return validation incomplete\n");result=1;
			} else {
                std::printf("SDL3 asynchronous loading: %u rendered loading frames, %u input events serviced, maximum frame gap %.3f ms\n",menu.loading_frames,menu.loading_events,maximum_loading_gap_ms);
                if(validate_level_cancel) {
                    if(menu.level_ready_frames) {std::fprintf(stderr,"loading cancellation arrived after scene publication\n");result=1;}
                    else std::printf("SDL3 level loading cancellation passed: menu returned before scene publication\n");
                } else std::printf("SDL3 level preview passed: rendered scene, Escape teardown, GPU menu labels and return\n");
            }
		}
        if(validate_menu && !playback_complete) { std::fprintf(stderr,"menu playback incomplete at step %zu of %zu (dialog %u)\n",playback_step,playback.size(),menu.displayed_dialog);result=1; }
        if(validate_gallery) {
            const auto presented=menu.presented_movie_frames["gallery"].size(),expected=menu.expected_movie_frames["gallery"];
            if(!playback_complete || gallery_starts!=3 || gallery_natural!=1 || gallery_escapes!=2 || gallery_restorations!=3 ||
                !menu.gallery_captured || !menu.captured_movies.contains("gallery") || !expected || presented!=expected) {
                std::fprintf(stderr,"gallery validation incomplete: step %zu/%zu, starts %u, natural %u, Escape %u, restorations %u, frames %zu/%llu\n",
                    playback_step,playback.size(),gallery_starts,gallery_natural,gallery_escapes,gallery_restorations,presented,
                    static_cast<unsigned long long>(expected));result=1;
            } else std::printf("SDL3 movie gallery passed: %u starts, %u natural completion, %u Escape stops, %u audio restorations, %zu/%llu decoded frames presented and return to menu\n",
                gallery_starts,gallery_natural,gallery_escapes,gallery_restorations,presented,static_cast<unsigned long long>(expected));
        }
        if(validate_scrollbars && result==0) {
            if(!playback_complete || !scroll_validation_complete || scroll_checks!=14 || scroll_hold_renders!=3 || menu.displayed_dialog!=128) {
                std::fprintf(stderr,"SDL3 scrollbar validation incomplete: phase=%u checks=%u holds=%u\n",scroll_phase,scroll_checks,scroll_hold_renders);result=1;
            } else std::printf("SDL3 scrollbar validation passed: %u state checks, %u rendered hold steps, resize, source paging and return to menu\n",scroll_checks,scroll_hold_renders);
        }
        if(validate_multiplayer_options && result==0) {
            if(!playback_complete || multiplayer_input_checks!=13 || menu.multiplayer_options->Applied() || menu.displayed_dialog!=128) {
                std::fprintf(stderr,"SDL3 multiplayer-options validation incomplete: %u checks\n",multiplayer_input_checks);result=1;
            } else std::printf("SDL3 multiplayer options passed: 13 state checks, keyboard/pointer, unavailable service controls, Back/Escape apply and menu return\n");
        }
        if(validate_profiles && result==0) {
            const auto& catalog=menu.profile_store->Catalog();
            if(!playback_complete || profile_input_checks<20 || catalog.profiles.size()!=2 || catalog.current!="input01.cfg" ||
                catalog.profiles[1].name!=u"Renamed" || menu.controls_settings->Configuration().Get("MoveForward").primary!=unsigned(KeyCode::f12) ||
                menu.controls_settings->sensitivity.position.Get()!=50 || menu.control_profiles->Pending()) {
                std::fprintf(stderr,"SDL3 profile validation incomplete: %u state checks, current=%s\n",profile_input_checks,catalog.current.c_str());result=1;
            } else std::printf("SDL3 profiles passed: %u state checks, create, empty-name, overwrite/cancel, builtin protection, load, delete/cancel, default fallback and menu return\n",profile_input_checks);
        }
        if(validate_controls && result==0) {
            if(!playback_complete || controls_input_checks!=15 || menu.controls_settings->conflict.Get() ||
                menu.controls_settings->sensitivity.position.Get()!=100 || menu.controls_settings->Configuration().Get("MoveForward").primary!=unsigned(KeyCode::w) ||
                menu.controls_settings->Configuration().Get("MapScreen").primary!=unsigned(KeyCode::f11)) {
                std::fprintf(stderr,"controls runtime playback incomplete: %u checks\n",controls_input_checks);result=1;
            } else std::printf("SDL3 controls passed: five tabs, keyboard/remap/cancel/confirm/delete/reserved modifiers, source pointer delay, defaults, look values, apply/return\n");
        }
        if(validate_performance) {
            const renegade::presentation::PerformanceApplication expected{2,1,2,0,5000,5000,false,false};
            if(!playback_complete || performance_input_checks<26 || menu.performance_frame_checks!=performance_input_checks ||
                menu.performance_texture_replacements!=1 || menu.performance_settings->Applied()!=expected) {
                std::fprintf(stderr,"performance validation incomplete: %u inputs, %u GPU checks, %u texture replacements\n",
                    performance_input_checks,menu.performance_frame_checks,menu.performance_texture_replacements);result=1;
            } else std::printf("SDL3 performance controls passed: source presets and expert edits, visibility, deferred application, texture replacement, reopen and return to menu\n");
        }
        if(validate_video) {
            if(!playback_complete || menu.video_frame_checks!=12 || !resize_completed.contains("video-calibration") ||
                menu.video_settings->Levels()!=renegade::presentation::VideoLevels{101,-12,123}) {
                std::fprintf(stderr,"video controls validation incomplete: step %zu/%zu, %u GPU checks\n",playback_step,playback.size(),menu.video_frame_checks);result=1;
            } else std::printf("SDL3 video controls passed: source endpoints, key repeats, pointer values, live calibrated GPU output, resize and return to menu\n");
        }
        if(validate_menu && !menu.credits_captured) {std::fprintf(stderr,"credits GPU image validation incomplete\n");result=1;}
        if(validate_credits) {
            const auto total=menu.credits_layout ? menu.credits_layout->rows.size() : 0;
            const auto text_rows=menu.credits_layout ? std::ranges::count_if(menu.credits_layout->rows,[](const auto& row){return !row.placements.empty();}) : 0;
            if(!playback_complete || !menu.credits_captured || !total || menu.credits_seen_rows.size()!=total ||
                menu.credits_verified_rows.size()!=static_cast<std::size_t>(text_rows) || menu.credits_scroll.Cycles()!=1 || credits_checkpoints.size()!=3) {
                std::fprintf(stderr,"credits cycle incomplete: %zu/%zu visible rows, %zu/%lld GPU text rows, %llu cycles\n",
                    menu.credits_seen_rows.size(),total,menu.credits_verified_rows.size(),static_cast<long long>(text_rows),
                    static_cast<unsigned long long>(menu.credits_scroll.Cycles()));result=1;
            } else std::printf("SDL3 credits full cycle passed: %zu/%zu visible rows, %zu/%lld GPU text rows, one restart and return to menu; deterministic quarter-second steps\n",
                menu.credits_seen_rows.size(),total,menu.credits_verified_rows.size(),static_cast<long long>(text_rows));
        }
        if(validate_quit) {
            if(quit_step!=2 || !menu.navigation.quitting.Get()) {std::fprintf(stderr,"SDL3 quit confirmation incomplete\n");result=1;}
            else std::printf("SDL3 quit confirmation passed: Escape, default Yes, Enter, process shutdown\n");
        }
        if(validate_menu) {
            for(const auto* label:{"menu-audio","menu-restore"}) if(!resize_completed.contains(label)) {
                std::fprintf(stderr,"menu resize validation incomplete: %s\n",label);result=1;
            }
            if(!skip_intro) for(const auto* label:{"movie-EA_WW","movie-R_Intro","movie-restore"}) if(!resize_completed.contains(label)) {
                std::fprintf(stderr,"movie resize validation incomplete: %s\n",label);result=1;
            }
        }
        if(validate_menu && !skip_intro) {
            if(menu.captured_movies.size()!=2) {std::fprintf(stderr,"intro GPU image validation incomplete\n");result=1;}
            for(const auto& [name,expected]:menu.expected_movie_frames) {
                const auto presented=menu.presented_movie_frames[name].size();
                std::printf("movie %s: %zu of %llu decoded frames presented\n",name.c_str(),presented,static_cast<unsigned long long>(expected));
                if(presented!=expected) result=1;
            }
        }
        if(!capture.empty() && !menu.captured) result=1;
        if(validate_intro_skip && !skip_validation_complete) {std::fprintf(stderr,"intro skip playback incomplete\n");result=1;}
        apply_previews(menu.audio_preview.Close());
        if(text_input_active) platform.text_input().stop(window->id());
        save_settings();
        std::printf("rendered %u frames; capture %s\n",menu.frames,menu.captured ? "saved" : "none");
        if(menu.level_view) menu.level_view->Cancel();
        Graphics::Get_Resource_Load_Queue().Drain();
        Graphics::Detach_Frame_Draw_Executor();scene=nullptr;
    }
    movie.Close();mixer.Stop(music_voice);audio.reset();
    Graphics::Get_Render_Services().Shutdown();
    Assets::Shutdown_Asset_Runtime();Graphics::Shutdown_Scene_Renderers();Graphics::Graphics_Shutdown_Shared_Frame();
    return result;
}
