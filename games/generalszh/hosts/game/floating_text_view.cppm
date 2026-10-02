export module games.generalszh.hosts.game.floating_text_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
export import games.generalszh.content.global.language_fonts;
export import games.generalszh.content.global.draw_group_info;

// Draws the floating texts the overlay lists (InGameUI::drawFloatingText):
// each centred on its point in the display string font (Language.ini
// DefaultDisplayStringFont, sized to the screen), in its
// colour with a black drop shadow at the same alpha.
export namespace generalszh::host
{
class FloatingTextView
{
public:
	bool Load(const content::LanguageFont &font, float fontScale)
	{
		m_font = Make(font, fontScale);
		return m_font != nullptr;
	}

	// The drawables' caption font (Drawable's construct display string: InGameUI's DrawableCaptionFont, else Language.ini's,
	// at GlobalLanguage::adjustFontSize's size).
	bool LoadCaption(const content::LanguageFont &font, float fontScale)
	{
		m_caption = Make(font, fontScale);
		return m_caption != nullptr;
	}

	bool Draw(const std::vector<OverlayText> &texts, Graphics::Renderer2D &renderer)
	{
		if (!m_font || texts.empty())
			return true;
		m_list.Clear();
		for (const OverlayText &text : texts)
		{
			const Engine::UI::WND::FontFace *font = text.caption && m_caption ? m_caption.get() : m_font.get();
			float width = 0.0f;
			for (const char16_t character : text.text)
				width += static_cast<float>(font->Get_Char_Spacing(static_cast<std::uint16_t>(character)));
			Engine::UI::WND::TextStyle style;
			style.color = {text.color[0], text.color[1], text.color[2], text.color[3]};
			style.drop_color = {0.0f, 0.0f, 0.0f, text.color[3]};
			Engine::UI::WND::StaticTextVisual visual;
			const float left = std::floor(text.x - width / 2.0f);
			visual.rectangle = {left, text.y, left + width + 2.0f, text.y + static_cast<float>(font->Height() + 2)};
			Engine::UI::WND::StaticTextContent content;
			content.font = font;
			content.text = reinterpret_cast<const std::uint16_t *>(text.text.c_str());
			content.style = style;
			Engine::UI::WND::Add_Static_Text(m_list, visual, content);
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

	// The control group numerals (W3DDisplayStringManager::postProcessLoad: NUMBER:0..9 in DrawGroupInfo's font, its
	// point size as given, not resized for the screen).
	bool LoadGroupNumbers(const content::DrawGroupInfoContent &info, std::array<std::u16string, 10> numerals)
	{
		m_numerals = std::move(numerals);
		m_groupFont = Make(content::LanguageFont{info.fontName, info.fontSize, info.fontIsBold}, 1.0f);
		return m_groupFont != nullptr;
	}

	// Drawable::drawCaption: the text half its width left of the drawable's centre, over a box a pixel larger each way
	// (drawFillRect black at 125, drawOpenRect 1 pixel of 20,20,20), in DrawableCaptionColor with a black shadow.
	bool DrawCaptions(const std::vector<OverlayDrawableCaption> &captions, Graphics::Renderer2D &renderer)
	{
		const Engine::UI::WND::FontFace *font = m_caption ? m_caption.get() : m_font.get();
		if (font == nullptr || captions.empty())
			return true;
		m_list.Clear();
		for (const OverlayDrawableCaption &caption : captions)
		{
			int width = 0;
			for (const char16_t character : caption.text)
				width += font->Get_Char_Spacing(static_cast<std::uint16_t>(character));
			const int height = font->Height();
			const int x = caption.x - width / 2;
			const float boxX = static_cast<float>(x - 1), boxY = static_cast<float>(caption.y - 1);
			renderer.Add_Rect({boxX, boxY, boxX + static_cast<float>(width + 2), boxY + static_cast<float>(height + 2)},
				Graphics::Color2D{0.0f, 0.0f, 0.0f, 125.0f / 255.0f});
			renderer.Add_Outline({boxX, boxY, boxX + static_cast<float>(width + 2), boxY + static_cast<float>(height + 2)}, 1.0f,
				Graphics::Color2D{20.0f / 255.0f, 20.0f / 255.0f, 20.0f / 255.0f, 1.0f});
			AddText(*font, caption.text, static_cast<float>(x), static_cast<float>(caption.y), caption.color, {0.0f, 0.0f, 0.0f, 1.0f}, 1, 1);
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

	// Drawable::drawUIText: each numeral at its point, its colour, its drop shadow's colour and offset.
	bool DrawGroupNumbers(const std::vector<OverlayGroupNumber> &numbers, Graphics::Renderer2D &renderer)
	{
		if (!m_groupFont || numbers.empty())
			return true;
		m_list.Clear();
		for (const OverlayGroupNumber &number : numbers)
			if (number.group >= 0 && number.group < 10)
				AddText(*m_groupFont, m_numerals[static_cast<std::size_t>(number.group)], static_cast<float>(number.x), static_cast<float>(number.y),
					number.color, number.dropColor, number.dropX, number.dropY);
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	void AddText(const Engine::UI::WND::FontFace &font, const std::u16string &text, float x, float y, const std::array<float, 4> &color,
		const std::array<float, 4> &drop, int dropX, int dropY)
	{
		float width = 0.0f;
		for (const char16_t character : text)
			width += static_cast<float>(font.Get_Char_Spacing(static_cast<std::uint16_t>(character)));
		Engine::UI::WND::TextStyle style;
		style.color = {color[0], color[1], color[2], color[3]};
		style.drop_color = {drop[0], drop[1], drop[2], drop[3]};
		style.x_drop = dropX;
		style.y_drop = dropY;
		Engine::UI::WND::StaticTextVisual visual;
		visual.rectangle = {x, y, x + width + 2.0f, y + static_cast<float>(font.Height() + 2)};
		Engine::UI::WND::StaticTextContent content;
		content.font = &font;
		content.text = reinterpret_cast<const std::uint16_t *>(text.c_str());
		content.style = style;
		Engine::UI::WND::Add_Static_Text(m_list, visual, content);
	}

	static std::unique_ptr<Engine::UI::WND::FontFace> Make(const content::LanguageFont &font, float fontScale)
	{
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr)
			return nullptr;
		const auto size = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::floor(static_cast<float>(font.size) * fontScale))));
		const Assets::FontAssetHandle handle = cache->Request_Font(font.name, size, font.bold);
		cache->Wait(handle);
		const Assets::FontAsset *asset = cache->Try_Get_Font(handle);
		auto face = std::make_unique<Engine::UI::WND::FontFace>();
		if (asset == nullptr || !face->Build(*asset))
			return nullptr;
		return face;
	}

	std::unique_ptr<Engine::UI::WND::FontFace> m_font;
	std::unique_ptr<Engine::UI::WND::FontFace> m_caption;
	std::unique_ptr<Engine::UI::WND::FontFace> m_groupFont;
	std::array<std::u16string, 10> m_numerals;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
