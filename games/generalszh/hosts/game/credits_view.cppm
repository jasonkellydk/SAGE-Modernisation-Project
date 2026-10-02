export module games.generalszh.hosts.game.credits_view;
import std;

import Engine.UI.WND;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Runtime;
import games.generalszh.shell.credits.credits_view_model;

// Draws the credits the view model scrolls (the original's CreditsManager::draw):
// each line centred in its style's font (Language.ini CreditsTitleFont,
// CreditsMinorTitleFont, CreditsNormalFont, as the installed Language.ini names
// them), sized to the screen as the original's classic font scaling), column
// lines at a third and two thirds of the width, in the style's colour faded
// by the line's opacity, with a black drop shadow a pixel down and right.
export import games.generalszh.content.global.language_fonts;

export namespace generalszh::host
{
class CreditsView
{
public:
	// The fonts for a `width`-pixel-wide screen; false when a font cannot be made.
	bool Load(std::uint32_t width, float fontScale, const std::array<content::LanguageFont, 3> &styles)
	{
		Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
		if (cache == nullptr)
			return false;
		// GlobalLanguage::adjustFontSize with the screen's font scale (FontScale).
		const float factor = fontScale;
		for (std::size_t index = 0; index < styles.size(); ++index)
		{
			const auto size = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::floor(static_cast<float>(styles[index].size) * factor))));
			const Assets::FontAssetHandle handle = cache->Request_Font(styles[index].name, size, styles[index].bold);
			cache->Wait(handle);
			const Assets::FontAsset *asset = cache->Try_Get_Font(handle);
			m_fonts[index] = std::make_unique<Engine::UI::WND::FontFace>();
			if (asset == nullptr || !m_fonts[index]->Build(*asset))
				return false;
		}
		m_width = width;
		return true;
	}

	// Each style's line height (title, minor title, normal), for the view model.
	std::array<int, 3> Heights() const noexcept
	{
		std::array<int, 3> heights{20, 20, 20};
		for (std::size_t index = 0; index < heights.size(); ++index)
			if (m_fonts[index])
				heights[index] = m_fonts[index]->Height();
		return heights;
	}

	bool Draw(const shell::CreditsViewModel &credits, Graphics::Renderer2D &renderer)
	{
		if (!m_fonts[2])
			return true;
		m_list.Clear();
		for (const shell::ShownCredit &shown : credits.Shown())
		{
			const shell::CreditLine &line = *shown.line;
			if (line.style == shell::CreditStyle::Blank || shown.fade <= 0)
				continue;
			const std::size_t font = line.style == shell::CreditStyle::Title ? 0 : line.style == shell::CreditStyle::MinorTitle ? 1 : 2;
			const std::uint32_t argb = credits.Credits().colors[font];
			Engine::UI::WND::TextStyle style;
			// GameMakeColor(r, g, b, a * perc): the alpha byte, truncated.
			const float alpha = static_cast<float>(static_cast<int>((argb >> 24) & 0xFF) * shown.fade / shown.of) / 255.0f;
			style.color = {static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
				static_cast<float>(argb & 0xFF) / 255.0f, alpha};
			style.drop_color = {0.0f, 0.0f, 0.0f, alpha};
			if (line.style == shell::CreditStyle::Column)
			{
				Text(*m_fonts[font], line.text, static_cast<float>(m_width) / 3.0f, shown.y, style);
				Text(*m_fonts[font], line.second, static_cast<float>(m_width) * 2.0f / 3.0f, shown.y, style);
			}
			else
				Text(*m_fonts[font], line.text, static_cast<float>(m_width) / 2.0f, shown.y, style);
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	void Text(const Engine::UI::WND::FontFace &font, const std::u16string &text, float centre, int top, const Engine::UI::WND::TextStyle &style)
	{
		if (text.empty())
			return;
		float width = 0.0f;
		for (const char16_t character : text)
			width += static_cast<float>(font.Get_Char_Spacing(static_cast<std::uint16_t>(character)));
		Engine::UI::WND::StaticTextVisual visual;
		visual.rectangle = {centre - width / 2.0f, static_cast<float>(top), centre + width / 2.0f + 2.0f, static_cast<float>(top + font.Height() + 2)};
		Engine::UI::WND::StaticTextContent content;
		content.font = &font;
		content.text = reinterpret_cast<const std::uint16_t *>(text.c_str());
		content.style = style;
		Engine::UI::WND::Add_Static_Text(m_list, visual, content);
	}

	std::array<std::unique_ptr<Engine::UI::WND::FontFace>, 3> m_fonts;
	std::uint32_t m_width{800};
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
