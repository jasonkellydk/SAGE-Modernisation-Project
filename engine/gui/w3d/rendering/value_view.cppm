export module engine.gui.w3d.value_view;
import std;
import engine.gui.w3d.value_controls;
import engine.gui.w3d.text_view;
import engine.gui.text.font_face;
import Graphics.Renderer2D;

namespace {
bool ValidBounds(engine::gui::w3d::HitRect bounds) {
    constexpr float limit=std::numeric_limits<int>::max()/4.0f;
    return std::isfinite(bounds.left) && std::isfinite(bounds.top) && std::isfinite(bounds.right) &&
        std::isfinite(bounds.bottom) && std::abs(bounds.left)<limit && std::abs(bounds.top)<limit &&
        std::abs(bounds.right)<limit && std::abs(bounds.bottom)<limit &&
        bounds.right>bounds.left && bounds.bottom>bounds.top;
}
}
export namespace engine::gui::w3d {
bool Draw_Filled_Outline(Graphics::Renderer2D& renderer,HitRect bounds,
    Graphics::Color2D line,Graphics::Color2D background) {
    if(!ValidBounds(bounds)) return false;
    // Source Render2D Add_Rect outlines first, then fills one pixel short of
    // the right/bottom edges. The palette belongs to the caller.
    return renderer.Add_Outline({bounds.left,bounds.top,bounds.right,bounds.bottom},1,line) &&
        renderer.Add_Rect({bounds.left,bounds.top,bounds.right-1,bounds.bottom-1},background);
}
struct ValuePalette {Graphics::Color2D line,background,text,shadow,highlight;};
struct SliderShape {HitRect thumb,before,after;};
struct CheckBoxShape {HitRect button,text,hit;};
std::optional<SliderShape> Layout_Slider(const SliderValueModel& model,HitRect bounds,float bar_height) {
    if(!ValidBounds(bounds) || !std::isfinite(bar_height) || bar_height<0 || bar_height>bounds.bottom-bounds.top ||
        bounds.bottom-bounds.top<4) return {};
    const auto range=std::int64_t(model.Maximum())-model.Minimum();
    const float fraction=range ? float(std::int64_t(model.position.Get())-model.Minimum())/range : 0;
    const float height=bounds.bottom-bounds.top,width=bounds.right-bounds.left;
    const int thumb_height=static_cast<int>(height-2),thumb_width=thumb_height/2;
    const float left=std::trunc(bounds.left+width*fraction-thumb_width*0.5f);
    const float top=std::trunc(bounds.top+height/2-thumb_height/2),bottom=std::trunc(bounds.top+height/2+thumb_height/2);
    const float bar_top=std::trunc(bounds.top+height/2-bar_height/2),bar_bottom=std::trunc(bounds.top+height/2+bar_height/2);
    return SliderShape{{left,top,left+thumb_width,bottom},
        {bounds.left,bar_top,left,bar_bottom},{left+thumb_width,bar_top,bounds.right,bar_bottom}};
}
bool DrawSlider(const SliderValueModel& model,Graphics::Renderer2D& renderer,HitRect bounds,
    const ValuePalette& palette,bool focused,float bar_height) {
    const auto shape=Layout_Slider(model,bounds,bar_height);if(!shape) return false;
    if(!Draw_Filled_Outline(renderer,shape->thumb,palette.line,focused ? palette.line : palette.background)) return false;
    for(const auto bar:{shape->before,shape->after}) if(bar.right>bar.left)
        if(!Draw_Filled_Outline(renderer,bar,palette.line,palette.background)) return false;
    return true;
}
// CheckboxCtrl sizes its square from one W advance, clips the caption and
// narrows the hit rectangle to the measured caption plus scaled padding.
std::optional<CheckBoxShape> Layout_Check_Box(const text::FontFace& font,std::u16string_view label,
    HitRect bounds,float x_scale) {
    if(!ValidBounds(bounds) || !std::isfinite(x_scale) || x_scale<=0) return {};
    const auto margin=Measure_Text(font,u"W"),caption=Measure_Text(font,label);
    if(!margin || !caption) return {};
    const float size=std::trunc((*margin)[0]*1.5f),height=bounds.bottom-bounds.top;
    const HitRect button{bounds.left,bounds.top+std::trunc(height/2-size/2),
        bounds.left+size,bounds.top+std::trunc(height/2+size/2)};
    const float text_left=std::trunc(button.right+(*margin)[0]*0.5f);
    const float text_right=std::min(bounds.right,text_left+(*caption)[0]+8*x_scale);
    return CheckBoxShape{button,{text_left,bounds.top,text_right,bounds.bottom},
        {bounds.left,bounds.top,text_right,bounds.bottom}};
}
bool DrawCheckBox(const CheckValueModel& model,const text::FontFace& font,Graphics::Renderer2D& renderer,
    std::u16string_view label,HitRect bounds,const ValuePalette& palette,bool focused=false,float x_scale=1) {
    const auto shape=Layout_Check_Box(font,label,bounds,x_scale);if(!shape) return false;
    if(!Draw_Filled_Outline(renderer,shape->button,palette.line,palette.background)) return false;
    if(model.checked.Get()) {
        const auto box=shape->button;
        const float left=box.left+5,right=box.right-5,top=box.top+5,bottom=box.bottom-5;
        if(!renderer.Add_Line({left-1,top+1},{right-2,bottom+1},2,palette.shadow) ||
            !renderer.Add_Line({left-1,bottom+1},{right-2,top+1},2,palette.shadow) ||
            !renderer.Add_Line({left,top},{right-1,bottom},2,palette.text) ||
            !renderer.Add_Line({left,bottom},{right-1,top},2,palette.text)) return false;
    }
    if(shape->text.right>shape->text.left) {
        text::TextStyle style;style.color=palette.text;style.drop_color=palette.shadow;style.x_drop=-1;style.y_drop=1;
        TextBoxOptions options;options.clip=true;options.vertical_center=true;options.left_inset=1;
        if(!Draw_Text_Box(font,renderer,label,{shape->text.left,shape->text.top,shape->text.right,shape->text.bottom},style,options)) return false;
        if(focused && !renderer.Add_Rect({shape->text.left,shape->text.top,shape->text.right,shape->text.bottom},
            palette.highlight,Graphics::Renderer2DBlendMode::Additive)) return false;
    }
    return true;
}
}
