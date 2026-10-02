export module engine.gui.w3d.text_edit_view;
import std;
import engine.gui.w3d.text_edit;
import engine.gui.w3d.dialog_input;
import engine.gui.w3d.value_view;
import engine.gui.w3d.text_view;
import engine.gui.text.font_face;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
struct TextEditViewState {std::size_t scroll{};};
struct TextEditLayout {HitRect client;std::u16string display;std::vector<float> positions;std::size_t scroll{},caret{};};
std::expected<TextEditLayout,std::string> BuildTextEditLayout(const TextEditModel& model,
    const text::FontFace& font,HitRect bounds,TextEditViewState& state) {
    constexpr float limit=std::numeric_limits<int>::max()/4.0f;
    for(const float value:{bounds.left,bounds.top,bounds.right,bounds.bottom}) if(!std::isfinite(value) || std::abs(value)>=limit) return std::unexpected("invalid edit bounds");
    const auto margin=Measure_Text(font,u"I");if(!margin || !font.Height()) return std::unexpected("edit font unavailable");
    HitRect client{bounds.left+(*margin)[0],bounds.top+2,bounds.right-(*margin)[0],bounds.bottom-2};
    if(client.right<=client.left || client.bottom-client.top<=2) return std::unexpected("empty edit client");
    auto display=model.text.Get();const auto caret=model.selection.Get().caret;
    if(caret>display.size() || model.selection.Get().anchor>display.size()) return std::unexpected("invalid edit selection");
    display.insert(caret,model.composition.Get().text);
    TextEditLayout result{client,std::move(display),{0},0,caret};result.positions.reserve(result.display.size()+1);
    for(std::size_t i=0;i<result.display.size();++i) {
        const auto size=Measure_Text(font,std::u16string_view(result.display).substr(i,1));
        if(!size) return std::unexpected("edit glyph unavailable");result.positions.push_back(result.positions.back()+(*size)[0]);
    }
    state.scroll=std::min(state.scroll,result.display.size());
    const auto position=result.positions[caret]-result.positions[state.scroll];
    if(position<=0) state.scroll=caret>2 ? caret-2 : 0;
    else if(position>=client.right-client.left) for(std::size_t i=0;i<caret;++i)
        if(result.positions[caret]-result.positions[i]<client.right-client.left) {state.scroll=std::min(i+2,caret);break;}
    if(!model.focused.Get()) state.scroll=0;
    state.scroll=std::min(state.scroll,result.display.empty() ? 0 : result.display.size()-1);
    result.scroll=state.scroll;return result;
}
std::size_t TextEditHit(const TextEditLayout& layout,float x) {
    if(!std::isfinite(x)) return layout.caret;
    const auto offset=x-layout.client.left+layout.positions[layout.scroll];
    for(std::size_t i=layout.scroll;i<layout.display.size();++i)
        if((layout.positions[i]+layout.positions[i+1])*0.5f>=offset) return i;
    return layout.display.size();
}
bool DrawTextEdit(const TextEditModel& model,const text::FontFace& font,Graphics::Renderer2D& renderer,
    HitRect bounds,const ValuePalette& palette,TextEditViewState& state,TextEditLayout* layout_out=nullptr) {
    auto layout=BuildTextEditLayout(model,font,bounds,state);if(!layout) return false;
    if(!Draw_Filled_Outline(renderer,bounds,palette.line,palette.background)) return false;
    const auto& client=layout->client;const float origin=layout->positions[layout->scroll];
    text::TextStyle style;style.color=palette.text;style.drop_color=palette.shadow;style.x_drop=-1;style.y_drop=1;
    TextBoxOptions options;options.clip=true;options.vertical_center=true;
    if(!Draw_Text_Box(font,renderer,std::u16string_view(layout->display).substr(layout->scroll),
        {client.left,client.top,client.right,client.bottom},style,options)) return false;
    const auto previous=renderer.Get_Clip();struct Restore {Graphics::Renderer2D& renderer;Graphics::Renderer2D::ClipState clip;~Restore(){renderer.Set_Clip(clip.enabled,clip.rectangle);}} restore{renderer,previous};
    Graphics::Rect2D clip{client.left,client.top,client.right,client.bottom};
    if(previous.enabled) {clip.left=std::max(clip.left,previous.rectangle.left);clip.right=std::min(clip.right,previous.rectangle.right);clip.top=std::max(clip.top,previous.rectangle.top);clip.bottom=std::min(clip.bottom,previous.rectangle.bottom);}
    if(clip.right>clip.left && clip.bottom>clip.top) {
        renderer.Set_Clip(true,clip);const auto [first,last]=model.Selection();
        if(model.enabled.Get() && first!=last && !renderer.Add_Rect({client.left+layout->positions[first]-origin,client.top+1,
            client.left+layout->positions[last]-origin,client.bottom-1},palette.highlight,Graphics::Renderer2DBlendMode::Additive)) return false;
        const auto& composition=model.composition.Get();
        if(!composition.text.empty()) {
            const auto start=layout->caret,end=start+composition.text.size();
            if(!renderer.Add_Line({client.left+layout->positions[start]-origin,client.bottom-2},
                {client.left+layout->positions[end]-origin,client.bottom-2},1,palette.line)) return false;
        }
        if(model.CaretVisible()) {
            const auto x=std::trunc(client.left+layout->positions[layout->caret]-origin);
            if(!renderer.Add_Rect({x,std::trunc(client.top+1),x+1,std::trunc(client.bottom-1)},palette.text)) return false;
        }
    }
    if(layout_out) *layout_out=std::move(*layout);return true;
}
}
