export module engine.gui.text.font_face;
import std;
import Assets.Fonts;
import Graphics.Text.GlyphAtlas;
import Graphics.Renderer2D;

export namespace engine::gui::text
{
enum class FontAtlasMode { Alpha,AdditiveInk };
struct FontGlyph final
{
	std::uint16_t character = 0;
	std::uint16_t width = 0;
	std::int16_t spacing = 0;
	Graphics::Rect2D uv{};
	bool available = false;
	std::uint32_t page = 0;
};

class FontFace final
{
public:
	FontFace() = default;
	FontFace(const FontFace &) = delete;
	FontFace &operator=(const FontFace &) = delete;

    bool Build(const Assets::FontAsset &source)
    {
        if (source.Height()==0 || m_revision==std::numeric_limits<std::uint32_t>::max()) return false;
        m_height=source.Height();
        m_extra_overlap=source.Extra_Overlap();
        m_atlas.Clear(); m_glyphs.clear();m_additive_pixels.clear();
        m_glyphs.reserve(source.Glyphs().size());
        for (const auto& input : source.Glyphs()) {
            FontGlyph glyph;
            glyph.character=input.character; glyph.width=input.width; glyph.spacing=input.spacing;
            if (input.width) {
                const auto region=m_atlas.Add(input.width,source.Height(),input.alpha);
                if (!region) return false;
                const float size=static_cast<float>(m_atlas.Page_Size());
                glyph.uv={region->x/size,region->y/size,(region->x+region->width)/size,(region->y+region->height)/size};
                glyph.page=region->page;
                glyph.available=true;
            }
            m_glyphs.push_back(glyph);
        }
        // Retain page owners when rebuilding for a new drawable size. The
        // renderer updates the existing texture instead of retaining a new
        // atlas allocation for every resize. Spare page owners can be reused.
        for (std::size_t page=m_resource_ids.size(); page<m_atlas.Page_Count(); ++page) {
            const auto id=Allocate_Resource_Id();
            const auto additive_id=Allocate_Resource_Id();
            if(!id || !additive_id) return false;
            m_resource_ids.push_back(id);
            m_additive_resource_ids.push_back(additive_id);
        }
        ++m_revision;
        return true;
    }

	int Height() const noexcept { return m_height; }
	int Extra_Overlap() const noexcept { return m_extra_overlap; }
	int Get_Char_Spacing(std::uint16_t character) const noexcept
	{
		const FontGlyph *glyph = Find_Glyph(character);
		return glyph != nullptr ? glyph->spacing : 0;
	}
	const FontGlyph *Get_Glyph(std::uint16_t character) const noexcept
	{
		const FontGlyph *glyph = Find_Glyph(character);
		if (glyph == nullptr || !glyph->available)
			return nullptr;
		return glyph;
	}

    Graphics::Renderer2DTexture Ensure_Texture(Graphics::Renderer2D &renderer, std::uint32_t page=0,
        FontAtlasMode mode=FontAtlasMode::Alpha) const;

private:
	const FontGlyph *Find_Glyph(std::uint16_t character) const noexcept
	{
		const auto found = std::lower_bound(m_glyphs.begin(), m_glyphs.end(), character,
			[](const FontGlyph &glyph, std::uint16_t value) { return glyph.character < value; });
		return found != m_glyphs.end() && found->character == character ? &*found : nullptr;
	}

	static std::uint32_t Allocate_Resource_Id() noexcept
	{
		static std::uint32_t next_id = 0x20000000u;
		if (next_id == 0x3fffffffu)
			return 0;
		return next_id++;
	}

	Graphics::GlyphAtlas m_atlas;
	std::vector<FontGlyph> m_glyphs;
	std::vector<std::uint32_t> m_resource_ids;
	std::vector<std::uint32_t> m_additive_resource_ids;
	mutable std::vector<std::vector<std::uint8_t>> m_additive_pixels;
	int m_height = 0;
	int m_extra_overlap = 0;
	std::uint32_t m_revision = 0;
};

Graphics::Renderer2DTexture FontFace::Ensure_Texture(Graphics::Renderer2D &renderer, std::uint32_t page,FontAtlasMode mode) const
{
    if (page>=m_atlas.Page_Count() || page>=m_resource_ids.size()) return {};
    const auto size=m_atlas.Page_Size();
    if(mode==FontAtlasMode::AdditiveInk) {
        // One-plus-one blending needs black RGB outside the ink. This variant
        // retains the W3D sentence texture's nonzero-coverage RGB mask; ordinary
        // text keeps its white-RGB alpha atlas and antialiasing unchanged.
        if(m_additive_pixels.size()!=m_atlas.Page_Count()) m_additive_pixels.resize(m_atlas.Page_Count());
        auto& pixels=m_additive_pixels[page];
        if(pixels.empty()) {
            const auto original=m_atlas.Pixels(page);pixels.assign(original.begin(),original.end());
            for(std::size_t index=0;index<pixels.size();index+=4)
                pixels[index]=pixels[index+1]=pixels[index+2]=pixels[index+3] ? 255 : 0;
        }
        return renderer.Register_Texture({Graphics::TextureHandle(m_additive_resource_ids[page],1),
            size,size,size*4,m_revision,std::as_bytes(std::span(pixels))});
    }
    return renderer.Register_Texture({Graphics::TextureHandle(m_resource_ids[page],1),
        size,size,size*4,m_revision,std::as_bytes(m_atlas.Pixels(page))});
}

}
