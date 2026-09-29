export module games.generalszh.hosts.game.floating_text_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.hosts.game.game_client;
export import games.generalszh.content.global.language_fonts;

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
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr)
			return false;
		const auto size = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::floor(static_cast<float>(font.size) * fontScale))));
		const Assets::FontAssetHandle handle = cache->Request_Font(font.name, size, font.bold);
		cache->Wait(handle);
		const Assets::FontAsset *asset = cache->Try_Get_Font(handle);
		m_font = std::make_unique<Engine::UI::WND::FontFace>();
		if (asset == nullptr || !m_font->Build(*asset))
		{
			m_font.reset();
			return false;
		}
		return true;
	}

	bool Draw(const std::vector<OverlayText> &texts, Graphics::Renderer2D &renderer)
	{
		if (!m_font || texts.empty())
			return true;
		m_list.Clear();
		for (const OverlayText &text : texts)
		{
			float width = 0.0f;
			for (const char16_t character : text.text)
				width += static_cast<float>(m_font->Get_Char_Spacing(static_cast<std::uint16_t>(character)));
			Engine::UI::WND::TextStyle style;
			style.color = {text.color[0], text.color[1], text.color[2], text.color[3]};
			style.drop_color = {0.0f, 0.0f, 0.0f, text.color[3]};
			Engine::UI::WND::StaticTextVisual visual;
			const float left = std::floor(text.x - width / 2.0f);
			visual.rectangle = {left, text.y, left + width + 2.0f, text.y + static_cast<float>(m_font->Height() + 2)};
			Engine::UI::WND::StaticTextContent content;
			content.font = m_font.get();
			content.text = reinterpret_cast<const std::uint16_t *>(text.text.c_str());
			content.style = style;
			Engine::UI::WND::Add_Static_Text(m_list, visual, content);
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	std::unique_ptr<Engine::UI::WND::FontFace> m_font;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
