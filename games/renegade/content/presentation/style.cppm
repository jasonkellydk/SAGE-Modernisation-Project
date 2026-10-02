export module games.renegade.content.presentation.style;
import std;
import engine.config.adapters.ini.section_reader;

export namespace renegade::content {
enum class FontRole : unsigned { Title,LargeControls,Controls,Lists,Tooltips,Menu,SmallMenu,Header,BigHeader,
    Credits,CreditsBold,InGame,InGameBig,Subtitle,InGameHeader,Count };
struct FontSpec { std::string family;unsigned points{};bool bold{}; };
struct InterfaceSound { std::string filename;unsigned volume{}; };
struct ControlText { std::u16string text;FontRole font{};bool title{}; };
struct ControlPalette {std::uint32_t line,background,text,shadow,highlight;};
struct ControlStyle {
    // wwui/stylemgr.cpp's palette remains game content policy.
    static constexpr ControlPalette enabled{0xffffae28u,0x28ffae28u,0xffffd528u,0xc8000000u,0xff464646u};
    static constexpr ControlPalette disabled{0x80e6a023u,0x1effae28u,0x8cffd528u,0x60000000u,0xff464646u};
    static constexpr float slider_bar_height=8;
};
struct MenuBackdropStyle {
    // wwui/menubackdrop.cpp uses a fixed horizontal angle and multiplies the
    // angle itself by H/W for the vertical angle, before taking its tangent.
    static std::optional<std::array<float,2>> ViewAngles(unsigned drawable_width,unsigned drawable_height) {
        if(!drawable_width || !drawable_height) return {};
        const float horizontal=std::numbers::pi_v<float>/4;
        return std::array<float,2>{horizontal,(static_cast<float>(drawable_height)/drawable_width)*horizontal};
    }
};
struct MainMenuEntryStyle {
    // dialogbase.cpp truncates resource-scaled control dimensions to pixels.
    // mainmenutransition.cpp then positions the sized control at IF_MMTF1..6.
    // Coordinates outside the viewport are valid during the authored motion.
    static std::optional<std::array<float,4>> ProjectedBounds(std::array<float,2> projected,
        float source_width,float source_height,unsigned drawable_width,unsigned drawable_height) {
        if(!drawable_width || !drawable_height || !std::isfinite(projected[0]) || !std::isfinite(projected[1]) ||
            !std::isfinite(source_width) || !std::isfinite(source_height) || source_width<0 || source_height<0) return {};
        const float width=std::trunc((source_width/400.f)*drawable_width);
        const float height=std::trunc((source_height/300.f)*drawable_height);
        const float left=drawable_width*(projected[0]+1)*0.5f-(drawable_width/800.f)*100;
        const float top=drawable_height*(1-projected[1])*0.5f-height*0.5f;
        const std::array<float,4> bounds{left,top,left+width,top+height};
        if(!std::ranges::all_of(bounds,[](float value){return std::isfinite(value);})) return {};
        return bounds;
    }
};
struct ButtonStyle {
    // wwui/buttonctrl.cpp Create_Component_Button and Create_Text_Renderers.
    static constexpr std::string_view texture="if_circle02.tga";
    static constexpr std::array<float,2> texture_size{256,256},corner_size{10,10},tile_size{10,10};
    static constexpr std::array<float,4> source_pixels{96,51,217,84},pressed_pixels{117,4,220,41};
    static constexpr std::array<float,2> pressed_inset{6,6},pressed_far_edge{1,1};
    static constexpr std::uint32_t glow=0xff100a00u;
    static constexpr int glow_radius=8;
};
struct PopupStyle {
    // wwui/popupdialog.cpp title padding and full-screen blackout alpha.
    static constexpr std::array<float,2> title_padding{10,8};
    static constexpr std::uint32_t blackout=0xec000000u;
    static constexpr bool darken_background=true;
};
struct ScrollStyle {
    // wwui/scrollbarctrl.cpp's atlas policy stays in Renegade content.
    static constexpr std::string_view texture="if_menuparts9.tga";
    static constexpr float width=10,button_offset=10,uv_divisor=255,gradient_edge_alpha=10.f/255;
    static constexpr std::array<float,4> previous_up{100,212,121,234},previous_down{76,212,98,234};
    static constexpr std::array<float,4> next_up{52,212,74,234},next_down{28,212,50,234};
    static constexpr std::array<float,4> thumb_up{233,70,254,136},thumb_down{233,138,254,204};
    static constexpr std::array<float,4> small_previous_up{147,228,169,241},small_previous_down{147,213,169,226};
    static constexpr std::array<float,4> small_next_up{123,228,145,241},small_next_down{123,213,145,226};
    static constexpr std::array<float,4> small_thumb_up{171,224,193,233},small_thumb_down{171,213,193,222};
};
struct CreditsStyle {
    // TextMarqueeCtrl's authored client inset and two rows per second.
    static constexpr std::array<float,2> padding{5,3.75f};
    static constexpr float rows_per_second=2;
    static constexpr unsigned blank_pages=2;
};
struct ListStyle {
    // wwui/listctrl.cpp Update_Client_Rect and Update_Row_Height.
    static constexpr float vertical_inset=1,header_rows=3,row_spacing=4,pulse_rate=2;
    // ListCtrl::Create_Control_Renderer divides channels by 256, then the
    // legacy color packer multiplies by 255 and truncates rather than rounds.
    static std::uint32_t FocusColor(std::uint32_t color,float pulse) {
        const float percent=std::clamp(pulse,0.f,1.f)*0.5f+0.5f;
        const auto channel=[&](unsigned shift) {return static_cast<std::uint32_t>(((color>>shift)&255)/256.f*percent*255.f);};
        return 0xff000000u | (channel(16)<<16) | (channel(8)<<8) | channel(0);
    }
};
struct MenuGlowStyle {
    static constexpr unsigned rings=4,samples=7;
    static constexpr std::uint32_t title_glow=0xff0e0000u;
    static constexpr std::uint32_t title_text=0xffffff24u;
    static constexpr std::uint32_t text=0xffffd528u,text_shadow=0xc8000000u;
    static constexpr float initial_radius=2;
    // stylemgr.cpp Render_Glow divides integers before assigning float values.
    static std::array<float,2> RadiusStep(int radius_x,int radius_y) {
        return {static_cast<float>(radius_x/static_cast<int>(rings)),
            static_cast<float>(radius_y/static_cast<int>(rings))};
    }
};
// wwui/dialogtext.cpp On_Create strips its title/header/tooltip prefix and
// selects the corresponding authored font. A header remains visible.
ControlText FormatControlText(std::u16string_view descriptor,FontRole fallback) {
    if(descriptor.size()>=2 && descriptor[0]==u'%') {
        switch(descriptor[1]) {
        case u't':return {std::u16string(descriptor.substr(2)),FontRole::Title,true};
        case u'h':return {std::u16string(descriptor.substr(2)),FontRole::LargeControls,false};
        case u's':return {std::u16string(descriptor.substr(2)),FontRole::Tooltips,false};
        }
    }
    return {std::u16string(descriptor),fallback,false};
}
struct MenuStyle {
    std::vector<std::string> font_files;
    std::array<FontSpec,static_cast<unsigned>(FontRole::Count)> fonts;
    std::array<InterfaceSound,4> sounds; // click, mouseover, back, popup
    // wwui/stylemgr.cpp Initialize_From_INI: vertical scaling, minimum size,
    // and removal of bold below ten points when reducing the resolution.
    FontSpec Font(FontRole role,unsigned height) const {
        auto spec=fonts.at(static_cast<unsigned>(role));
        const float scale=height/600.0f;
        const float points=std::max(8.0f,spec.points*scale);
        spec.points=static_cast<unsigned>(points);
        if(points<10 && scale<1) spec.bold=false;
        return spec;
    }
};
std::expected<MenuStyle,std::string> ReadMenuStyle(std::string text) {
    using namespace engine::config;
    auto document=ini::ReadSections("stylemgr.ini",std::move(text));
    if(!document) return std::unexpected(document.error());
    MenuStyle style;
    const auto* files=ini::FindSection(*document,"Font File List");
    const auto* fonts=ini::FindSection(*document,"Font Names");
    const auto* audio=ini::FindSection(*document,"Audio");
    if(!files || !fonts || !audio) return std::unexpected("stylemgr.ini requires font files, names and audio sections");
    for(const auto& file:files->children) {
        if(file.text.empty() || file.text.find_first_of("/\\:")!=std::string_view::npos || file.text=="..")
            return std::unexpected("font file must be a retail or mod basename");
        style.font_files.emplace_back(file.text);
    }
    const auto trim=[](std::string_view value) {
        while(!value.empty() && (value.front()==' ' || value.front()=='\t')) value.remove_prefix(1);
        while(!value.empty() && (value.back()==' ' || value.back()=='\t')) value.remove_suffix(1);
        return value;
    };
    const auto number=[&](std::string_view value)->std::optional<unsigned> {
        value=trim(value);unsigned result{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        return parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() ? std::optional(result) : std::nullopt;
    };
    constexpr std::array keys{"font_title","font_lg_controls","font_controls","font_lists","font_tooltips","font_menu",
        "font_sm_menu","font_header","font_big_header","font_credits","font_credits_bold","font_ingame_txt",
        "font_ingame_big_txt","font_ingame_subtitle_txt","font_ingame_header_txt"};
    for(unsigned i=0;i<keys.size();++i) {
        const auto* item=fonts->Find(keys[i]);
        if(!item) return std::unexpected("missing style font: "+std::string(keys[i]));
        const auto first=item->text.find(',');const auto last=item->text.find(',',first==std::string_view::npos ? first : first+1);
        if(first==std::string_view::npos || last==std::string_view::npos) return std::unexpected("invalid font specification");
        const auto size=number(item->text.substr(first+1,last-first-1)),bold=number(item->text.substr(last+1));
        const auto family=trim(item->text.substr(0,first));
        if(family.empty() || !size || !*size || *size>1024 || !bold) return std::unexpected("invalid font size, family or weight");
        style.fonts[i]={std::string(family),*size,*bold!=0};
    }
    constexpr std::array sounds{"audio_click","audio_mouseover","audio_back","audio_popup"};
    for(unsigned i=0;i<sounds.size();++i) {
        const auto* item=audio->Find(sounds[i]);
        if(!item) return std::unexpected("missing interface audio: "+std::string(sounds[i]));
        const auto comma=item->text.find(',');
        if(comma==std::string_view::npos) return std::unexpected("invalid interface audio specification");
        const auto volume=number(item->text.substr(comma+1));
        const auto filename=trim(item->text.substr(0,comma));
        if(filename.empty() || !volume) return std::unexpected("invalid interface audio filename or volume");
        style.sounds[i]={std::string(filename),std::min(*volume,100u)};
    }
    return style;
}
}
