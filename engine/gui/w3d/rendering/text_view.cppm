export module engine.gui.w3d.text_view;
import std;
import engine.gui.text.font_face;
export import engine.gui.text.renderer;
import Graphics.Renderer2D;
import engine.gui.w3d.menu_entry;
import engine.gui.w3d.dialog_input;

export namespace engine::gui::w3d
{
enum class TextAlignment { Left,Center,Right };
struct TextBoxOptions {
    TextAlignment alignment{TextAlignment::Left};
    bool wrap{},clip{},vertical_center{};
    float left_inset{};
};
// W3D controls share the engine's font atlases with WND controls. The caller
// supplies text, color and position; content and navigation remain in its VM.
bool Draw_Text(const text::FontFace& font,Graphics::Renderer2D& renderer,
    std::u16string_view value,float x,float y,Graphics::Color2D color,
    Graphics::Renderer2DBlendMode blend=Graphics::Renderer2DBlendMode::Alpha) {
    if(value.empty()) return true;
    std::vector<std::uint16_t> characters(value.begin(),value.end());characters.push_back(0);
    text::TextRenderer view(value.size()+1,value.size()+1);
    text::TextStyle style;style.color=color;style.drop_color={0,0,0,0};style.x_drop=style.y_drop=0;style.blend=blend;
    return view.Draw(renderer,font,nullptr,characters.data(),x,y,{},style);
}

// Offset effects reuse the shared glyph batching and Renderer2D blend modes.
// The caller owns its effect geometry, tint and animation policy.
std::vector<Graphics::Point2D> Radial_Text_Offsets(unsigned rings,unsigned samples,
    Graphics::Point2D initial_radius,Graphics::Point2D increment) {
    if(!std::isfinite(initial_radius.x) || !std::isfinite(initial_radius.y) ||
        !std::isfinite(increment.x) || !std::isfinite(increment.y) ||
        std::uint64_t(rings)*samples>65536) throw std::invalid_argument("invalid radial text effect");
    std::vector<Graphics::Point2D> offsets;
    if(!rings || !samples) return offsets;
    offsets.reserve(std::size_t(rings)*samples);
    const float angle_step=2*std::numbers::pi_v<float>/samples;
    auto radius=initial_radius;
    for(unsigned ring=0;ring<rings;++ring) {
        float angle=0;
        for(unsigned sample=0;sample<samples;++sample) {
            offsets.push_back({std::cos(angle)*radius.x,std::sin(angle)*radius.y});angle+=angle_step;
        }
        radius.x+=increment.x;radius.y+=increment.y;
    }
    return offsets;
}
bool Draw_Text_Offsets(const text::FontFace& font,Graphics::Renderer2D& renderer,
    std::u16string_view value,float x,float y,Graphics::Color2D color,
    std::span<const Graphics::Point2D> offsets,Graphics::Renderer2DBlendMode blend) {
    if(!std::isfinite(x) || !std::isfinite(y) ||
        !std::ranges::all_of(offsets,[](const auto& offset) {return std::isfinite(offset.x) && std::isfinite(offset.y);})) return false;
    if(value.empty() || offsets.empty()) return true;
    std::vector<std::uint16_t> characters(value.begin(),value.end());characters.push_back(0);
    text::TextRenderer view(value.size()+1,value.size()+1);
    text::TextStyle style;style.color=color;style.drop_color={0,0,0,0};style.x_drop=style.y_drop=0;style.blend=blend;
    for(const auto& offset:offsets)
        if(!view.Draw(renderer,font,nullptr,characters.data(),x+offset.x,y+offset.y,{},style)) return false;
    return true;
}

std::optional<std::array<std::uint32_t,2>> Measure_Text(const text::FontFace& font,std::u16string_view value) {
    std::vector<std::uint16_t> characters(value.begin(),value.end());characters.push_back(0);
    text::TextRenderer measure(value.size()+1,value.size()+1);
    std::array<std::uint32_t,2> size;
    if(!measure.Measure(font,characters.data(),{},size[0],size[1])) return {};
    return size;
}
// menuentryctrl.cpp On_Create sizes the clickable rectangle to the caption
// plus one W advance. Animated controls subsequently anchor that sized rect.
HitRect Menu_Entry_Bounds(const text::FontFace& font,std::u16string_view value,
    HitRect authored,std::uint32_t style,bool positioned_by_animation=false) {
    const auto size=Measure_Text(font,value),margin=Measure_Text(font,u"W");
    if(size && margin) {
        if(!positioned_by_animation && (style&0xf00)!=0x100)
            authored.left=std::trunc(authored.left+(authored.right-authored.left-(*size)[0])*0.5f);
        authored.right=authored.left+(*size)[0]+(*margin)[0];
    }
    return authored;
}
bool Draw_Menu_Entry(const text::FontFace& font,Graphics::Renderer2D& renderer,std::u16string_view value,
    HitRect bounds,std::uint32_t style,MenuEntryPhase phase,Graphics::Color2D text_color,
    Graphics::Color2D embossed_color,Graphics::Color2D glow_color,std::span<const Graphics::Point2D> offsets) {
    const auto size=Measure_Text(font,value);if(!size) return false;
    const float x=std::trunc((style&0xf00)==0x100 ? bounds.left+1 : bounds.left+(bounds.right-bounds.left-(*size)[0])*0.5f);
    const float y=std::trunc(bounds.top+(bounds.bottom-bounds.top-(*size)[1])*0.5f);
    if(!Draw_Text_Offsets(font,renderer,value,x,y,glow_color,offsets,Graphics::Renderer2DBlendMode::Additive)) return false;
    if(phase==MenuEntryPhase::Idle) return Draw_Text(font,renderer,value,x,y,text_color);
    if(phase==MenuEntryPhase::Focused) {
        if(!Draw_Text(font,renderer,value,x-1,y-1,text_color)) return false;
    } else if(!Draw_Text(font,renderer,value,x+1,y+1,embossed_color)) return false;
    return Draw_Text(font,renderer,value,x,y,embossed_color);
}

// Uses the same word wrapping and glyph batching as WND. The view supplies
// alignment and styling; authored control flags and game colors stay outside.
bool Draw_Text_Box(const text::FontFace& font,Graphics::Renderer2D& renderer,
    std::u16string_view value,Graphics::Rect2D bounds,text::TextStyle style,TextBoxOptions options={}) {
    if(!std::isfinite(bounds.left) || !std::isfinite(bounds.top) || !std::isfinite(bounds.right) ||
        !std::isfinite(bounds.bottom) || !std::isfinite(options.left_inset) || bounds.right<bounds.left ||
        bounds.bottom<bounds.top || double(bounds.right)-bounds.left>std::numeric_limits<int>::max()) return false;
    if(value.empty()) return true;
    std::vector<std::uint16_t> characters(value.begin(),value.end());characters.push_back(0);
    text::TextRenderer view(value.size()+1,value.size()+1);
    text::TextLayoutOptions layout;
    layout.wrapping_width=options.wrap ? static_cast<int>(bounds.right-bounds.left) : 0;
    layout.centered=options.alignment==TextAlignment::Center;
    layout.right_aligned=options.alignment==TextAlignment::Right;
    layout.snap_to_pixels=true;
    std::uint32_t width{},height{};
    if(!view.Measure(font,characters.data(),layout,width,height)) return false;
    float x=bounds.left+options.left_inset,y=bounds.top;
    if(options.alignment==TextAlignment::Center) x=bounds.left+(bounds.right-bounds.left-width)*0.5f;
    if(options.alignment==TextAlignment::Right) x=bounds.right-width;
    if(options.vertical_center) y=bounds.top+(bounds.bottom-bounds.top-height)*0.5f;
    // Authored W3D text anchors truncate toward zero, including fractional
    // centered dialog positions. Keep that pixel alignment at the view boundary.
    const auto previous=renderer.Get_Clip();
    struct RestoreClip {
        Graphics::Renderer2D& renderer;Graphics::Renderer2D::ClipState previous;
        ~RestoreClip() {renderer.Set_Clip(previous.enabled,previous.rectangle);}
    } restore{renderer,previous};
    if(options.clip) {
        if(previous.enabled) bounds={std::max(bounds.left,previous.rectangle.left),std::max(bounds.top,previous.rectangle.top),
            std::min(bounds.right,previous.rectangle.right),std::min(bounds.bottom,previous.rectangle.bottom)};
        if(bounds.right<=bounds.left || bounds.bottom<=bounds.top) return true;
        renderer.Set_Clip(true,bounds);
    }
    return view.Draw(renderer,font,nullptr,characters.data(),x,y,layout,style);
}
}
