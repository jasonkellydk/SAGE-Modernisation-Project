export module engine.gui.w3d.input_view;
import std;
import engine.gui.w3d.input_capture;
import engine.gui.w3d.dialog_input;
import engine.gui.w3d.value_view;
import engine.gui.w3d.text_view;
import engine.gui.text.font_face;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
bool DrawInputCapture(const InputCaptureModel& model,const text::FontFace& font,
    Graphics::Renderer2D& renderer,HitRect bounds,const ValuePalette& palette) {
    constexpr float limit=std::numeric_limits<int>::max()/4.0f;
    for(const float coordinate:{bounds.left,bounds.top,bounds.right,bounds.bottom})
        if(!std::isfinite(coordinate) || std::abs(coordinate)>=limit) return false;
    if(bounds.right-bounds.left<=2 || bounds.bottom-bounds.top<=2) return false;
    if(!Draw_Filled_Outline(renderer,bounds,palette.line,palette.background)) return false;
    const Graphics::Rect2D client{bounds.left+1,bounds.top+1,bounds.right-1,bounds.bottom-1};
    text::TextStyle style;style.color=palette.text;style.drop_color=palette.shadow;style.x_drop=-1;style.y_drop=1;
    TextBoxOptions options;options.clip=true;options.vertical_center=true;options.alignment=TextAlignment::Center;
    if(!Draw_Text_Box(font,renderer,model.assignment.Get().caption,client,style,options)) return false;
    return !model.focused.Get() || renderer.Add_Rect(client,palette.highlight,Graphics::Renderer2DBlendMode::Additive);
}
}
