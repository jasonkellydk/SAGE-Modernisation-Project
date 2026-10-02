export module engine.gui.w3d.list_view;
import std;
import engine.gui.w3d.dialog_input;
import engine.gui.w3d.list_selection;
import engine.gui.w3d.text_view;
import engine.gui.w3d.value_view;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
struct ListRow {std::u16string text;Graphics::Color2D color{1,1,1,1};};
struct ListLayout {HitRect bounds,header,text;std::vector<float> heights;std::optional<HitRect> scrollbar;};
struct ListLayoutStyle {float vertical_inset{},header_rows{},row_spacing{};bool show_header{true};float scrollbar_width{};};
std::expected<ListLayout,std::string> Build_List_Layout(const text::FontFace& header_font,
    const text::FontFace& row_font,std::span<const ListRow> rows,HitRect bounds,ListLayoutStyle style) {
    if(!std::isfinite(bounds.left) || !std::isfinite(bounds.top) || !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom) ||
        !std::isfinite(style.vertical_inset) || !std::isfinite(style.header_rows) || !std::isfinite(style.row_spacing) ||
        !std::isfinite(style.scrollbar_width) || style.scrollbar_width<0 ||
        style.vertical_inset<0 || style.header_rows<0 || style.row_spacing<0 || rows.size()>65536)
        return std::unexpected("invalid list geometry");
    const auto margin=Measure_Text(header_font,u"W");if(!margin) return std::unexpected("list header font is unavailable");
    HitRect client{bounds.left+(*margin)[0],bounds.top+style.vertical_inset,bounds.right-(*margin)[0],bounds.bottom-style.vertical_inset};
    if(client.right<=client.left || client.bottom<=client.top || client.right-client.left>16777216 || row_font.Height()<=0)
        return std::unexpected("list client area is empty");
    auto header=client;header.bottom=header.top+(style.show_header ? header_font.Height()*style.header_rows : 0);
    auto text_bounds=client;text_bounds.top=header.bottom;
    if(text_bounds.bottom<=text_bounds.top) return std::unexpected("list header consumes its page");
    ListLayout layout{bounds,header,text_bounds};layout.heights.reserve(rows.size());
    for(const auto& row:rows) {
        std::vector<std::uint16_t> characters(row.text.begin(),row.text.end());characters.push_back(0);
        text::TextRenderer measure(row.text.size()+1,row.text.size()+1);text::TextLayoutOptions options;
        options.wrapping_width=static_cast<int>(text_bounds.right-text_bounds.left);std::uint32_t width{},height{};
        if(!measure.Measure(row_font,characters.data(),options,width,height)) return std::unexpected("list row layout exceeds capacity");
        const float row_height=std::max(float(row_font.Height()),float(height))+std::trunc(style.row_spacing);
        if(row_height>16777216) return std::unexpected("list row exceeds capacity");layout.heights.push_back(row_height);
    }
    if(style.scrollbar_width>0 && std::accumulate(layout.heights.begin(),layout.heights.end(),0.f)>=text_bounds.bottom-text_bounds.top) {
        auto narrowed=bounds;narrowed.right=std::trunc(bounds.right-style.scrollbar_width);
        const auto bar=HitRect{narrowed.right,bounds.top,bounds.right,bounds.bottom};
        style.scrollbar_width=0;
        auto result=Build_List_Layout(header_font,row_font,rows,narrowed,style);
        if(!result) return result;
        result->scrollbar=bar;return result;
    }
    return layout;
}
bool Draw_List(Graphics::Renderer2D& renderer,const ListLayout& layout,const ListSelectionModel& selection,
    const text::FontFace& header_font,const text::FontFace& row_font,std::u16string_view header,
    std::span<const ListRow> rows,ValuePalette palette,Graphics::Color2D selection_outline) {
    if(rows.size()!=layout.heights.size() || rows.size()!=static_cast<std::size_t>(selection.Count())) return false;
    if(!Draw_Filled_Outline(renderer,layout.bounds,palette.line,palette.background)) return false;
    // The control renderer paints the selection outline separately from text.
    // Get_Entry_Rect truncates its top before computing its bottom, and permits
    // an exact page fit even though Create_Text_Renderers omits that row.
    if(selection.selected.Get()>=selection.scroll.Get() && selection.selected.Get()<selection.Count()) {
        float top=layout.text.top;
        for(int i=selection.scroll.Get();i<selection.selected.Get();++i) top+=layout.heights[i];
        top=std::trunc(top);const float bottom=std::trunc(top+layout.heights[selection.selected.Get()]);
        if(top>=layout.text.top && bottom<=layout.text.bottom &&
            !renderer.Add_Outline({std::trunc(layout.text.left),top,std::trunc(layout.text.right),bottom},1,selection_outline)) return false;
    }
    text::TextStyle style;style.color={1,1,1,1};style.drop_color={0,0,0,1};style.x_drop=-1;style.y_drop=1;
    TextBoxOptions options;options.vertical_center=true;options.clip=true;
    if(layout.header.bottom>layout.header.top) {
        if(!Draw_Text_Box(header_font,renderer,header,{layout.header.left,layout.header.top,layout.header.right,layout.header.bottom},style,options)) return false;
        const auto extent=Measure_Text(header_font,header);if(!extent) return false;
        const float y=layout.header.top+(layout.header.bottom-layout.header.top+(*extent)[1])*0.5f+2;
        if(!renderer.Add_Line({layout.header.left,y},{layout.header.left+(*extent)[0],y},1,style.color)) return false;
    }
    options.wrap=true;float top=layout.text.top;
    for(int i=selection.scroll.Get();i<selection.Count();++i) {
        const float bottom=top+layout.heights[i];
        // Source painting omits even an exactly-fitting bottom row; paging and
        // hit testing retain their distinct inclusive boundary rules.
        if(bottom>=layout.text.bottom) break;
        const Graphics::Rect2D rectangle{std::trunc(layout.text.left),std::trunc(top),std::trunc(layout.text.right),std::trunc(bottom)};
        style.color=rows[i].color;
        if(!Draw_Text_Box(row_font,renderer,rows[i].text,rectangle,style,options)) return false;
        if(selection.selected.Get()==i) {
            if(!renderer.Add_Rect(rectangle,palette.highlight,Graphics::Renderer2DBlendMode::Additive)) return false;
        }
        top=bottom;
    }
    return true;
}
}
