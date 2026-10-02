export module engine.gui.text.renderer;
import std;
export import engine.gui.text.layout;
export import engine.gui.text.font_face;
import Graphics.Renderer2D;

export namespace engine::gui::text {
struct TextStyle final
{
	Graphics::Color2D color{};
	Graphics::Color2D drop_color{};
	Graphics::Color2D hotkey_color{};
	int x_drop = 1;
	int y_drop = 1;
	Graphics::Renderer2DBlendMode blend{Graphics::Renderer2DBlendMode::Alpha};
};

namespace text_detail {
bool DrawGlyphLayer(Graphics::Renderer2D& renderer,const FontFace& font,std::span<const TextPlacement> placements,
    std::vector<Graphics::Renderer2DGlyph>& glyphs,float x,float y,Graphics::Color2D color,bool hotkey_only,
    bool snap_to_pixels,Graphics::Renderer2DBlendMode blend) {
    glyphs.clear();std::uint32_t page=0;
    const auto flush=[&]() {
        if(glyphs.empty()) return true;
        const auto texture=font.Ensure_Texture(renderer,page,blend==Graphics::Renderer2DBlendMode::Additive
            ? FontAtlasMode::AdditiveInk : FontAtlasMode::Alpha);
        const bool drawn=texture.index.Is_Valid() && renderer.Add_Text_Glyphs(glyphs,texture,blend);
        glyphs.clear();return drawn;
    };
    for(const auto& placement:placements) {
        if(placement.hotkey!=hotkey_only) continue;
        const auto* glyph=font.Get_Glyph(placement.character);if(!glyph) continue;
        if(glyph->page!=page && !flush()) return false;page=glyph->page;
        const float left=snap_to_pixels ? std::trunc(x+placement.x) : x+placement.x;
        const float top=snap_to_pixels ? std::trunc(y+placement.y) : y+placement.y;
        glyphs.push_back({{left,top,left+glyph->width,top+font.Height()},glyph->uv,color});
    }
    return flush();
}
}
// Reuse the ordinary glyph/shadow batching for precomputed layouts such as
// scrolling text. Layout remains independent of GPU resources and game state.
bool Draw_Placed_Text(Graphics::Renderer2D& renderer,const FontFace& font,std::span<const TextPlacement> placements,
    float x,float y,TextStyle style,bool snap_to_pixels=true) {
    if(!std::isfinite(x) || !std::isfinite(y) || !std::ranges::all_of(placements,[](const auto& p) {
        return std::isfinite(p.x) && std::isfinite(p.y);
    })) return false;
    std::vector<Graphics::Renderer2DGlyph> glyphs;glyphs.reserve(placements.size());
    return text_detail::DrawGlyphLayer(renderer,font,placements,glyphs,x+style.x_drop,y+style.y_drop,style.drop_color,false,snap_to_pixels,style.blend)
        && text_detail::DrawGlyphLayer(renderer,font,placements,glyphs,x,y,style.color,false,snap_to_pixels,style.blend);
}

class TextRenderer final
{
public:
	explicit TextRenderer(std::size_t placement_capacity=8192, std::size_t line_capacity=1024)
		: m_layout(placement_capacity, line_capacity)
	{
		m_glyphs.reserve(placement_capacity);
	}

	bool Measure(
		const FontFace &font,
		const std::uint16_t *text,
		TextLayoutOptions options,
		std::uint32_t &width,
		std::uint32_t &height)
	{
		if (!Build_Layout(font, text, options))
			return false;
		width = static_cast<std::uint32_t>(m_layout.Width());
		height = m_layout.Height();
		return true;
	}

	bool Draw(
		Graphics::Renderer2D &renderer,
		const FontFace &font,
		const FontFace *hotkey_font,
		const std::uint16_t *text,
		float x,
		float y,
		TextLayoutOptions options,
		TextStyle style)
	{
		if (!Build_Layout(font, text, options))
			return false;
        if (!Draw_Glyphs(renderer,font,x+style.x_drop,y+style.y_drop,style.drop_color,false,options.snap_to_pixels,style.blend)
            || !Draw_Glyphs(renderer,font,x,y,style.color,false,options.snap_to_pixels,style.blend)) return false;
        if (!options.parse_hotkey) return true;
        const FontFace& highlight=hotkey_font ? *hotkey_font : font;
        return Draw_Glyphs(renderer,highlight,x+style.x_drop,y+style.y_drop,style.drop_color,true,options.snap_to_pixels,style.blend)
            && Draw_Glyphs(renderer,highlight,x,y,style.hotkey_color,true,options.snap_to_pixels,style.blend);
	}

private:
	static int Spacing(const void *context, std::uint16_t character) noexcept
	{
		return static_cast<const FontFace *>(context)->Get_Char_Spacing(character);
	}
	static int Height(const void *context) noexcept
	{
		return static_cast<const FontFace *>(context)->Height();
	}
	static int Overlap(const void *context) noexcept
	{
		return static_cast<const FontFace *>(context)->Extra_Overlap();
	}

	bool Build_Layout(const FontFace &font, const std::uint16_t *text, TextLayoutOptions options)
	{
		return m_layout.Build({&font, &Spacing, &Height, &Overlap}, text, options);
	}

    bool Draw_Glyphs(Graphics::Renderer2D& renderer,const FontFace& font,
        float x,float y,Graphics::Color2D color,bool hotkey_only,bool snap_to_pixels,Graphics::Renderer2DBlendMode blend)
    {
        return text_detail::DrawGlyphLayer(renderer,font,m_layout.Placements(),m_glyphs,x,y,color,hotkey_only,snap_to_pixels,blend);
    }

	TextLayout m_layout;
	std::vector<Graphics::Renderer2DGlyph> m_glyphs;
};

}
