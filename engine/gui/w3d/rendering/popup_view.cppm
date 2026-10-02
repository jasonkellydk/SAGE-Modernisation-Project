export module engine.gui.w3d.popup_view;
import std;
import engine.gui.images;
import engine.gui.w3d.text_view;
import engine.gui.w3d.value_view;
import engine.gui.w3d.dialog_input;
import engine.gui.text.font_face;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
struct PopupShape {HitRect body;std::optional<HitRect> title;};
std::optional<PopupShape> Layout_Popup(const text::FontFace& font,std::u16string_view title,HitRect bounds,
    Graphics::Point2D title_padding,Graphics::Point2D scale) {
    if(!images::FiniteRectangle({bounds.left,bounds.top,bounds.right,bounds.bottom}) || bounds.right<=bounds.left || bounds.bottom<=bounds.top ||
        !std::isfinite(title_padding.x) || !std::isfinite(title_padding.y) || title_padding.x<0 || title_padding.y<0 ||
        !std::isfinite(scale.x) || !std::isfinite(scale.y) || scale.x<=0 || scale.y<=0) return {};
    PopupShape shape{bounds,{}};
    if(title.empty()) return shape;
    const auto extent=Measure_Text(font,title);if(!extent) return {};
    if((*extent)[0] || (*extent)[1]) {
        const HitRect caption{bounds.left,bounds.top-std::trunc((*extent)[1]+2*title_padding.y*scale.y),
            bounds.left+std::trunc((*extent)[0]+2*title_padding.x*scale.x),bounds.top};
        if(!images::FiniteRectangle({caption.left,caption.top,caption.right,caption.bottom})) return {};
        shape.title=caption;
    }
    return shape;
}
bool Draw_Popup(const text::FontFace& font,Graphics::Renderer2D& renderer,std::u16string_view title,
    HitRect body,Graphics::Rect2D screen,const ValuePalette& palette,Graphics::Point2D title_padding,
    Graphics::Point2D scale,Graphics::Color2D blackout,bool darken_background) {
    const auto shape=Layout_Popup(font,title,body,title_padding,scale);if(!shape) return false;
    if(darken_background && !renderer.Add_Rect(screen,blackout)) return false;
    if(!Draw_Filled_Outline(renderer,body,palette.line,palette.background)) return false;
    if(shape->title) {
        const auto rect=*shape->title;
        if(!Draw_Filled_Outline(renderer,rect,palette.line,palette.background)) return false;
        text::TextStyle style;style.color=palette.text;style.drop_color=palette.shadow;style.x_drop=-1;style.y_drop=1;
        TextBoxOptions options;options.alignment=TextAlignment::Center;options.vertical_center=true;
        if(!Draw_Text_Box(font,renderer,title,{rect.left,rect.top,rect.right,rect.bottom},style,options)) return false;
    }
    return true;
}
}
