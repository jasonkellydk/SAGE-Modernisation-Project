export module engine.gui.w3d.marquee_view;
import std;
import engine.gui.w3d.dialog_input;
import engine.gui.w3d.marquee;
import engine.gui.text.renderer;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
struct MarqueeRow {
    unsigned font{};std::uint32_t color{};float width{},height{};
    std::vector<text::TextPlacement> placements;
};
struct MarqueeLayout {std::vector<MarqueeRow> rows;HitRect client;float height{};};
struct MarqueeRowPosition {std::size_t index{};float top{};};

std::optional<HitRect> MarqueeClient(HitRect bounds,std::array<float,2> padding) {
    if(!std::isfinite(bounds.left) || !std::isfinite(bounds.top) || !std::isfinite(bounds.right) || !std::isfinite(bounds.bottom) ||
        !std::isfinite(padding[0]) || !std::isfinite(padding[1]) || padding[0]<0 || padding[1]<0) return {};
    HitRect client{std::trunc(bounds.left+padding[0]),std::trunc(bounds.top+padding[1]),
        std::trunc(bounds.right-padding[0]),std::trunc(bounds.bottom-padding[1])};
    if(client.right<=client.left || client.bottom<=client.top || client.right-client.left>16777216) return {};
    return client;
}
std::expected<MarqueeLayout,std::string> Build_Marquee_Layout(std::span<const MarqueeLine> lines,
    std::span<const text::FontFace* const> fonts,HitRect client) {
    const auto valid=MarqueeClient(client,{0,0});if(!valid) return std::unexpected("invalid marquee client area");
    MarqueeLayout result;result.client=client;
    for(const auto& line:lines) {
        if(line.font>=fonts.size() || !fonts[line.font] || fonts[line.font]->Height()<=0)
            return std::unexpected("marquee font is unavailable");
        const auto& font=*fonts[line.font];
        std::vector<std::uint16_t> characters(line.text.begin(),line.text.end());characters.push_back(0);
        text::TextLayout layout(characters.size()+1,characters.size()+1);
        const text::TextMetrics metrics{&font,
            [](const void* f,std::uint16_t c) noexcept {return static_cast<const text::FontFace*>(f)->Get_Char_Spacing(c);},
            [](const void* f) noexcept {return static_cast<const text::FontFace*>(f)->Height();},
            [](const void* f) noexcept {return static_cast<const text::FontFace*>(f)->Extra_Overlap();}};
        text::TextLayoutOptions options;options.wrapping_width=static_cast<int>(client.right-client.left);
        if(!layout.Build(metrics,characters.data(),options)) return std::unexpected("marquee layout exceeds its capacity");
        for(const auto& source:layout.Lines()) {
            if(result.rows.size()>=65536) return std::unexpected("marquee has too many rows");
            MarqueeRow row{line.font,line.color,source.width,float(font.Height())};
            const auto placements=layout.Placements().subspan(source.first,source.count);
            row.placements.assign(placements.begin(),placements.end());
            for(auto& placement:row.placements) placement.y=0;
            result.height+=row.height;if(result.height>16777216) return std::unexpected("marquee height exceeds its capacity");
            result.rows.push_back(std::move(row));
        }
    }
    return result;
}
std::vector<MarqueeRowPosition> Visible_Marquee_Rows(const MarqueeLayout& layout,float scroll) {
    std::vector<MarqueeRowPosition> visible;
    if(!std::isfinite(scroll) || scroll<0) return visible;
    const float start=scroll-(layout.client.bottom-layout.client.top);float height{};std::size_t first{};
    while(first<layout.rows.size() && height+layout.rows[first].height<start) height+=layout.rows[first++].height;
    float top=layout.client.top+height-start;
    for(auto index=first;index<layout.rows.size() && top<layout.client.bottom;++index) {
        visible.push_back({index,top});top=std::trunc(top+layout.rows[index].height);
    }
    return visible;
}
bool Draw_Marquee(Graphics::Renderer2D& renderer,const MarqueeLayout& layout,
    std::span<const text::FontFace* const> fonts,float scroll,Graphics::Color2D shadow) {
    const auto previous=renderer.Get_Clip();
    struct Restore {Graphics::Renderer2D& renderer;Graphics::Renderer2D::ClipState clip;~Restore(){renderer.Set_Clip(clip.enabled,clip.rectangle);}} restore{renderer,previous};
    Graphics::Rect2D clip{layout.client.left,layout.client.top,layout.client.right,layout.client.bottom};
    if(previous.enabled) clip={std::max(clip.left,previous.rectangle.left),std::max(clip.top,previous.rectangle.top),
        std::min(clip.right,previous.rectangle.right),std::min(clip.bottom,previous.rectangle.bottom)};
    if(clip.right<=clip.left || clip.bottom<=clip.top) return true;
    renderer.Set_Clip(true,clip);
    for(const auto& positioned:Visible_Marquee_Rows(layout,scroll)) {
        const auto& row=layout.rows[positioned.index];if(row.font>=fonts.size() || !fonts[row.font]) return false;
        text::TextStyle style;style.color=Graphics::Color2D::From_ARGB(row.color);style.drop_color=shadow;style.x_drop=-1;style.y_drop=1;
        const float x=std::trunc(layout.client.left+(layout.client.right-layout.client.left-row.width)*0.5f);
        if(!text::Draw_Placed_Text(renderer,*fonts[row.font],row.placements,x,std::trunc(positioned.top),style)) return false;
    }
    return true;
}
}
